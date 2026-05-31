package risk

import (
	"encoding/json"
	"sync"
	"time"
)

type UserHistory struct {
	mu sync.RWMutex

	Username         string    `json:"username"`
	LastIP           string    `json:"lastIp"`
	LastCountry      string    `json:"lastCountry"`
	LastRegion       string    `json:"lastRegion"`
	LastCity         string    `json:"lastCity"`
	LastTimezone     string    `json:"lastTimezone"`
	LastDevice       string    `json:"lastDevice"`
	LastOS           string    `json:"lastOs"`
	LastBrowser      string    `json:"lastBrowser"`
	LastHour         int       `json:"lastHour"`
	LastLoginTime    time.Time `json:"lastLoginTime"`
	DaysSinceLastLogin int     `json:"daysSinceLastLogin"`
	LoginFrequency   float64   `json:"loginFrequency"`
	FailedAttempts   int       `json:"failedAttempts"`
	TotalLogins      int       `json:"totalLogins"`
	FirstLoginTime   time.Time `json:"firstLoginTime"`

	IPHistory        []string  `json:"ipHistory"`
	DeviceHistory    []string  `json:"deviceHistory"`
	LoginHistory     []time.Time `json:"loginHistory"`

	RiskThresholds   map[RiskLevel]float64 `json:"riskThresholds"`
}

func NewUserHistory(username string) *UserHistory {
	return &UserHistory{
		Username:       username,
		FirstLoginTime: time.Now(),
		IPHistory:      make([]string, 0, 50),
		DeviceHistory:  make([]string, 0, 50),
		LoginHistory:   make([]time.Time, 0, 100),
		RiskThresholds: map[RiskLevel]float64{
			RiskLow:      30,
			RiskMedium:   60,
			RiskHigh:     85,
			RiskCritical: 100,
		},
	}
}

func (h *UserHistory) Update(ctx *ContextInfo, success bool) {
	h.mu.Lock()
	defer h.mu.Unlock()

	now := time.Now()

	if success {
		h.LastIP = ctx.IPAddress
		h.LastCountry = ctx.Country
		h.LastRegion = ctx.Region
		h.LastCity = ctx.City
		h.LastTimezone = ctx.Timezone
		h.LastDevice = ctx.DeviceFingerprint
		h.LastOS = ctx.OS
		h.LastBrowser = ctx.Browser
		h.LastHour = ctx.HourOfDay
		h.LastLoginTime = now
		h.TotalLogins++

		if !h.FirstLoginTime.IsZero() {
			h.DaysSinceLastLogin = int(now.Sub(h.LastLoginTime).Hours() / 24)
		}

		if len(h.IPHistory) >= 50 {
			h.IPHistory = h.IPHistory[1:]
		}
		h.IPHistory = append(h.IPHistory, ctx.IPAddress)

		if len(h.DeviceHistory) >= 50 {
			h.DeviceHistory = h.DeviceHistory[1:]
		}
		h.DeviceHistory = append(h.DeviceHistory, ctx.DeviceFingerprint)

		if len(h.LoginHistory) >= 100 {
			h.LoginHistory = h.LoginHistory[1:]
		}
		h.LoginHistory = append(h.LoginHistory, now)

		h.updateLoginFrequency()
		h.FailedAttempts = 0
	} else {
		h.FailedAttempts++
	}
}

func (h *UserHistory) updateLoginFrequency() {
	if len(h.LoginHistory) < 2 {
		h.LoginFrequency = 1.0
		return
	}

	now := time.Now()
	recentLogins := 0
	for _, t := range h.LoginHistory {
		if now.Sub(t).Hours() < 168 {
			recentLogins++
		}
	}

	h.LoginFrequency = float64(recentLogins) / 7.0
}

func (h *UserHistory) IsNewUser() bool {
	return h.TotalLogins < 5
}

func (h *UserHistory) HasUnusualLocation(ip, country string) bool {
	h.mu.RLock()
	defer h.mu.RUnlock()

	for _, histIP := range h.IPHistory {
		if histIP == ip {
			return false
		}
	}

	if country != "" && country == h.LastCountry {
		return false
	}

	return len(h.IPHistory) > 0
}

func (h *UserHistory) HasUnusualDevice(fingerprint string) bool {
	h.mu.RLock()
	defer h.mu.RUnlock()

	for _, dev := range h.DeviceHistory {
		if dev == fingerprint {
			return false
		}
	}

	return len(h.DeviceHistory) > 0
}

func (h *UserHistory) ToJSON() string {
	h.mu.RLock()
	defer h.mu.RUnlock()
	data, _ := json.Marshal(h)
	return string(data)
}

type HistoryStore struct {
	mu       sync.RWMutex
	histories map[string]*UserHistory
}

func NewHistoryStore() *HistoryStore {
	return &HistoryStore{
		histories: make(map[string]*UserHistory),
	}
}

func (s *HistoryStore) GetOrCreate(username string) *UserHistory {
	s.mu.Lock()
	defer s.mu.Unlock()

	history, exists := s.histories[username]
	if !exists {
		history = NewUserHistory(username)
		s.histories[username] = history
	}
	return history
}

func (s *HistoryStore) Get(username string) (*UserHistory, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	history, exists := s.histories[username]
	return history, exists
}

func (s *HistoryStore) Update(username string, ctx *ContextInfo, success bool) {
	s.mu.Lock()
	defer s.mu.Unlock()

	history, exists := s.histories[username]
	if !exists {
		history = NewUserHistory(username)
		s.histories[username] = history
	}
	history.Update(ctx, success)
}

func (s *HistoryStore) GetAll() []*UserHistory {
	s.mu.RLock()
	defer s.mu.RUnlock()

	result := make([]*UserHistory, 0, len(s.histories))
	for _, h := range s.histories {
		result = append(result, h)
	}
	return result
}

func (s *HistoryStore) GenerateTrainingData() []DataPoint {
	s.mu.RLock()
	defer s.mu.RUnlock()

	var data []DataPoint

	for _, history := range s.histories {
		history.mu.RLock()
		for _, loginTime := range history.LoginHistory {
			features := []float64{
				hourToRiskScore(loginTime.Hour()),
				float64(boolToInt(loginTime.Weekday() == time.Saturday || loginTime.Weekday() == time.Sunday)),
				float64(len(history.IPHistory)) / 50.0,
				float64(len(history.DeviceHistory)) / 50.0,
				history.LoginFrequency / 10.0,
				float64(history.FailedAttempts) / 10.0,
			}
			data = append(data, DataPoint{
				Features: features,
				Label:    history.Username,
			})
		}
		history.mu.RUnlock()
	}

	return data
}

func boolToInt(b bool) int {
	if b {
		return 1
	}
	return 0
}
