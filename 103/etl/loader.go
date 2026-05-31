package etl

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sync"

	"gopkg.in/yaml.v3"

	"mongochsync/etl/javascript"
)

type PipelineConfig struct {
	Name     string      `yaml:"name"`
	Stages   []StageConfig `yaml:"stages"`
}

type StageConfig struct {
	Type   string                 `yaml:"type"`
	Name   string                 `yaml:"name"`
	Config map[string]interface{} `yaml:"config"`
}

type PipelineLoader struct {
	manager *PipelineManager
	mu      sync.RWMutex
	watcher *ConfigWatcher
}

func NewPipelineLoader(manager *PipelineManager) *PipelineLoader {
	return &PipelineLoader{
		manager: manager,
	}
}

func (l *PipelineLoader) LoadFromFile(path string) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("failed to read config file: %w", err)
	}

	return l.LoadFromBytes(data)
}

func (l *PipelineLoader) LoadFromBytes(data []byte) error {
	var configs []PipelineConfig
	if err := yaml.Unmarshal(data, &configs); err != nil {
		return fmt.Errorf("failed to parse config: %w", err)
	}

	for _, cfg := range configs {
		pipeline, err := l.BuildPipeline(cfg)
		if err != nil {
			return fmt.Errorf("failed to build pipeline %s: %w", cfg.Name, err)
		}

		if err := l.manager.Register(cfg.Name, pipeline); err != nil {
			return fmt.Errorf("failed to register pipeline %s: %w", cfg.Name, err)
		}
	}

	return nil
}

func (l *PipelineLoader) BuildPipeline(cfg PipelineConfig) (*Pipeline, error) {
	pipeline := NewPipeline()

	for i, stageCfg := range cfg.Stages {
		stage, err := l.buildStage(stageCfg)
		if err != nil {
			return nil, fmt.Errorf("failed to build stage %d (%s): %w", i, stageCfg.Name, err)
		}
		pipeline.AddStage(stage)
	}

	return pipeline, nil
}

func (l *PipelineLoader) buildStage(cfg StageConfig) (Stage, error) {
	switch cfg.Type {
	case "filter":
		return l.buildFilterStage(cfg)
	case "project":
		return l.buildProjectStage(cfg)
	case "convert":
		return l.buildConvertStage(cfg)
	case "transform":
		return l.buildTransformStage(cfg)
	case "wasm":
		return l.buildWasmStage(cfg)
	default:
		return nil, fmt.Errorf("unknown stage type: %s", cfg.Type)
	}
}

func (l *PipelineLoader) buildFilterStage(cfg StageConfig) (Stage, error) {
	script, ok := cfg.Config["script"].(string)
	if !ok {
		return nil, fmt.Errorf("filter stage missing script")
	}

	jsStage, err := javascript.NewJSFilterStage(script)
	if err != nil {
		return nil, err
	}
	return jsStage, nil
}

func (l *PipelineLoader) buildProjectStage(cfg StageConfig) (Stage, error) {
	fieldsRaw, ok := cfg.Config["fields"].([]interface{})
	if !ok {
		fieldsRaw = []interface{}{}
	}

	fields := make([]string, 0, len(fieldsRaw))
	for _, f := range fieldsRaw {
		if s, ok := f.(string); ok {
			fields = append(fields, s)
		}
	}

	renames := make(map[string]string)
	if renamesRaw, ok := cfg.Config["renames"].(map[string]interface{}); ok {
		for k, v := range renamesRaw {
			if s, ok := v.(string); ok {
				renames[k] = s
			}
		}
	}

	script := cfg.Config["script"]
	var scriptStr string
	if script != nil {
		scriptStr = script.(string)
	} else {
		scriptStr = l.generateProjectScript(fields, renames)
	}

	jsStage, err := javascript.NewJSProjectStage(scriptStr, renames)
	if err != nil {
		return nil, err
	}
	return jsStage, nil
}

func (l *PipelineLoader) generateProjectScript(fields []string, renames map[string]string) string {
	if len(fields) == 0 && len(renames) == 0 {
		return "return doc;"
	}

	script := "var result = {};\n"
	for _, field := range fields {
		script += fmt.Sprintf("if (doc.%s !== undefined) { result.%s = doc.%s; }\n", field, field, field)
	}
	for oldName, newName := range renames {
		script += fmt.Sprintf("if (doc.%s !== undefined) { result.%s = doc.%s; }\n", oldName, newName, oldName)
	}
	script += "return result;"
	return script
}

func (l *PipelineLoader) buildConvertStage(cfg StageConfig) (Stage, error) {
	script, ok := cfg.Config["script"].(string)
	if !ok {
		return nil, fmt.Errorf("convert stage missing script")
	}

	fieldType := make(map[string]string)
	if fieldTypeRaw, ok := cfg.Config["field_type"].(map[string]interface{}); ok {
		for k, v := range fieldTypeRaw {
			if s, ok := v.(string); ok {
				fieldType[k] = s
			}
		}
	}

	jsStage, err := javascript.NewJSConvertStage(script, fieldType)
	if err != nil {
		return nil, err
	}
	return jsStage, nil
}

func (l *PipelineLoader) buildTransformStage(cfg StageConfig) (Stage, error) {
	script, ok := cfg.Config["script"].(string)
	if !ok {
		return nil, fmt.Errorf("transform stage missing script")
	}

	jsStage, err := javascript.NewJSTransformStage(script)
	if err != nil {
		return nil, err
	}
	return jsStage, nil
}

func (l *PipelineLoader) buildWasmStage(cfg StageConfig) (Stage, error) {
	wasmPath, ok := cfg.Config["path"].(string)
	if !ok {
		return nil, fmt.Errorf("wasm stage missing path")
	}

	wasmBytes, err := os.ReadFile(wasmPath)
	if err != nil {
		return nil, fmt.Errorf("failed to read wasm file: %w", err)
	}

	return NewWasmStage(wasmBytes)
}

func (l *PipelineLoader) WatchConfigDir(dir string) error {
	watcher, err := NewConfigWatcher(dir)
	if err != nil {
		return err
	}

	l.mu.Lock()
	l.watcher = watcher
	l.mu.Unlock()

	go l.watchLoop()

	return nil
}

func (l *PipelineLoader) watchLoop() {
	l.mu.RLock()
	watcher := l.watcher
	l.mu.RUnlock()

	for event := range watcher.Events {
		if event.Type == ConfigEventModify || event.Type == ConfigEventCreate {
			if err := l.LoadFromFile(event.Path); err != nil {
				fmt.Printf("Failed to reload config: %v\n", err)
				continue
			}
			fmt.Printf("Reloaded pipeline config: %s\n", event.Path)
		}
	}
}

type ConfigEventType int

const (
	ConfigEventCreate ConfigEventType = iota
	ConfigEventModify
	ConfigEventDelete
)

type ConfigEvent struct {
	Type ConfigEventType
	Path string
}

type ConfigWatcher struct {
	dir     string
	events  chan ConfigEvent
	done    chan struct{}
}

func NewConfigWatcher(dir string) (*ConfigWatcher, error) {
	watcher := &ConfigWatcher{
		dir:    dir,
		events: make(chan ConfigEvent, 100),
		done:   make(chan struct{}),
	}

	return watcher, nil
}

func (w *ConfigWatcher) Events() <-chan ConfigEvent {
	return w.events
}

func (w *ConfigWatcher) Close() error {
	close(w.done)
	return nil
}

func (l *PipelineLoader) HotReloadPipeline(name string, newPipeline *Pipeline) error {
	return l.manager.HotReload(name, newPipeline)
}

func (l *PipelineLoader) GetPipeline(name string) (*Pipeline, error) {
	return l.manager.Get(name)
}

func (l *PipelineLoader) ListPipelines() []string {
	return l.manager.ListPipelines()
}

type PipelineDebugRequest struct {
	PipelineName string                 `json:"pipeline_name"`
	InputDoc     map[string]interface{} `json:"input_doc"`
}

type PipelineDebugResponse struct {
	Success     bool                   `json:"success"`
	OutputDoc   map[string]interface{} `json:"output_doc,omitempty"`
	Errors      []string               `json:"errors,omitempty"`
	Filtered    bool                   `json:"filtered"`
	LatencyUs   int64                  `json:"latency_us"`
}

func (l *PipelineLoader) DebugExecute(ctx context.Context, req PipelineDebugRequest) (*PipelineDebugResponse, error) {
	pipeline, err := l.manager.Get(req.PipelineName)
	if err != nil {
		return nil, err
	}

	inputBson := toBsonM(req.InputDoc)

	start := time.Now()
	result := pipeline.Execute(ctx, inputBson)
	latency := time.Since(start).Microseconds()

	response := &PipelineDebugResponse{
		Success:   len(result.Errors) == 0 && !result.Filtered,
		Errors:    result.Errors,
		Filtered:  result.Filtered,
		LatencyUs: latency,
	}

	if result.Document != nil {
		response.OutputDoc = toMap(result.Document)
	}

	return response, nil
}

func toBsonM(m map[string]interface{}) bson.M {
	result := make(bson.M)
	for k, v := range m {
		result[k] = v
	}
	return result
}

func toMap(m bson.M) map[string]interface{} {
	result := make(map[string]interface{})
	for k, v := range m {
		result[k] = v
	}
	return result
}

func (l *PipelineLoader) ValidatePipeline(name string) (bool, []string, error) {
	pipeline, err := l.manager.Get(name)
	if err != nil {
		return false, nil, err
	}

	if err := pipeline.Validate(); err != nil {
		return false, []string{err.Error()}, nil
	}

	return true, nil, nil
}

func LoadPipelineDefinitions(path string) ([]PipelineConfig, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}

	var configs []PipelineConfig
	if err := yaml.Unmarshal(data, &configs); err != nil {
		return nil, err
	}

	return configs, nil
}

func SavePipelineDefinitions(path string, configs []PipelineConfig) error {
	data, err := yaml.Marshal(configs)
	if err != nil {
		return err
	}

	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return err
	}

	return os.WriteFile(path, data, 0644)
}

func ConvertMapToConfig(input map[string]interface{}) ([]StageConfig, error) {
	stagesRaw, ok := input["stages"].([]interface{})
	if !ok {
		return nil, fmt.Errorf("missing stages")
	}

	stages := make([]StageConfig, 0, len(stagesRaw))
	for i, s := range stagesRaw {
		stageMap, ok := s.(map[string]interface{})
		if !ok {
			return nil, fmt.Errorf("invalid stage at index %d", i)
		}

		stageType, ok := stageMap["type"].(string)
		if !ok {
			return nil, fmt.Errorf("missing type in stage %d", i)
		}

		stage := StageConfig{
			Type:   stageType,
			Name:   getString(stageMap, "name", stageType),
			Config: getMap(stageMap, "config", make(map[string]interface{})),
		}
		stages = append(stages, stage)
	}

	return stages, nil
}

func getString(m map[string]interface{}, key, defaultVal string) string {
	if v, ok := m[key].(string); ok {
		return v
	}
	return defaultVal
}

func getMap(m map[string]interface{}, key string, defaultVal map[string]interface{}) map[string]interface{} {
	if v, ok := m[key].(map[string]interface{}); ok {
		return v
	}
	return defaultVal
}
