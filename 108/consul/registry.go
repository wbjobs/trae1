package consul

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/consul/consul/api"
	"github.com/wasi-service/runtime/config"
	"go.uber.org/zap"
)

type ServiceRegistry struct {
	client      *api.Client
	logger      *zap.Logger
	services    map[string]*api.AgentServiceRegistration
	healthFuncs map[string]func(context.Context) error
	mu          sync.RWMutex
	enabled     bool
}

func NewServiceRegistry(consulAddr string, logger *zap.Logger) (*ServiceRegistry, error) {
	cfg := api.DefaultConfig()
	cfg.Address = consulAddr

	client, err := api.NewClient(cfg)
	if err != nil {
		return nil, fmt.Errorf("failed to create consul client: %w", err)
	}

	return &ServiceRegistry{
		client:      client,
		logger:      logger,
		services:    make(map[string]*api.AgentServiceRegistration),
		healthFuncs: make(map[string]func(context.Context) error),
		enabled:     true,
	}, nil
}

func (sr *ServiceRegistry) SetEnabled(enabled bool) {
	sr.mu.Lock()
	defer sr.mu.Unlock()
	sr.enabled = enabled
}

func (sr *ServiceRegistry) RegisterService(ctx context.Context, svcCfg *config.ServiceConfig, healthFunc func(context.Context) error) error {
	sr.mu.Lock()
	defer sr.mu.Unlock()

	if !sr.enabled {
		sr.logger.Info("consul disabled, skipping registration")
		return nil
	}

	registration := &api.AgentServiceRegistration{
		ID:      svcCfg.Name,
		Name:    svcCfg.Consul.Service,
		Port:    svcCfg.Port,
		Address: "localhost",
		Meta: map[string]string{
			"module":      svcCfg.Module,
			"instances":   fmt.Sprintf("%d", svcCfg.Instances),
		},
		Checks: []*api.AgentServiceCheck{
			{
				Interval:                       svcCfg.Consul.CheckInterval,
				DeregisterCriticalServiceAfter: "30s",
				HTTP:                            fmt.Sprintf("http://localhost:%d%s", svcCfg.Port, svcCfg.HealthCheckPath),
				Method:                          "GET",
			},
		},
	}

	if err := sr.client.Agent().ServiceRegister(registration); err != nil {
		return fmt.Errorf("failed to register service: %w", err)
	}

	sr.services[svcCfg.Name] = registration
	sr.healthFuncs[svcCfg.Name] = healthFunc

	sr.logger.Info("service registered to consul",
		zap.String("service", svcCfg.Name),
		zap.String("consul_service", svcCfg.Consul.Service),
		zap.Int("port", svcCfg.Port),
	)

	return nil
}

func (sr *ServiceRegistry) DeregisterService(ctx context.Context, serviceID string) error {
	sr.mu.Lock()
	defer sr.mu.Unlock()

	if !sr.enabled {
		return nil
	}

	if err := sr.client.Agent().ServiceDeregister(serviceID); err != nil {
		return fmt.Errorf("failed to deregister service: %w", err)
	}

	delete(sr.services, serviceID)
	delete(sr.healthFuncs, serviceID)

	sr.logger.Info("service deregistered from consul", zap.String("service", serviceID))
	return nil
}

func (sr *ServiceRegistry) PerformHealthCheck(ctx context.Context, serviceID string) error {
	sr.mu.RLock()
	healthFunc := sr.healthFuncs[serviceID]
	sr.mu.RUnlock()

	if healthFunc == nil {
		return nil
	}

	return healthFunc(ctx)
}

func (sr *ServiceRegistry) GetService(ctx context.Context, serviceName string) ([]*api.ServiceEntry, error) {
	if !sr.enabled {
		return nil, fmt.Errorf("consul is disabled")
	}

	services, _, err := sr.client.Health().Service(serviceName, "", true, nil)
	if err != nil {
		return nil, fmt.Errorf("failed to get service from consul: %w", err)
	}

	return services, nil
}

func (sr *ServiceRegistry) GetAllServices(ctx context.Context) (map[string][]*api.ServiceEntry, error) {
	if !sr.enabled {
		return nil, fmt.Errorf("consul is disabled")
	}

	allServices := make(map[string][]*api.ServiceEntry)

	services, _, err := sr.client.Catalog().Services(nil)
	if err != nil {
		return nil, fmt.Errorf("failed to get services from consul: %w", err)
	}

	for serviceName := range services {
		entries, _, err := sr.client.Health().Service(serviceName, "", true, nil)
		if err != nil {
			continue
		}
		if len(entries) > 0 {
			allServices[serviceName] = entries
		}
	}

	return allServices, nil
}

type HealthChecker struct {
	registry  *ServiceRegistry
	interval  time.Duration
	logger    *zap.Logger
	stopCh    chan struct{}
	wg        sync.WaitGroup
}

func NewHealthChecker(registry *ServiceRegistry, interval time.Duration, logger *zap.Logger) *HealthChecker {
	return &HealthChecker{
		registry: registry,
		interval: interval,
		logger:   logger,
		stopCh:   make(chan struct{}),
	}
}

func (hc *HealthChecker) Start(ctx context.Context) {
	hc.wg.Add(1)
	go func() {
		defer hc.wg.Done()
		ticker := time.NewTicker(hc.interval)
		defer ticker.Stop()

		for {
			select {
			case <-ticker.C:
				hc.check(ctx)
			case <-hc.stopCh:
				return
			case <-ctx.Done():
				return
			}
		}
	}()
}

func (hc *HealthChecker) Stop() {
	close(hc.stopCh)
	hc.wg.Wait()
}

func (hc *HealthChecker) check(ctx context.Context) {
	hc.registry.mu.RLock()
	serviceIDs := make([]string, 0, len(hc.registry.healthFuncs))
	for id := range hc.registry.healthFuncs {
		serviceIDs = append(serviceIDs, id)
	}
	hc.registry.mu.RUnlock()

	for _, id := range serviceIDs {
		if err := hc.registry.PerformHealthCheck(ctx, id); err != nil {
			hc.logger.Warn("health check failed",
				zap.String("service_id", id),
				zap.Error(err),
			)
		}
	}
}

type ServiceDiscovery struct {
	registry    *ServiceRegistry
	logger      *zap.Logger
	serviceName string
	instances   map[string]string
	mu          sync.RWMutex
}

func NewServiceDiscovery(registry *ServiceRegistry, serviceName string, logger *zap.Logger) *ServiceDiscovery {
	return &ServiceDiscovery{
		registry:    registry,
		logger:      logger,
		serviceName: serviceName,
		instances:   make(map[string]string),
	}
}

func (sd *ServiceDiscovery) AddInstance(instanceID, address string) {
	sd.mu.Lock()
	defer sd.mu.Unlock()
	sd.instances[instanceID] = address
}

func (sd *ServiceDiscovery) RemoveInstance(instanceID string) {
	sd.mu.Lock()
	defer sd.mu.Unlock()
	delete(sd.instances, instanceID)
}

func (sd *ServiceDiscovery) GetInstances() map[string]string {
	sd.mu.RLock()
	defer sd.mu.RUnlock()
	result := make(map[string]string)
	for k, v := range sd.instances {
		result[k] = v
	}
	return result
}

func (sd *ServiceDiscovery) Discover(ctx context.Context) ([]string, error) {
	entries, err := sd.registry.GetService(ctx, sd.serviceName)
	if err != nil {
		return nil, err
	}

	addrs := make([]string, 0, len(entries))
	for _, entry := range entries {
		if entry.Service.Address != "" {
			addrs = append(addrs, fmt.Sprintf("%s:%d", entry.Service.Address, entry.Service.Port))
		}
	}

	return addrs, nil
}