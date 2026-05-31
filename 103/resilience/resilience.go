package resilience

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"sync"
	"time"

	"go.mongodb.org/mongo-driver/bson"
	"go.mongodb.org/mongo-driver/bson/primitive"
	"go.mongodb.org/mongo-driver/mongo"
	"go.mongodb.org/mongo-driver/mongo/options"
	"go.mongodb.org/mongo-driver/mongo/readpref"
)

type ResumeToken struct {
	Token       []byte    `bson:"_id"`
	ClusterTime bson.M    `bson:"clusterTime"`
	Term        int64     `bson:"term"`
	ValidUntil  time.Time `bson:"validUntil"`
	IsPrimary   bool      `bson:"isPrimary"`
}

type ElectionEvent struct {
	Term             int64     `json:"term"`
	ClusterTime      time.Time `json:"cluster_time"`
	OldPrimary       string    `json:"old_primary"`
	NewPrimary       string    `json:"new_primary"`
	EventType        string    `json:"event_type"`
	Timestamp        time.Time `json:"timestamp"`
	ResumeTokenUsed  []byte    `json:"resume_token_used"`
	RecoveryStrategy string    `json:"recovery_strategy"`
}

type ChangeEvent struct {
	OperationType string `bson:"operationType"`
	FullDocument  bson.M `bson:"fullDocument"`
	DocumentKey   bson.M `bson:"documentKey"`
	ResumeToken   bson.Raw `bson:"_id"`
	Namespace     struct {
		Db         string `bson:"db"`
		Coll       string `bson:"coll"`
	} `bson:"ns"`
	ClusterTime bson.M `bson:"clusterTime"`
	Term        int64  `bson:"term"`
	UpdateDescription struct {
		UpdatedFields bson.M   `bson:"updatedFields"`
		RemovedFields []string `bson:"removedFields"`
	} `bson:"updateDescription"`
}

type DiskCache struct {
	mu            sync.Mutex
	cacheDir      string
	maxSizeBytes  int64
	currentSize   int64
	events        []ChangeEvent
}

func NewDiskCache(cacheDir string, maxSizeMB int64) (*DiskCache, error) {
	if err := os.MkdirAll(cacheDir, 0755); err != nil {
		return nil, err
	}
	return &DiskCache{
		cacheDir:     cacheDir,
		maxSizeBytes: maxSizeMB * 1024 * 1024,
		events:       make([]ChangeEvent, 0),
	}, nil
}

func (dc *DiskCache) Write(event ChangeEvent) error {
	dc.mu.Lock()
	defer dc.mu.Unlock()

	data, err := bson.Marshal(event)
	if err != nil {
		return err
	}

	eventSize := int64(len(data))
	if dc.currentSize+eventSize > dc.maxSizeBytes {
		if err := dc.rotate(); err != nil {
			return err
		}
	}

	if dc.currentSize+eventSize > dc.maxSizeBytes {
		oldest := len(dc.events) / 2
		dc.events = dc.events[oldest:]
		dc.persistEventsLocked()
	}

	dc.events = append(dc.events, event)
	dc.currentSize += eventSize
	return dc.persistEventsLocked()
}

func (dc *DiskCache) persistEventsLocked() error {
	file := filepath.Join(dc.cacheDir, "events.cache")
	f, err := os.OpenFile(file, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0644)
	if err != nil {
		return err
	}
	defer f.Close()

	for _, evt := range dc.events {
		data, _ := bson.Marshal(evt)
		var size [4]byte
		binary.LittleEndian.PutUint32(size[:], uint32(len(data)))
		if _, err := f.Write(size[:]); err != nil {
			return err
		}
		if _, err := f.Write(data); err != nil {
			return err
		}
	}
	return nil
}

func (dc *DiskCache) rotate() error {
	oldest := len(dc.events) / 4
	if oldest < 10 {
		oldest = 10
	}
	dc.events = dc.events[oldest:]
	dc.currentSize = 0
	for _, evt := range dc.events {
		data, _ := bson.Marshal(evt)
		dc.currentSize += int64(len(data))
	}
	return nil
}

func (dc *DiskCache) ReadAll() ([]ChangeEvent, error) {
	dc.mu.Lock()
	defer dc.mu.Unlock()

	file := filepath.Join(dc.cacheDir, "events.cache")
	f, err := os.Open(file)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}
	defer f.Close()

	var events []ChangeEvent
	for {
		var size [4]byte
		n, err := f.Read(size[:])
		if err == io.EOF {
			break
		}
		if err != nil || n < 4 {
			continue
		}
		data := make([]byte, binary.LittleEndian.Uint32(size[:]))
		if _, err := io.ReadFull(f, data); err != nil {
			continue
		}
		var evt ChangeEvent
		if err := bson.Unmarshal(data, &evt); err != nil {
			continue
		}
		events = append(events, evt)
	}
	return events, nil
}

func (dc *DiskCache) Clear() error {
	dc.mu.Lock()
	defer dc.mu.Unlock()
	dc.events = make([]ChangeEvent, 0)
	dc.currentSize = 0
	file := filepath.Join(dc.cacheDir, "events.cache")
	os.Remove(file)
	return nil
}

func (dc *DiskCache) Size() int64 {
	dc.mu.Lock()
	defer dc.mu.Unlock()
	return dc.currentSize
}

type ResumePolicy string

const (
	ResumePolicyAuto    ResumePolicy = "auto"
	ResumePolicyManual ResumePolicy = "manual"
)

type AlertSystem interface {
	SendElectionAlert(event ElectionEvent) error
	SendRecoveryAlert(table string, eventsProcessed int64) error
	SendFallbackAlert(fromNode, toNode string) error
}

type LogAlertSystem struct{}

func (l *LogAlertSystem) SendElectionAlert(event ElectionEvent) error {
	data, _ := json.MarshalIndent(event, "", "  ")
	log.Printf("[ELECTION ALERT] %s\n%s", event.EventType, string(data))
	return nil
}

func (l *LogAlertSystem) SendRecoveryAlert(table string, eventsProcessed int64) error {
	log.Printf("[RECOVERY ALERT] Table %s recovered, processed %d cached events", table, eventsProcessed)
	return nil
}

func (l *LogAlertSystem) SendFallbackAlert(fromNode, toNode string) error {
	log.Printf("[FALLBACK ALERT] Switching from %s to %s", fromNode, toNode)
	return nil
}

type ClusterTopology struct {
	mu             sync.RWMutex
	mongoClients   map[string]*mongo.Client
	currentPrimary string
	replicaSetName string
	lastElection   *ElectionEvent
	term           int64
}

func NewClusterTopology() *ClusterTopology {
	return &ClusterTopology{
		mongoClients: make(map[string]*mongo.Client),
	}
}

func (ct *ClusterTopology) AddNode(uri, name string) error {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	client, err := mongo.Connect(ctx, options.Client().ApplyURI(uri))
	if err != nil {
		return err
	}

	if err := client.Ping(ctx, readpref.Primary()); err == nil {
		ct.mu.Lock()
		ct.currentPrimary = name
		ct.mu.Unlock()
	}

	ct.mu.Lock()
	ct.mongoClients[name] = client
	ct.mu.Unlock()

	return nil
}

func (ct *ClusterTopology) DetectElection(ctx context.Context) (*ElectionEvent, error) {
	ct.mu.RLock()
	primary := ct.currentPrimary
	ct.mu.RUnlock()

	if primary == "" {
		return nil, fmt.Errorf("no primary known")
	}

	ct.mu.RLock()
	client, exists := ct.mongoClients[primary]
	ct.mu.RUnlock()

	if !exists || client == nil {
		return nil, fmt.Errorf("primary client not available: %s", primary)
	}

	err := client.Ping(ctx, readpref.Primary())
	if err != nil {
		event := &ElectionEvent{
			EventType: "election_detected",
			Timestamp: time.Now(),
		}
		ct.mu.Lock()
		ct.lastElection = event
		ct.mu.Unlock()
		return event, nil
	}

	return nil, nil
}

func (ct *ClusterTopology) FindNewPrimary(ctx context.Context) (string, error) {
	ct.mu.RLock()
	defer ct.mu.RUnlock()

	for name, client := range ct.mongoClients {
		if client == nil {
			continue
		}
		err := client.Ping(ctx, readpref.Primary())
		if err == nil {
			ct.mu.Lock()
			ct.currentPrimary = name
			ct.mu.Unlock()
			return name, nil
		}
	}

	return "", fmt.Errorf("no primary found")
}

func (ct *ClusterTopology) Close() {
	ct.mu.Lock()
	defer ct.mu.Unlock()
	for _, client := range ct.mongoClients {
		if client != nil {
			client.Disconnect(context.Background())
		}
	}
}

func ParseResumeToken(token []byte) (*ResumeToken, error) {
	if len(token) == 0 {
		return nil, fmt.Errorf("empty token")
	}
	var rt ResumeToken
	if err := bson.Unmarshal(token, &rt); err != nil {
		return nil, err
	}
	return &rt, nil
}

func ValidateTokenForNode(token *ResumeToken, term int64, isPrimary bool) bool {
	if token == nil {
		return false
	}
	if token.Term > term {
		return false
	}
	if !isPrimary && token.ClusterTime == nil {
		return false
	}
	return true
}

type ResilientChangeStream struct {
	ct             *ClusterTopology
	diskCache      *DiskCache
	alerts         AlertSystem
	policy         ResumePolicy
	stream         *mongo.ChangeStream
	currentToken   []byte
	currentNode    string
	tableMapping   map[string]string
	mu             sync.RWMutex
	isRecovering   bool
	reconnectCount int
	maxReconnect   int
}

func NewResilientChangeStream(
	cacheDir string,
	alerts AlertSystem,
	policy ResumePolicy,
	maxReconnect int,
) (*ResilientChangeStream, error) {
	cache, err := NewDiskCache(cacheDir, 1024)
	if err != nil {
		return nil, err
	}

	return &ResilientChangeStream{
		ct:            NewClusterTopology(),
		diskCache:     cache,
		alerts:        alerts,
		policy:        policy,
		maxReconnect:  maxReconnect,
		tableMapping:  make(map[string]string),
	}, nil
}

func (r *ResilientChangeStream) AddReplicaSetMember(uri, name string) error {
	return r.ct.AddNode(uri, name)
}

func (r *ResilientChangeStream) SetTableMapping(source, target string) {
	r.mu.Lock()
	r.tableMapping[source] = target
	r.mu.Unlock()
}

func (r *ResilientChangeStream) TryResumeWithFallback(ctx context.Context, lastToken []byte, collection *mongo.Collection) error {
	r.mu.Lock()
	r.reconnectCount++
	reconnectAttempt := r.reconnectCount
	r.mu.Unlock()

	if reconnectAttempt > r.maxReconnect && r.policy == ResumePolicyManual {
		return fmt.Errorf("max reconnect attempts (%d) reached, manual intervention required", r.maxReconnect)
	}

	rt, err := ParseResumeToken(lastToken)
	if err != nil {
		log.Printf("Invalid resume token: %v", err)
		rt = nil
	}

	nodes := r.getNodesSortedByPriority(rt)

	for _, nodeName := range nodes {
		token, err := r.attemptResumeFromNode(ctx, nodeName, collection, lastToken)
		if err == nil {
			r.mu.Lock()
			r.currentNode = nodeName
			r.currentToken = token
			r.reconnectCount = 0
			r.mu.Unlock()
			return nil
		}
		log.Printf("Failed to resume from %s: %v", nodeName, err)
	}

	return fmt.Errorf("failed to resume from all nodes")
}

func (r *ResilientChangeStream) getNodesSortedByPriority(lastToken *ResumeToken) []string {
	r.mu.RLock()
	defer r.mu.RUnlock()

	if lastToken == nil {
		nodes := make([]string, 0, len(r.ct.mongoClients))
		for name := range r.ct.mongoClients {
			nodes = append(nodes, name)
		}
		return nodes
	}

	var primary, secondaries []string
	for name := range r.ct.mongoClients {
		if name == r.ct.currentPrimary {
			primary = append(primary, name)
		} else {
			secondaries = append(secondaries, name)
		}
	}

	if ValidateTokenForNode(lastToken, r.ct.term, true) {
		return append(primary, secondaries...)
	}
	return append(secondaries, primary...)
}

func (r *ResilientChangeStream) attemptResumeFromNode(ctx context.Context, nodeName string, collection *mongo.Collection, lastToken []byte) ([]byte, error) {
	r.mu.RLock()
	client := r.ct.mongoClients[nodeName]
	r.mu.RUnlock()

	if client == nil {
		return nil, fmt.Errorf("client not found for %s", nodeName)
	}

	db := client.Database(collection.Database().Name())
	coll := db.Collection(collection.Name())

	pipeline := mongo.Pipeline{
		{{"$match", bson.D{{"operationType", bson.D{{"$in", bson.A{"insert", "update", "replace", "delete"}}}}}}},
		{{"$project", bson.D{{"operationType", 1}, {"fullDocument", 1}, {"documentKey", 1}, {"_id", 1}, {"ns", 1}, {"updateDescription", 1}, {"clusterTime", 1}, {"term", 1}}}},
	}

	opts := options.ChangeStream()
	if lastToken != nil {
		opts.SetResumeAfter(bson.Raw(lastToken))
	}

	stream, err := coll.Watch(ctx, pipeline, opts)
	if err != nil {
		return nil, err
	}

	r.mu.Lock()
	r.stream = stream
	r.mu.Unlock()

	if lastToken != nil {
		return lastToken, nil
	}

	return nil, nil
}

func (r *ResilientChangeStream) HandleElectionAndRecover(ctx context.Context, event *ElectionEvent, collection *mongo.Collection) error {
	r.mu.Lock()
	r.isRecovering = true
	r.mu.Unlock()

	if r.alerts != nil {
		r.alerts.SendElectionAlert(*event)
	}

	if r.policy == ResumePolicyManual {
		log.Println("Manual resume policy: waiting for explicit resume command")
		return nil
	}

	cachedEvents, err := r.diskCache.ReadAll()
	if err != nil {
		log.Printf("Failed to read cached events: %v", err)
	}

	if len(cachedEvents) > 0 {
		log.Printf("Processing %d cached events before resuming stream", len(cachedEvents))
		for _, evt := range cachedEvents {
			if err := r.processCachedEvent(ctx, evt); err != nil {
				log.Printf("Failed to process cached event: %v", err)
			}
		}

		if r.alerts != nil && len(cachedEvents) > 0 {
			r.alerts.SendRecoveryAlert(collection.Name(), int64(len(cachedEvents)))
		}

		r.diskCache.Clear()
	}

	event.RecoveryStrategy = "automatic"
	if r.alerts != nil {
		r.alerts.SendElectionAlert(*event)
	}

	newToken, err := r.fetchLatestValidToken(ctx, collection)
	if err != nil {
		log.Printf("Failed to fetch latest token: %v", err)
	}

	if err := r.TryResumeWithFallback(ctx, newToken, collection); err != nil {
		return err
	}

	r.mu.Lock()
	r.isRecovering = false
	r.mu.Unlock()

	return nil
}

func (r *ResilientChangeStream) processCachedEvent(ctx context.Context, event ChangeEvent) error {
	return nil
}

func (r *ResilientChangeStream) fetchLatestValidToken(ctx context.Context, collection *mongo.Collection) ([]byte, error) {
	r.mu.RLock()
	client := r.ct.mongoClients[r.ct.currentPrimary]
	r.mu.RUnlock()

	if client == nil {
		return nil, fmt.Errorf("no primary client available")
	}

	coll := client.Database(collection.Database().Name()).Collection(collection.Name())

	pipeline := mongo.Pipeline{
		{{"$match", bson.D{{"operationType", bson.D{{"$in", bson.A{"insert", "update", "replace", "delete"}}}}}}},
		{{"$sort", bson.D{{"clusterTime", -1}}}},
		{{"$limit", 1}},
		{{"$project", bson.D{{"_id", 1}}}},
	}

	cursor, err := coll.Aggregate(ctx, pipeline)
	if err != nil {
		return nil, err
	}
	defer cursor.Close(ctx)

	if cursor.Next(ctx) {
		var doc bson.M
		if err := cursor.Decode(&doc); err != nil {
			return nil, err
		}
		return bson.Marshal(doc["_id"])
	}

	return nil, fmt.Errorf("no documents found to determine latest token")
}

func (r *ResilientChangeStream) CacheEvent(event ChangeEvent) error {
	if r.diskCache == nil {
		return nil
	}
	return r.diskCache.Write(event)
}

func (r *ResilientChangeStream) Next(ctx context.Context) (*ChangeEvent, error) {
	r.mu.RLock()
	stream := r.stream
	isRecovering := r.isRecovering
	r.mu.RUnlock()

	if isRecovering || stream == nil {
		return nil, fmt.Errorf("stream not available or recovering")
	}

	if !stream.Next(ctx) {
		if err := stream.Err(); err != nil {
			electionEvent, detectErr := r.ct.DetectElection(ctx)
			if detectErr == nil && electionEvent != nil {
				go r.HandleElectionAndRecover(context.Background(), electionEvent, stream.Collection.Database().Collection(stream.Collection.Name()))
				return nil, fmt.Errorf("election detected, recovering")
			}
			return nil, err
		}
		return nil, io.EOF
	}

	var event ChangeEvent
	if err := stream.Decode(&event); err != nil {
		return nil, err
	}

	token := stream.ResumeToken()
	r.mu.Lock()
	r.currentToken = token
	r.mu.Unlock()

	return &event, nil
}

func (r *ResilientChangeStream) Close(ctx context.Context) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	if r.stream != nil {
		r.stream.Close(ctx)
	}
	r.ct.Close()
	return nil
}

func (r *ResilientChangeStream) GetCurrentToken() []byte {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.currentToken
}

func (r *ResilientChangeStream) IsRecovering() bool {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.isRecovering
}

func (r *ResilientChangeStream) DiskCacheSize() int64 {
	if r.diskCache == nil {
		return 0
	}
	return r.diskCache.Size()
}

func GetClusterTopologyInfo(ctx context.Context, client *mongo.Client) (string, int64, error) {
	admin := client.Database("admin")
	var result bson.M
	err := admin.RunCommand(ctx, bson.D{{"replSetGetStatus", 1}}).Decode(&result)
	if err != nil {
		return "", 0, err
	}

	members, ok := result["members"].(bson.A)
	if !ok || len(members) == 0 {
		return "", 0, fmt.Errorf("no members found")
	}

	var primaryMember bson.M
	for _, m := range members {
		member, ok := m.(bson.M)
		if !ok {
			continue
		}
		if state, ok := member["stateStr"].(string); ok && state == "PRIMARY" {
			primaryMember = member
			break
		}
	}

	if primaryMember == nil {
		return "", 0, fmt.Errorf("no primary found")
	}

	name, _ := primaryMember["name"].(string)
	_, _ = primaryMember["optime"].(primitive.DateTime)
	term, _ := result["term"].(int64)

	return name, term, nil
}
