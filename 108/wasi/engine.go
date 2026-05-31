package wasi

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"sync"
	"time"

	"github.com/bytecodealliance/wasmtime-go/v18"
	"github.com/wasi-service/runtime/compile"
	"go.uber.org/zap"
)

const (
	DefaultMemoryLimitMB = 64
	DefaultMaxInstructions = 10_000_000
	DefaultTimeoutSeconds = 10
)

type InstanceConfig struct {
	MemoryLimitMB   int
	MaxInstructions int64
	TimeoutSeconds  int
	ModuleVersion   string
	UseAOTCache     bool
	CacheDir        string
	OptLevel        compile.OptimizationLevel
	WasmPath        string
}

func DefaultInstanceConfig() *InstanceConfig {
	return &InstanceConfig{
		MemoryLimitMB:   DefaultMemoryLimitMB,
		MaxInstructions: DefaultMaxInstructions,
		TimeoutSeconds:  DefaultTimeoutSeconds,
		ModuleVersion:   "latest",
		UseAOTCache:     false,
		CacheDir:        "./aot-cache",
		OptLevel:        compile.OptLevel_O2,
	}
}

type WasmInstance struct {
	id       string
	engine   *wasmtime.Engine
	store    *wasmtime.Store
	linker   *wasmtime.Linker
	module   *wasmtime.Module
	instance *wasmtime.Instance
	mu       sync.Mutex

	handleRequestFunc *wasmtime.Func
	healthFunc        *wasmtime.Func
	memory            *wasmtime.Memory

	logger       *zap.Logger
	config       *InstanceConfig
	epochTicker  *time.Ticker
}

func (i *WasmInstance) ID() string {
	return i.id
}

type InstanceManager struct {
	instances map[string]*WasmInstance
	mu        sync.RWMutex
	engine    *wasmtime.Engine
	logger    *zap.Logger
	cache     *compile.AOTCacheManager
}

func NewInstanceManager(logger *zap.Logger) (*InstanceManager, error) {
	engineConfig := wasmtime.NewConfig()
	if err := engineConfig.SetEpochInterruption(true); err != nil {
		return nil, fmt.Errorf("failed to set epoch interruption: %w", err)
	}

	engine := wasmtime.NewEngineWithConfig(engineConfig)

	cache, err := compile.NewAOTCacheManager("./aot-cache", compile.OptLevel_O2, logger)
	if err != nil {
		logger.Warn("failed to initialize AOT cache manager", zap.Error(err))
	}

	return &InstanceManager{
		instances: make(map[string]*WasmInstance),
		engine:    engine,
		logger:    logger,
		cache:     cache,
	}, nil
}

func (m *InstanceManager) SetCacheManager(cache *compile.AOTCacheManager) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.cache = cache
}

func (m *InstanceManager) GetCacheManager() *compile.AOTCacheManager {
	m.mu.RLock()
	defer m.mu.RUnlock()
	return m.cache
}

func (m *InstanceManager) CreateInstance(ctx context.Context, id string, wasmBytes []byte, envVars map[string]string, stderr io.Writer, cfg *InstanceConfig) (*WasmInstance, error) {
	if cfg == nil {
		cfg = DefaultInstanceConfig()
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	if _, exists := m.instances[id]; exists {
		return nil, fmt.Errorf("instance %s already exists", id)
	}

	startTime := time.Now()
	aotCacheHit := false

	if cfg.UseAOTCache && m.cache != nil && cfg.WasmPath != "" {
		if cachedData, metadata, found, err := m.cache.GetCached(cfg.WasmPath, cfg.ModuleVersion); err == nil && found {
			compatible, reason := compile.VerifyCacheCompatibility(metadata)
			if compatible {
				m.logger.Info("AOT cache hit, will use cached compilation",
					zap.String("instance_id", id),
					zap.String("cache_key", metadata.CompiledHash))
			} else {
				m.logger.Warn("AOT cache incompatible", zap.String("reason", reason))
			}
		}
	}

	compilationTime := time.Since(startTime)
	if aotCacheHit {
		m.logger.Debug("AOT cache hit, skipping compilation",
			zap.String("instance_id", id),
			zap.Duration("time_saved", compilationTime))
	}

	instance := &WasmInstance{
		id:     id,
		logger: m.logger,
		config: cfg,
	}

	engineConfig := wasmtime.NewConfig()
	if err := engineConfig.SetEpochInterruption(true); err != nil {
		return nil, fmt.Errorf("failed to set epoch interruption: %w", err)
	}
	if err := engineConfig.SetStaticMemoryMaximum(cfg.MemoryLimitMB * 1024 * 1024); err != nil {
		return nil, fmt.Errorf("failed to set memory limit: %w", err)
	}
	
	instance.engine = wasmtime.NewEngineWithConfig(engineConfig)
	instance.store = wasmtime.NewStore(instance.engine)
	
	wasiConfig := wasmtime.NewWasiConfig()
	wasiConfig.SetStdoutWritable(io.Discard)
	wasiConfig.SetStderrWritable(stderr)
	wasiConfig.SetEnv(envVars)
	
	instance.store.SetWasi(wasiConfig)
	instance.store.SetEpochDeadline(1)
	
	linker := wasmtime.NewLinker(instance.store)
	instance.linker = linker
	
	if err := linker.DefineWasi(); err != nil {
		return nil, fmt.Errorf("failed to define WASI: %w", err)
	}

	module, err := wasmtime.NewModule(instance.store, wasmBytes)
	if err != nil {
		return nil, fmt.Errorf("failed to compile module: %w", err)
	}
	instance.module = module

	instanceFunc, err := linker.Instantiate(instance.store, module)
	if err != nil {
		return nil, fmt.Errorf("failed to instantiate module: %w", err)
	}
	instance.instance = instanceFunc

	if memory := instanceFunc.GetGlobal(instance.store, "memory"); memory != nil {
		instance.memory = memory.Memory()
	}

	instance.handleRequestFunc = instanceFunc.GetFunc(instance.store, "handle_request")
	instance.healthFunc = instanceFunc.GetFunc(instance.store, "health")

	m.instances[id] = instance
	return instance, nil
}

type HandleRequestResult struct {
	Body        []byte
	StatusCode  int
	Headers     map[string]string
	Error       error
	SlowLog     bool
}

func (i *WasmInstance) HandleRequest(ctx context.Context, method, path, body []byte, headers map[string]string) *HandleRequestResult {
	i.mu.Lock()
	defer i.mu.Unlock()

	if i.handleRequestFunc == nil {
		return &HandleRequestResult{
			StatusCode: 500,
			Error:      fmt.Errorf("handle_request function not found"),
		}
	}

	requestData := buildRequestData(method, path, body, headers)

	timeoutCtx, cancel := context.WithTimeout(ctx, time.Duration(i.config.TimeoutSeconds)*time.Second)
	defer cancel()

	i.store.SetEpochDeadline(1)
	
	epochTicker := time.NewTicker(time.Duration(i.config.TimeoutSeconds) * time.Second / 2)
	defer epochTicker.Stop()
	
	done := make(chan struct{}, 1)
	var result interface{}
	var callErr error

	startTime := time.Now()
	
	go func() {
		defer func() {
			if r := recover(); r != nil {
				callErr = fmt.Errorf("panic during execution: %v", r)
			}
			close(done)
		}()
		
		resultPtr, lenPtr, err := i.handleRequestFunc.Call(i.store, i.store.LuxuryWriteBytes(requestData))
		if err != nil {
			callErr = err
			return
		}
		
		result = []interface{}{resultPtr, lenPtr}
	}()

	select {
	case <-timeoutCtx.Done():
		i.store.IncrementEpoch()
		select {
		case <-done:
		case <-time.After(1 * time.Second):
		}
		
		elapsed := time.Since(startTime)
		i.logger.Warn("request timed out",
			zap.String("instance_id", i.id),
			zap.Duration("elapsed", elapsed))
		
		return &HandleRequestResult{
			StatusCode: 504,
			Error:      fmt.Errorf("request timed out after %v", elapsed),
			SlowLog:    true,
		}
	case <-epochTicker.C:
		i.store.IncrementEpoch()
		epochTicker.Reset(time.Duration(i.config.TimeoutSeconds) * time.Second / 2)
		<-done
	case <-done:
	}

	elapsed := time.Since(startTime)
	
	if callErr != nil {
		i.logger.Error("handle_request failed", zap.Error(callErr))
		return &HandleRequestResult{
			StatusCode: 500,
			Error:      fmt.Errorf("handle_request failed: %w", callErr),
		}
	}

	resultSlice := result.([]interface{})
	resultPtr, resultLen := resultSlice[0], resultSlice[1]
	responseData := i.store.ReadBytes(resultPtr.(int64), int(resultLen.(int64)))
	
	body, status, respHeaders, err := parseResponseData(responseData)
	
	slowLog := elapsed > 5*time.Second
	if slowLog {
		i.logger.Warn("slow request",
			zap.String("instance_id", i.id),
			zap.Duration("elapsed", elapsed),
			zap.Int("status", status))
	}

	return &HandleRequestResult{
		Body:       body,
		StatusCode: status,
		Headers:    respHeaders,
		Error:      err,
		SlowLog:    slowLog,
	}
}

func (i *WasmInstance) Health(ctx context.Context) error {
	i.mu.Lock()
	defer i.mu.Unlock()

	if i.healthFunc == nil {
		return nil
	}

	timeoutCtx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	i.store.SetEpochDeadline(1)

	done := make(chan struct{}, 1)
	var callErr error

	go func() {
		defer func() {
			if r := recover(); r != nil {
				callErr = fmt.Errorf("panic during health check: %v", r)
			}
			close(done)
		}()
		
		_, callErr = i.healthFunc.Call(i.store)
	}()

	select {
	case <-timeoutCtx.Done():
		i.store.IncrementEpoch()
		return fmt.Errorf("health check timed out")
	case <-done:
	}

	return callErr
}

func (i *WasmInstance) Close() {
	i.mu.Lock()
	defer i.mu.Unlock()

	if i.epochTicker != nil {
		i.epochTicker.Stop()
	}

	if i.instance != nil {
		i.instance.Drop(i.store)
	}
	if i.module != nil {
		i.module.Drop(i.store)
	}
}

func (m *InstanceManager) CloseInstance(id string) {
	m.mu.Lock()
	defer m.mu.Unlock()

	if inst, ok := m.instances[id]; ok {
		inst.Close()
		delete(m.instances, id)
	}
}

func (m *InstanceManager) CloseAll() {
	m.mu.Lock()
	defer m.mu.Unlock()

	for id := range m.instances {
		m.instances[id].Close()
	}
	m.instances = make(map[string]*WasmInstance)
}

func buildRequestData(method, path string, body []byte, headers map[string]string) []byte {
	var buf bytes.Buffer

	methodLen := len(method)
	pathLen := len(path)
	bodyLen := len(body)
	headerCount := len(headers)

	buf.WriteByte(byte(methodLen >> 8))
	buf.WriteByte(byte(methodLen & 0xFF))
	buf.Write([]byte(method))

	buf.WriteByte(byte(pathLen >> 8))
	buf.WriteByte(byte(pathLen & 0xFF))
	buf.Write([]byte(path))

	buf.WriteByte(byte(bodyLen >> 24))
	buf.WriteByte(byte((bodyLen >> 16) & 0xFF))
	buf.WriteByte(byte((bodyLen >> 8) & 0xFF))
	buf.WriteByte(byte(bodyLen & 0xFF))
	buf.Write(body)

	buf.WriteByte(byte(headerCount >> 8))
	buf.WriteByte(byte(headerCount & 0xFF))

	for k, v := range headers {
		kLen := len(k)
		vLen := len(v)
		buf.WriteByte(byte(kLen >> 8))
		buf.WriteByte(byte(kLen & 0xFF))
		buf.Write([]byte(k))
		buf.WriteByte(byte(vLen >> 8))
		buf.WriteByte(byte(vLen & 0xFF))
		buf.Write([]byte(v))
	}

	return buf.Bytes()
}

func parseResponseData(data []byte) ([]byte, int, map[string]string, error) {
	if len(data) < 6 {
		return nil, 500, nil, fmt.Errorf("invalid response data")
	}

	status := int(data[0])<<8 | int(data[1])
	headerCount := int(data[2])<<8 | int(data[3])
	bodyLen := int(data[4])<<24 | int(data[5])<<16 | int(data[6])<<8 | int(data[7])

	offset := 8
	headers := make(map[string]string)

	for i := 0; i < headerCount; i++ {
		if offset+4 > len(data) {
			break
		}
		kLen := int(data[offset])<<8 | int(data[offset+1])
		vLen := int(data[offset+2])<<8 | int(data[offset+3])
		offset += 4

		if offset+kLen+vLen > len(data) {
			break
		}
		k := string(data[offset : offset+kLen])
		v := string(data[offset+kLen : offset+kLen+vLen])
		offset += kLen + vLen
		headers[k] = v
	}

	bodyStart := offset
	if bodyStart+bodyLen > len(data) {
		bodyLen = len(data) - bodyStart
	}
	body := data[bodyStart : bodyStart+bodyLen]

	return body, status, headers, nil
}
