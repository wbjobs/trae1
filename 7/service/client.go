package service

import (
	"fmt"
	"sync"
	"time"

	"api-signature/config"
	"api-signature/model"
	"api-signature/repository"
)

type ClientService struct {
	mu      sync.RWMutex
	clients map[string]*model.ClientInfo
}

var clientServiceInstance *ClientService
var clientServiceOnce sync.Once

func NewClientService() *ClientService {
	clientServiceOnce.Do(func() {
		clientServiceInstance = &ClientService{
			clients: make(map[string]*model.ClientInfo),
		}
		clientServiceInstance.loadClients()
	})
	return clientServiceInstance
}

func (s *ClientService) loadClients() {
	for _, cfg := range config.AppConfig.Security.AllowedClients {
		s.clients[cfg.ClientID] = &model.ClientInfo{
			ClientID:       cfg.ClientID,
			ClientSecret:   cfg.ClientSecret,
			SecretVersion:  1,
			SecretHistory:  make([]model.SecretVersion, 0),
			Permissions:    cfg.Permissions,
			RateLimit:      cfg.RateLimit,
			EndpointLimits: make(map[string]int),
			Enabled:        true,
		}
	}
}

func (s *ClientService) GetClient(clientID string) (*model.ClientInfo, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	client, exists := s.clients[clientID]
	if !exists {
		return nil, fmt.Errorf("client not found: %s", clientID)
	}

	if !client.Enabled {
		return nil, fmt.Errorf("client is disabled: %s", clientID)
	}

	return client, nil
}

func (s *ClientService) GetAllClients() []*model.ClientInfo {
	s.mu.RLock()
	defer s.mu.RUnlock()

	var clients []*model.ClientInfo
	for _, client := range s.clients {
		clients = append(clients, client)
	}
	return clients
}

func (s *ClientService) AddClient(clientID string, clientSecret string, permissions []string, rateLimit int) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if _, exists := s.clients[clientID]; exists {
		return fmt.Errorf("client already exists: %s", clientID)
	}

	s.clients[clientID] = &model.ClientInfo{
		ClientID:       clientID,
		ClientSecret:   clientSecret,
		SecretVersion:  1,
		SecretHistory:  make([]model.SecretVersion, 0),
		Permissions:    permissions,
		RateLimit:      rateLimit,
		EndpointLimits: make(map[string]int),
		Enabled:        true,
	}

	return nil
}

func (s *ClientService) UpdateClient(clientID string, permissions []string, rateLimit int) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	client, exists := s.clients[clientID]
	if !exists {
		return fmt.Errorf("client not found: %s", clientID)
	}

	if permissions != nil {
		client.Permissions = permissions
	}
	if rateLimit > 0 {
		client.RateLimit = rateLimit
	}

	return nil
}

func (s *ClientService) UpdateClientSecret(clientID string, newSecret string, version int) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	client, exists := s.clients[clientID]
	if !exists {
		return fmt.Errorf("client not found: %s", clientID)
	}

	client.ClientSecret = newSecret
	client.SecretVersion = version

	return nil
}

func (s *ClientService) SetEndpointLimit(clientID string, path string, limit int) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	client, exists := s.clients[clientID]
	if !exists {
		return fmt.Errorf("client not found: %s", clientID)
	}

	if client.EndpointLimits == nil {
		client.EndpointLimits = make(map[string]int)
	}

	client.EndpointLimits[path] = limit
	return nil
}

func (s *ClientService) GetEndpointLimit(clientID string, path string) (int, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	client, exists := s.clients[clientID]
	if !exists {
		return 0, fmt.Errorf("client not found: %s", clientID)
	}

	if limit, ok := client.EndpointLimits[path]; ok {
		return limit, nil
	}

	return client.RateLimit, nil
}

func (s *ClientService) DeleteClient(clientID string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if _, exists := s.clients[clientID]; !exists {
		return fmt.Errorf("client not found: %s", clientID)
	}

	delete(s.clients, clientID)
	return nil
}

func (s *ClientService) SetClientStatus(clientID string, enabled bool) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	client, exists := s.clients[clientID]
	if !exists {
		return fmt.Errorf("client not found: %s", clientID)
	}

	client.Enabled = enabled
	return nil
}

func (s *ClientService) CheckRateLimit(clientID string) (bool, int64, error) {
	client, err := s.GetClient(clientID)
	if err != nil {
		return false, 0, err
	}

	window := 1 * time.Minute
	count, err := repository.IncrementRateLimit(clientID, window)
	if err != nil {
		return false, 0, fmt.Errorf("failed to check rate limit: %w", err)
	}

	if count > int64(client.RateLimit) {
		return false, count, nil
	}

	return true, count, nil
}

func (s *ClientService) CheckEndpointRateLimit(clientID string, path string) (bool, int64, error) {
	limit, err := s.GetEndpointLimit(clientID, path)
	if err != nil {
		return false, 0, err
	}

	window := 1 * time.Minute
	count, err := repository.IncrementEndpointRateLimit(clientID, path, window)
	if err != nil {
		return false, 0, fmt.Errorf("failed to check endpoint rate limit: %w", err)
	}

	if count > int64(limit) {
		return false, count, nil
	}

	return true, count, nil
}

func (s *ClientService) GetRateLimitCount(clientID string) (int64, error) {
	return repository.GetRateLimitCount(clientID)
}

func (s *ClientService) GetEndpointRateLimitCount(clientID string, path string) (int64, error) {
	return repository.GetEndpointRateLimitCount(clientID, path)
}

func (s *ClientService) GetRateLimitInfo(clientID string) (*model.RateLimitInfo, error) {
	client, err := s.GetClient(clientID)
	if err != nil {
		return nil, err
	}

	globalCount, _ := repository.GetRateLimitCount(clientID)
	resetTime, _ := repository.GetRateLimitResetTime(clientID)

	info := &model.RateLimitInfo{
		ClientID:       clientID,
		GlobalLimit:    client.RateLimit,
		GlobalCount:    globalCount,
		GlobalReset:    resetTime,
		EndpointLimits: make(map[string]model.EndpointLimit),
	}

	for path, limit := range client.EndpointLimits {
		count, _ := repository.GetEndpointRateLimitCount(clientID, path)
		info.EndpointLimits[path] = model.EndpointLimit{
			Path:  path,
			Limit: limit,
			Count: count,
		}
	}

	return info, nil
}

