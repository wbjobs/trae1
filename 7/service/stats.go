package service

import (
	"sync"
	"time"

	"api-signature/model"
	"api-signature/repository"
	"api-signature/util"

	"github.com/google/uuid"
)

type StatsService struct {
	mu            sync.RWMutex
	abnormalIPs   map[string]*AbnormalIPTracker
}

type AbnormalIPTracker struct {
	IP             string
	ErrorCount     int64
	LastErrorTime  int64
	Blocked        bool
}

var statsServiceInstance *StatsService
var statsServiceOnce sync.Once

func NewStatsService() *StatsService {
	statsServiceOnce.Do(func() {
		statsServiceInstance = &StatsService{
			abnormalIPs: make(map[string]*AbnormalIPTracker),
		}
	})
	return statsServiceInstance
}

func (s *StatsService) RecordError(errorType string, clientID string, ip string) {
	if err := repository.IncrementErrorStats(errorType, clientID, ip); err != nil {
		return
	}

	if ip != "" {
		s.mu.Lock()
		tracker, exists := s.abnormalIPs[ip]
		if !exists {
			tracker = &AbnormalIPTracker{
				IP: ip,
			}
			s.abnormalIPs[ip] = tracker
		}
		tracker.ErrorCount++
		tracker.LastErrorTime = util.GetCurrentTimestamp()

		if tracker.ErrorCount >= 10 && !tracker.Blocked {
			tracker.Blocked = true
			go s.autoBlockIP(ip, errorType)
		}
		s.mu.Unlock()
	}
}

func (s *StatsService) RecordEndpointAccess(path string, clientID string) {
	if err := repository.IncrementEndpointStats(path, clientID); err != nil {
		return
	}
}

func (s *StatsService) GetAbnormalStats() (*model.AbnormalStats, error) {
	stats, err := repository.GetAllStats()
	if err != nil {
		return nil, err
	}

	stats.LastUpdated = util.GetCurrentTimestamp()

	return stats, nil
}

func (s *StatsService) GetErrorStats(errorType string) (map[string]string, error) {
	return repository.GetStatsCounter("errors")
}

func (s *StatsService) GetEndpointStats() (map[string]string, error) {
	return repository.GetStatsCounter("endpoints")
}

func (s *StatsService) autoBlockIP(ip string, reason string) {
	blacklistService := NewBlacklistService()
	duration := 1 * time.Hour

	if err := blacklistService.AddIP(ip, reason, duration); err != nil {
		return
	}

	alert := &model.SecurityAlert{
		ID:          uuid.New().String(),
		Type:        "AUTO_BLOCK",
		Severity:    "HIGH",
		IP:          ip,
		Description: "Auto-blocked due to repeated errors: " + reason,
		Timestamp:   util.GetCurrentTimestamp(),
		Resolved:    false,
	}

	_ = repository.SaveSecurityAlert(alert)
}

func (s *StatsService) GetAbnormalIPs() []map[string]interface{} {
	s.mu.RLock()
	defer s.mu.RUnlock()

	var result []map[string]interface{}
	for ip, tracker := range s.abnormalIPs {
		if tracker.ErrorCount >= 5 {
			result = append(result, map[string]interface{}{
				"ip":              ip,
				"error_count":     tracker.ErrorCount,
				"last_error_time": tracker.LastErrorTime,
				"blocked":         tracker.Blocked,
			})
		}
	}
	return result
}

func (s *StatsService) ResetIPTracker(ip string) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if tracker, exists := s.abnormalIPs[ip]; exists {
		tracker.ErrorCount = 0
		tracker.Blocked = false
	}
}

func (s *StatsService) CreateSecurityAlert(alertType string, severity string, clientID string, ip string, description string) *model.SecurityAlert {
	alert := &model.SecurityAlert{
		ID:          uuid.New().String(),
		Type:        alertType,
		Severity:    severity,
		ClientID:    clientID,
		IP:          ip,
		Description: description,
		Timestamp:   util.GetCurrentTimestamp(),
		Resolved:    false,
	}

	_ = repository.SaveSecurityAlert(alert)

	return alert
}

func (s *StatsService) GetSecurityAlerts(limit int64) ([]model.SecurityAlert, error) {
	return repository.GetSecurityAlerts(limit)
}
