package runtime

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"os"
	"sync"
	"sync/atomic"

	"github.com/wasi-service/runtime/config"
	"github.com/wasi-service/runtime/consul"
	"github.com/wasi-service/runtime/http"
	"github.com/wasi-service/runtime/log"
	"github.com/wasi-service/runtime/wasi"
	"go.uber.org/zap"
)

type Service struct {
	config       *config.ServiceConfig
	instances    []*wasi.WasmInstance
	loadBalancer *http.LoadBalancer
	httpServer   *http.HTTPServer
	registry     *consul.ServiceRegistry
	logCollector *log.LogCollector
	slowLogger   *log.SlowLogger
	stopCh       chan struct{}
	wg           sync.WaitGroup
	mu           sync.RWMutex
	running      atomic.Bool
}

type Runtime struct {
	configManager   *config.ConfigManager
	instanceManager *wasi.InstanceManager
	registry        *consul.ServiceRegistry
	services        map[string]*Service
	logger          *zap.Logger
	mu              sync.RWMutex
	limit           int
}

func NewRuntime(configManager *config.ConfigManager, consulAddr string, limit int, logger *zap.Logger) (*Runtime, error) {
	instanceManager, err := wasi.NewInstanceManager(logger)
	if err != nil {
		return nil, fmt.Errorf("failed to create instance manager: %w", err)
	}

	registry, err := consul.NewServiceRegistry(consulAddr, logger)
	if err != nil {
		logger.Warn("failed to connect to consul, disabling service discovery", zap.Error(err))
		registry = nil
	}

	return &Runtime{
		configManager:   configManager,
		instanceManager: instanceManager,
		registry:        registry,
		services:        make(map[string]*Service),
		logger:          logger,
		limit:           limit,
	}, nil
}

func (r *Runtime) StartServiceWithParams(ctx context.Context, name, module string, port, instances int, envVars map[string]string) error {
	svcCfg := &config.ServiceConfig{
		Name:      name,
		Module:    module,
		Port:      port,
		Instances: instances,
		EnvVars:   envVars,
		Consul: config.ConsulConfig{
			Enabled:       false,
			CheckInterval: "10s",
		},
	}
	return r.StartService(ctx, svcCfg)
}

func (r *Runtime) StartService(ctx context.Context, svcCfg *config.ServiceConfig) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	if _, exists := r.services[svcCfg.Name]; exists {
		return fmt.Errorf("service %s already running", svcCfg.Name)
	}

	wasmBytes, err := os.ReadFile(svcCfg.Module)
	if err != nil {
		return fmt.Errorf("failed to read wasm module: %w", err)
	}

	service := &Service{
		config:       svcCfg,
		instances:    make([]*wasi.WasmInstance, 0, svcCfg.Instances),
		loadBalancer: http.NewLoadBalancer(r.logger),
		logCollector: log.NewLogCollector(svcCfg.Name, r.logger),
		stopCh:       make(chan struct{}),
	}

	slowLogger, err := log.NewSlowLogger(fmt.Sprintf("%s-slow.log", svcCfg.Name), 1000, r.logger)
	if err != nil {
		r.logger.Warn("failed to create slow logger", zap.Error(err))
	} else {
		service.slowLogger = slowLogger
	}

	envVars := config.PrepareEnvVars(svcCfg.EnvVars, os.Environ())

	instanceConfig := &wasi.InstanceConfig{
		MemoryLimitMB:   svcCfg.MemoryLimitMB,
		MaxInstructions: svcCfg.MaxInstructions,
		TimeoutSeconds:  svcCfg.TimeoutSeconds,
		ModuleVersion:   svcCfg.ModuleVersion,
		UseAOTCache:    svcCfg.UseAOTCache,
		CacheDir:       r.configManager.Get().DefaultAOTCacheDir,
		WasmPath:       svcCfg.Module,
	}

	for i := 0; i < svcCfg.Instances; i++ {
		instanceID := fmt.Sprintf("%s-%d", svcCfg.Name, i)

		stderrWriter := io.MultiWriter(service.logCollector, os.Stderr)

		instance, err := r.instanceManager.CreateInstance(ctx, instanceID, wasmBytes, envVars, stderrWriter, instanceConfig)
		if err != nil {
			r.cleanupInstances(service)
			return fmt.Errorf("failed to create instance %s: %w", instanceID, err)
		}

		service.instances = append(service.instances, instance)
		service.loadBalancer.AddInstance(instance)
	}

	handler := http.NewHandler(r.instanceManager, svcCfg.Name, r.logger)
	if len(service.instances) > 0 {
		handler.SetInstance(service.instances[0])
	}

	service.httpServer = http.NewHTTPServer(
		fmt.Sprintf(":%d", svcCfg.Port),
		handler,
		r.logger,
	)

	r.services[svcCfg.Name] = service

	if r.registry != nil && svcCfg.Consul.Enabled {
		healthFunc := func(ctx context.Context) error {
			for _, inst := range service.instances {
				if err := inst.Health(ctx); err != nil {
					return err
				}
			}
			return nil
		}

		if err := r.registry.RegisterService(ctx, svcCfg, healthFunc); err != nil {
			r.logger.Warn("failed to register service to consul", zap.Error(err))
		}
	}

	service.running.Store(true)
	r.wg.Add(1)
	go func() {
		defer r.wg.Done()
		if err := service.httpServer.Start(); err != nil && err != http.ErrServerClosed {
			r.logger.Error("HTTP server error", zap.Error(err))
		}
	}()

	r.logger.Info("service started",
		zap.String("service", svcCfg.Name),
		zap.Int("instances", svcCfg.Instances),
		zap.Int("port", svcCfg.Port),
	)

	return nil
}

func (r *Runtime) StopService(ctx context.Context, serviceName string) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	service, exists := r.services[serviceName]
	if !exists {
		return fmt.Errorf("service %s not found", serviceName)
	}

	service.running.Store(false)
	close(service.stopCh)

	if err := service.httpServer.Stop(ctx); err != nil {
		r.logger.Error("failed to stop HTTP server", zap.Error(err))
	}

	for _, inst := range service.instances {
		r.instanceManager.CloseInstance(inst.ID())
	}

	if r.registry != nil {
		if err := r.registry.DeregisterService(ctx, serviceName); err != nil {
			r.logger.Warn("failed to deregister service from consul", zap.Error(err))
		}
	}

	delete(r.services, serviceName)

	r.logger.Info("service stopped", zap.String("service", serviceName))
	return nil
}

func (r *Runtime) StopAll(ctx context.Context) {
	r.mu.Lock()
	defer r.mu.Unlock()

	for name := range r.services {
		service := r.services[name]
		service.running.Store(false)
		close(service.stopCh)

		service.httpServer.Stop(ctx)

		for _, inst := range service.instances {
			r.instanceManager.CloseInstance(inst.ID())
		}

		if r.registry != nil {
			r.registry.DeregisterService(ctx, name)
		}
	}

	r.instanceManager.CloseAll()
	r.services = make(map[string]*Service)
}

func (r *Runtime) GetService(name string) (*Service, bool) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	svc, exists := r.services[name]
	return svc, exists
}

func (r *Runtime) ListServices() []string {
	r.mu.RLock()
	defer r.mu.RUnlock()
	names := make([]string, 0, len(r.services))
	for name := range r.services {
		names = append(names, name)
	}
	return names
}

func (r *Runtime) cleanupInstances(service *Service) {
	for _, inst := range service.instances {
		if inst != nil {
			inst.Close()
		}
	}
}

func (s *Service) GetInstances() []*wasi.WasmInstance {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.instances
}

func (s *Service) IsRunning() bool {
	return s.running.Load()
}

func (s *Service) GetConfig() *config.ServiceConfig {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.config
}

func (s *Service) GetLogs() string {
	return s.logCollector.GetLogs()
}

type InstancePool struct {
	pool      chan *wasi.WasmInstance
	available map[string]*wasi.WasmInstance
	inUse     map[string]*wasi.WasmInstance
	mu        sync.RWMutex
	logger    *zap.Logger
}

func NewInstancePool(size int, logger *zap.Logger) *InstancePool {
	return &InstancePool{
		pool:      make(chan *wasi.WasmInstance, size),
		available: make(map[string]*wasi.WasmInstance),
		inUse:     make(map[string]*wasi.WasmInstance),
		logger:    logger,
	}
}

func (p *InstancePool) Add(instance *wasi.WasmInstance) {
	p.mu.Lock()
	defer p.mu.Unlock()

	p.available[instance.ID()] = instance
	p.pool <- instance
}

func (p *InstancePool) Acquire(ctx context.Context) (*wasi.WasmInstance, error) {
	select {
	case inst := <-p.pool:
		p.mu.Lock()
		id := inst.ID()
		p.inUse[id] = inst
		delete(p.available, id)
		p.mu.Unlock()
		return inst, nil
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

func (p *InstancePool) Release(instance *wasi.WasmInstance) {
	p.mu.Lock()
	defer p.mu.Unlock()

	id := instance.ID()
	delete(p.inUse, id)
	p.available[id] = instance
	p.pool <- instance
}

func (p *InstancePool) Size() int {
	p.mu.RLock()
	defer p.mu.RUnlock()
	return len(p.available) + len(p.inUse)
}

func (p *InstancePool) Available() int {
	p.mu.RLock()
	defer p.mu.RUnlock()
	return len(p.available)
}

type WasmRequest struct {
	Method  string
	Path    string
	Body    []byte
	Headers map[string]string
}

type WasmResponse struct {
	StatusCode int
	Body       []byte
	Headers    map[string]string
}

func EncodeRequest(req *WasmRequest) []byte {
	var buf bytes.Buffer

	methodLen := len(req.Method)
	pathLen := len(req.Path)
	bodyLen := len(req.Body)
	headerCount := len(req.Headers)

	buf.WriteByte(byte(methodLen >> 8))
	buf.WriteByte(byte(methodLen & 0xFF))
	buf.Write([]byte(req.Method))

	buf.WriteByte(byte(pathLen >> 8))
	buf.WriteByte(byte(pathLen & 0xFF))
	buf.Write([]byte(req.Path))

	buf.WriteByte(byte(bodyLen >> 24))
	buf.WriteByte(byte((bodyLen >> 16) & 0xFF))
	buf.WriteByte(byte((bodyLen >> 8) & 0xFF))
	buf.WriteByte(byte(bodyLen & 0xFF))
	buf.Write(req.Body)

	buf.WriteByte(byte(headerCount >> 8))
	buf.WriteByte(byte(headerCount & 0xFF))

	for k, v := range req.Headers {
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

func DecodeResponse(data []byte) (*WasmResponse, error) {
	if len(data) < 6 {
		return nil, fmt.Errorf("invalid response data")
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

	return &WasmResponse{
		StatusCode: status,
		Body:       body,
		Headers:    headers,
	}, nil
}