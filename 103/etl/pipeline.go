package etl

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"sync"
	"time"

	"mongochsync/etl/javascript"
	"mongochsync/etl/wasm"

	"go.mongodb.org/mongo-driver/bson"
)

type Pipeline struct {
	stages    []Stage
	scriptEng *javascript.ScriptEngine
	wasmEng   *wasm.Engine
	dlq       DeadLetterQueue
	mu        sync.RWMutex
	reloading bool
}

type Stage interface {
	Execute(ctx context.Context, doc bson.M) (bson.M, error)
	Name() string
}

type FilterStage struct {
	script string
	engine *javascript.ScriptEngine
}

type ProjectStage struct {
	fields  map[string]int
	renames map[string]string
}

type ConvertStage struct {
	script    string
	engine    *javascript.ScriptEngine
	fieldType map[string]string
}

type TransformStage struct {
	script string
	engine *javascript.ScriptEngine
	mode   string
}

type DeadLetterEvent struct {
	OriginalDoc  bson.M       `json:"original_doc"`
	Error        string       `json:"error"`
	StageName    string       `json:"stage_name"`
	Timestamp    time.Time    `json:"timestamp"`
	PipelineName string       `json:"pipeline_name"`
}

type DeadLetterQueue interface {
	Send(ctx context.Context, event DeadLetterEvent) error
}

type PipelineResult struct {
	Document  bson.M
	Errors   []string
	Filtered bool
}

func NewPipeline() *Pipeline {
	return &Pipeline{
		stages: make([]Stage, 0),
	}
}

func (p *Pipeline) AddStage(stage Stage) {
	p.mu.Lock()
	defer p.mu.Unlock()
	p.stages = append(p.stages, stage)
}

func (p *Pipeline) Execute(ctx context.Context, doc bson.M) *PipelineResult {
	p.mu.RLock()
	stages := p.stages
	p.mu.RUnlock()

	result := &PipelineResult{
		Document: doc,
		Errors:  make([]string, 0),
	}

	currentDoc := doc

	for _, stage := range stages {
		select {
		case <-ctx.Done():
			return result
		default:
		}

		transformed, err := stage.Execute(ctx, currentDoc)
		if err != nil {
			errMsg := fmt.Sprintf("stage %s error: %v", stage.Name(), err)
			result.Errors = append(result.Errors, errMsg)

			dlqErr := p.dlq.Send(ctx, DeadLetterEvent{
				OriginalDoc: doc,
				Error:       err.Error(),
				StageName:   stage.Name(),
				Timestamp:   time.Now(),
			})
			if dlqErr != nil {
				log.Printf("Failed to send to DLQ: %v", dlqErr)
			}

			result.Errors = result.Errors[:len(result.Errors)-1]
			return result
		}

		if transformed == nil {
			result.Filtered = true
			return result
		}

		currentDoc = transformed
	}

	result.Document = currentDoc
	return result
}

func (p *Pipeline) HotReload(newStages []Stage) error {
	p.mu.Lock()
	defer p.mu.Unlock()

	if p.reloading {
		return fmt.Errorf("pipeline is already being reloaded")
	}

	p.reloading = true
	defer func() {
		p.reloading = false
	}()

	oldStages := p.stages
	p.stages = newStages

	for _, stage := range oldStages {
		if js, ok := stage.(*javascript.ScriptEngine); ok {
			js.Close()
		}
	}

	return nil
}

func (p *Pipeline) SetDLQ(dlq DeadLetterQueue) {
	p.dlq = dlq
}

func (p *Pipeline) GetStages() []Stage {
	p.mu.RLock()
	defer p.mu.RUnlock()
	return p.stages
}

func (p *Pipeline) Validate() error {
	if len(p.stages) == 0 {
		return fmt.Errorf("pipeline has no stages")
	}
	return nil
}

func (p *Pipeline) Clone() *Pipeline {
	p.mu.RLock()
	defer p.mu.RUnlock()

	newPipeline := &Pipeline{
		stages:    make([]Stage, len(p.stages)),
		scriptEng: p.scriptEng,
		wasmEng:   p.wasmEng,
		dlq:       p.dlq,
	}
	copy(newPipeline.stages, p.stages)
	return newPipeline
}

func (p *Pipeline) Close() {
	p.mu.Lock()
	defer p.mu.Unlock()

	for _, stage := range p.stages {
		if js, ok := stage.(*javascript.ScriptEngine); ok {
			js.Close()
		}
	}
}

type PipelineStats struct {
	ProcessedCount   int64     `json:"processed_count"`
	FilteredCount    int64     `json:"filtered_count"`
	ErrorCount       int64     `json:"error_count"`
	DLQCount         int64     `json:"dlq_count"`
	AvgLatencyMicros float64   `json:"avg_latency_micros"`
	LastUpdate       time.Time `json:"last_update"`
}

type PipelineManager struct {
	pipelines map[string]*Pipeline
	stats     map[string]*PipelineStats
	mu        sync.RWMutex
	dlq       DeadLetterQueue
}

func NewPipelineManager(dlq DeadLetterQueue) *PipelineManager {
	return &PipelineManager{
		pipelines: make(map[string]*Pipeline),
		stats:     make(map[string]*PipelineStats),
		dlq:       dlq,
	}
}

func (pm *PipelineManager) Register(name string, pipeline *Pipeline) error {
	pm.mu.Lock()
	defer pm.mu.Unlock()

	if _, exists := pm.pipelines[name]; exists {
		return fmt.Errorf("pipeline %s already exists", name)
	}

	pipeline.SetDLQ(pm.dlq)
	pm.pipelines[name] = pipeline
	pm.stats[name] = &PipelineStats{
		LastUpdate: time.Now(),
	}

	return nil
}

func (pm *PipelineManager) Get(name string) (*Pipeline, error) {
	pm.mu.RLock()
	defer pm.mu.RUnlock()

	pipeline, exists := pm.pipelines[name]
	if !exists {
		return nil, fmt.Errorf("pipeline %s not found", name)
	}
	return pipeline, nil
}

func (pm *PipelineManager) HotReload(name string, newPipeline *Pipeline) error {
	pm.mu.Lock()
	defer pm.mu.Unlock()

	oldPipeline, exists := pm.pipelines[name]
	if !exists {
		return fmt.Errorf("pipeline %s not found", name)
	}

	newPipeline.SetDLQ(pm.dlq)
	pm.pipelines[name] = newPipeline
	pm.stats[name] = &PipelineStats{
		LastUpdate: time.Now(),
	}

	go func() {
		oldPipeline.Close()
	}()

	return nil
}

func (pm *PipelineManager) Unregister(name string) error {
	pm.mu.Lock()
	defer pm.mu.Unlock()

	pipeline, exists := pm.pipelines[name]
	if !exists {
		return fmt.Errorf("pipeline %s not found", name)
	}

	pipeline.Close()
	delete(pm.pipelines, name)
	delete(pm.stats, name)

	return nil
}

func (pm *PipelineManager) GetStats(name string) (*PipelineStats, error) {
	pm.mu.RLock()
	defer pm.mu.RUnlock()

	stats, exists := pm.stats[name]
	if !exists {
		return nil, fmt.Errorf("pipeline %s not found", name)
	}

	statsCopy := *stats
	return &statsCopy, nil
}

func (pm *PipelineManager) ListPipelines() []string {
	pm.mu.RLock()
	defer pm.mu.RUnlock()

	names := make([]string, 0, len(pm.pipelines))
	for name := range pm.pipelines {
		names = append(names, name)
	}
	return names
}

func (pm *PipelineManager) Close() {
	pm.mu.Lock()
	defer pm.mu.Unlock()

	for _, pipeline := range pm.pipelines {
		pipeline.Close()
	}
}
