package syncer

import (
	"context"
	"fmt"
	"log"
	"sync"
	"time"

	"mongochsync/clickhouse"
	"mongochsync/config"
	"mongochsync/converter"
	"mongochsync/dlq"
	"mongochsync/etl"
	"mongochsync/mongodb"
	"mongochsync/resilience"

	"go.mongodb.org/mongo-driver/bson"
	"go.mongodb.org/mongo-driver/bson/primitive"
	"go.mongodb.org/mongo-driver/mongo"
)

type Syncer struct {
	cfg              *config.Config
	mongo            *mongodb.Client
	ch               *clickhouse.Client
	resilient        *resilience.ResilientChangeStream
	pipelineManager  *etl.PipelineManager
	pipelineLoader   *etl.PipelineLoader
	debugServer      *etl.DebugServer
	dlq              etl.DeadLetterQueue
	batch            map[string][]map[string]interface{}
	batchMu          sync.Mutex
	processed        int64
	wg               sync.WaitGroup
	ctx              context.Context
	cancel           context.CancelFunc
}

func New(cfg *config.Config) (*Syncer, error) {
	mongoClient, err := mongodb.NewClient(cfg.MongoDB.URI, cfg.MongoDB.Database)
	if err != nil {
		return nil, err
	}

	chClient, err := clickhouse.NewClient(cfg.ClickHouse.DSN, cfg.ClickHouse.Database)
	if err != nil {
		mongoClient.Close()
		return nil, err
	}

	ctx, cancel := context.WithCancel(context.Background())

	syncer := &Syncer{
		cfg:     cfg,
		mongo:   mongoClient,
		ch:      chClient,
		batch:   make(map[string][]map[string]interface{}),
		ctx:     ctx,
		cancel:  cancel,
	}

	if cfg.Resilience.ResumePolicy != "" {
		syncer.setupResilience()
	}

	if cfg.ETL.Enabled {
		if err := syncer.setupETL(); err != nil {
			log.Printf("Warning: failed to setup ETL: %v", err)
		}
	}

	return syncer, nil
}

func (s *Syncer) setupResilience() {
	policy := resilience.ResumePolicyAuto
	if s.cfg.Resilience.ResumePolicy == "manual" {
		policy = resilience.ResumePolicyManual
	}

	resilientStream, err := resilience.NewResilientChangeStream(
		s.cfg.Resilience.CacheDir,
		&resilience.LogAlertSystem{},
		policy,
		s.cfg.Resilience.MaxReconnect,
	)
	if err != nil {
		log.Printf("Warning: failed to create resilient stream: %v", err)
		return
	}

	for _, member := range s.cfg.MongoDB.ReplicaSetMembers {
		if err := resilientStream.AddReplicaSetMember(member.URI, member.Name); err != nil {
			log.Printf("Warning: failed to add replica set member %s: %v", member.Name, err)
		}
	}

	s.resilient = resilientStream
}

func (s *Syncer) setupETL() error {
	var dlq etl.DeadLetterQueue

	if s.cfg.ETL.DLQ.Type == "kafka" {
		kafkaDLQ, err := dlq.NewKafkaDLQ(dlq.DLQConfig{
			Brokers:       s.cfg.ETL.DLQ.Brokers,
			Topic:         s.cfg.ETL.DLQ.Topic,
			BufferSize:    s.cfg.ETL.DLQ.BufferSize,
			FlushInterval: 1000,
		})
		if err != nil {
			log.Printf("Warning: failed to create Kafka DLQ: %v, using in-memory DLQ", err)
			dlq = dlq.NewInMemoryDLQ()
		} else {
			dlq = kafkaDLQ
		}
	} else {
		dlq = dlq.NewInMemoryDLQ()
	}
	s.dlq = dlq

	manager := etl.NewPipelineManager(dlq)
	s.pipelineManager = manager

	loader := etl.NewPipelineLoader(manager)
	s.pipelineLoader = loader

	if s.cfg.ETL.PipelineFile != "" {
		if err := loader.LoadFromFile(s.cfg.ETL.PipelineFile); err != nil {
			log.Printf("Warning: failed to load pipeline file: %v", err)
		}
	}

	if s.cfg.ETL.DebugServer.Enabled {
		debugSrv := etl.NewDebugServer(s.cfg.ETL.DebugServer.Addr, loader, manager)
		if err := debugSrv.Start(); err != nil {
			log.Printf("Warning: failed to start debug server: %v", err)
		} else {
			s.debugServer = debugSrv
		}
	}

	return nil
}

func (s *Syncer) Close() error {
	s.cancel()
	s.wg.Wait()

	if s.debugServer != nil {
		s.debugServer.Stop()
	}

	if s.pipelineManager != nil {
		s.pipelineManager.Close()
	}

	if s.dlq != nil {
		s.dlq.Close()
	}

	if s.resilient != nil {
		s.resilient.Close(s.ctx)
	}

	s.mongo.Close()
	s.ch.Close()
	return nil
}

func (s *Syncer) InitialSync() error {
	log.Println("Starting initial sync...")
	for _, tbl := range s.cfg.Tables {
		if err := s.syncCollection(tbl); err != nil {
			return fmt.Errorf("failed to sync collection %s: %w", tbl.Source, err)
		}
	}
	log.Println("Initial sync completed")
	return nil
}

func (s *Syncer) syncCollection(tbl config.TableMapping) error {
	ctx := context.Background()
	cursor, err := s.mongo.FindAll(ctx, tbl.Source)
	if err != nil {
		return err
	}
	defer cursor.Close(ctx)

	var docs []map[string]interface{}
	batchSize := s.cfg.Sync.BatchSize

	for cursor.Next(ctx) {
		var doc bson.M
		if err := cursor.Decode(&doc); err != nil {
			log.Printf("Error decoding document: %v", err)
			continue
		}

		converted, err := converter.ConvertBSONToClickHouse(doc, tbl.PrimaryKey, tbl.VersionField)
		if err != nil {
			log.Printf("Error converting document: %v", err)
			continue
		}

		if s.pipelineLoader != nil && tbl.Pipeline != "" {
			result := s.executePipeline(tbl.Pipeline, converted)
			if result.Filtered {
				continue
			}
			if len(result.Errors) > 0 {
				log.Printf("Pipeline errors for doc: %v", result.Errors)
			}
			converted = result.Document
		}

		docs = append(docs, converted)
		if len(docs) >= batchSize {
			if err := s.ch.BatchInsert(ctx, tbl.Target, docs); err != nil {
				return err
			}
			s.processed += int64(len(docs))
			docs = []map[string]interface{}{}
		}
	}

	if len(docs) > 0 {
		if err := s.ch.BatchInsert(ctx, tbl.Target, docs); err != nil {
			return err
		}
		s.processed += int64(len(docs))
	}

	return cursor.Err()
}

func (s *Syncer) executePipeline(name string, doc bson.M) *etl.PipelineResult {
	pipeline, err := s.pipelineManager.Get(name)
	if err != nil {
		return &etl.PipelineResult{
			Document: doc,
			Errors:   []string{err.Error()},
		}
	}
	return pipeline.Execute(s.ctx, doc)
}

func (s *Syncer) Start() error {
	log.Println("Starting change stream sync...")
	if err := s.ch.InitStateTable(s.ctx); err != nil {
		return err
	}

	if s.resilient == nil && s.pipelineManager == nil {
		return s.startSimpleMode()
	}

	return s.startAdvancedMode()
}

func (s *Syncer) startSimpleMode() error {
	var resumeToken []byte
	if len(s.cfg.Tables) > 0 {
		token, err := s.ch.GetResumeToken(s.ctx, s.cfg.Tables[0].Source)
		if err != nil {
			log.Printf("Warning: failed to get resume token: %v", err)
		} else {
			resumeToken = token
		}
	}

	stream, err := s.mongo.Watch(s.ctx, resumeToken)
	if err != nil {
		return err
	}
	defer stream.Close(s.ctx)

	s.wg.Add(1)
	go s.batchWorker()

	for stream.Next(s.ctx) {
		var event mongodb.ChangeEvent
		if err := stream.Decode(&event); err != nil {
			log.Printf("Error decoding change event: %v", err)
			continue
		}

		s.processEvent(event)
		s.saveResumeToken(stream.ResumeToken())
	}

	if err := stream.Err(); err != nil {
		return err
	}

	return nil
}

func (s *Syncer) startAdvancedMode() error {
	for _, tbl := range s.cfg.Tables {
		if s.pipelineLoader != nil && tbl.Pipeline != "" {
			s.pipelineLoader.GetPipeline(tbl.Pipeline)
		}
	}

	var resumeToken []byte
	if s.resilient != nil && len(s.cfg.Tables) > 0 {
		token, err := s.ch.GetResumeToken(s.ctx, s.cfg.Tables[0].Source)
		if err != nil {
			log.Printf("Warning: failed to get resume token: %v", err)
		} else {
			resumeToken = token
		}
	}

	s.wg.Add(1)
	go s.batchWorker()

	if s.resilient != nil {
		return s.startResilientMode(resumeToken)
	}

	return s.startSimpleMode()
}

func (s *Syncer) startResilientMode(resumeToken []byte) error {
	for _, tbl := range s.cfg.Tables {
		s.resilient.SetTableMapping(tbl.Source, tbl.Target)
	}

	collection := s.mongo.GetCollection(s.cfg.Tables[0].Source)
	if err := s.resilient.TryResumeWithFallback(s.ctx, resumeToken, collection); err != nil {
		log.Printf("Warning: initial resume failed, starting fresh: %v", err)
	}

	s.wg.Add(1)
	go s.monitorElections()

	return s.watchResilientStream(collection)
}

func (s *Syncer) watchResilientStream(collection *mongo.Collection) error {
	for {
		select {
		case <-s.ctx.Done():
			return nil
		default:
			event, err := s.resilient.Next(s.ctx)
			if err != nil {
				if s.ctx.Err() != nil {
					return nil
				}
				log.Printf("Stream error: %v", err)
				time.Sleep(time.Second)
				continue
			}

			if event != nil {
				s.processResilientEvent(*event)
				s.saveResumeToken(event.ResumeToken)
			}
		}
	}
}

func (s *Syncer) monitorElections() {
	defer s.wg.Done()
	ticker := time.NewTicker(time.Duration(s.cfg.Resilience.ElectionTimeout) * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-s.ctx.Done():
			return
		case <-ticker.C:
			if s.resilient == nil {
				continue
			}
			electionEvent, err := s.resilient.ct.DetectElection(s.ctx)
			if err == nil && electionEvent != nil {
				log.Printf("Election detected: %s", electionEvent.EventType)
				collection := s.mongo.GetCollection(s.cfg.Tables[0].Source)
				if err := s.resilient.HandleElectionAndRecover(s.ctx, electionEvent, collection); err != nil {
					log.Printf("Recovery failed: %v", err)
				}
			}
		}
	}
}

func (s *Syncer) processResilientEvent(event resilience.ChangeEvent) {
	tableMapping := s.findTableMapping(event.Namespace.Coll)
	if tableMapping == nil {
		return
	}

	var doc bson.M
	if event.FullDocument != nil {
		doc = event.FullDocument
	}

	switch event.OperationType {
	case "insert", "replace", "update":
		if doc != nil {
			converted, err := converter.ConvertBSONToClickHouse(doc, tableMapping.PrimaryKey, tableMapping.VersionField)
			if err != nil {
				log.Printf("Error converting document: %v", err)
				return
			}

			if s.pipelineLoader != nil && tableMapping.Pipeline != "" {
				result := s.executePipeline(tableMapping.Pipeline, converted)
				if result.Filtered {
					return
				}
				if len(result.Errors) > 0 {
					log.Printf("Pipeline errors: %v", result.Errors)
				}
				converted = result.Document
			}

			s.addToBatch(tableMapping.Target, converted)
		}
	case "delete":
		if oid, ok := event.DocumentKey["_id"]; ok {
			var id interface{}
			if objID, isOid := oid.(primitive.ObjectID); isOid {
				id = objID.Hex()
			} else {
				id = oid
			}
			s.ch.Delete(s.ctx, tableMapping.Target, tableMapping.PrimaryKey, id)
		}
	}
	s.processed++
}

func (s *Syncer) processEvent(event mongodb.ChangeEvent) {
	tableMapping := s.findTableMapping(event.Namespace.Coll)
	if tableMapping == nil {
		return
	}

	var doc bson.M
	if event.FullDocument != nil {
		doc = event.FullDocument
	}

	switch event.OperationType {
	case "insert", "replace", "update":
		if doc != nil {
			converted, err := converter.ConvertBSONToClickHouse(doc, tableMapping.PrimaryKey, tableMapping.VersionField)
			if err != nil {
				log.Printf("Error converting document: %v", err)
				return
			}

			if s.pipelineLoader != nil && tableMapping.Pipeline != "" {
				result := s.executePipeline(tableMapping.Pipeline, converted)
				if result.Filtered {
					return
				}
				converted = result.Document
			}

			s.addToBatch(tableMapping.Target, converted)
		}
	case "delete":
		if oid, ok := event.DocumentKey["_id"]; ok {
			var id interface{}
			if objID, isOid := oid.(bson.ObjectID); isOid {
				id = objID.Hex()
			} else {
				id = oid
			}
			s.ch.Delete(s.ctx, tableMapping.Target, tableMapping.PrimaryKey, id)
		}
	}
	s.processed++
}

func (s *Syncer) findTableMapping(source string) *config.TableMapping {
	for i := range s.cfg.Tables {
		if s.cfg.Tables[i].Source == source {
			return &s.cfg.Tables[i]
		}
	}
	return nil
}

func (s *Syncer) addToBatch(table string, doc map[string]interface{}) {
	s.batchMu.Lock()
	s.batch[table] = append(s.batch[table], doc)
	s.batchMu.Unlock()
}

func (s *Syncer) batchWorker() {
	defer s.wg.Done()
	ticker := time.NewTicker(time.Duration(s.cfg.Sync.BatchTimeout) * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-s.ctx.Done():
			s.flushAll()
			return
		case <-ticker.C:
			s.flushAll()
		}
	}
}

func (s *Syncer) flushAll() {
	s.batchMu.Lock()
	defer s.batchMu.Unlock()

	for table, docs := range s.batch {
		if len(docs) > 0 {
			if err := s.ch.BatchInsert(s.ctx, table, docs); err != nil {
				log.Printf("Error batch insert failed for table %s: %v", table, err)
			}
			s.batch[table] = []map[string]interface{}{}
		}
	}
}

func (s *Syncer) saveResumeToken(token bson.Raw) {
	if len(s.cfg.Tables) > 0 {
		lag := time.Since(time.Now()).Seconds()
		s.ch.SaveResumeToken(s.ctx, s.cfg.Tables[0].Source, token, s.processed, lag)
	}
}

func (s *Syncer) PrintStatus() {
	states, err := s.ch.GetStatus(s.ctx)
	if err != nil {
		log.Printf("Error getting status: %v", err)
		return
	}

	fmt.Println("\nSync Status:")
	fmt.Println("============")
	for _, state := range states {
		fmt.Printf("Table: %s\n", state.Table)
		fmt.Printf("  Processed: %d\n", state.Processed)
		fmt.Printf("  Lag: %.2f seconds\n", state.LagSeconds)
		fmt.Printf("  Last Update: %s\n", state.LastUpdate.Format(time.RFC3339))
		fmt.Println()
	}
	fmt.Printf("Total Processed: %d\n", s.processed)

	if s.resilient != nil {
		fmt.Printf("Resilient Mode: %s\n", s.cfg.Resilience.ResumePolicy)
		fmt.Printf("Cache Size: %d bytes\n", s.resilient.DiskCacheSize())
	}

	if s.pipelineManager != nil {
		fmt.Println("\nETL Pipeline Status:")
		fmt.Println("===================")
		for _, name := range s.pipelineLoader.ListPipelines() {
			stats, _ := s.pipelineManager.GetStats(name)
			if stats != nil {
				fmt.Printf("Pipeline: %s\n", name)
				fmt.Printf("  Processed: %d\n", stats.ProcessedCount)
				fmt.Printf("  Errors: %d\n", stats.ErrorCount)
				fmt.Printf("  DLQ Count: %d\n", stats.DLQCount)
			}
		}
	}
}

func (s *Syncer) ManualResume() error {
	if s.resilient == nil {
		return fmt.Errorf("resilient mode not enabled")
	}

	collection := s.mongo.GetCollection(s.cfg.Tables[0].Source)
	token := s.resilient.GetCurrentToken()
	return s.resilient.TryResumeWithFallback(s.ctx, token, collection)
}

func (s *Syncer) ReloadPipelines() error {
	if s.pipelineLoader == nil {
		return fmt.Errorf("ETL not enabled")
	}

	if s.cfg.ETL.PipelineFile == "" {
		return fmt.Errorf("no pipeline file configured")
	}

	return s.pipelineLoader.LoadFromFile(s.cfg.ETL.PipelineFile)
}

func (s *Syncer) GetPipelineNames() []string {
	if s.pipelineLoader == nil {
		return []string{}
	}
	return s.pipelineLoader.ListPipelines()
}
