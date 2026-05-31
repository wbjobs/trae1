package ml

import (
	"math"
	"sync"
	"time"

	"iprep-sync/internal/config"
	"iprep-sync/internal/model"
)

type pathBucket struct {
	Count int
}

type ipStats struct {
	TotalRequests   int
	FailedRequests  int
	FirstSeen       time.Time
	LastSeen        time.Time
	HourlyCounts    [24]int
	PathCategories  map[string]int
}

type FeatureCollector struct {
	mu      sync.RWMutex
	stats   map[string]*ipStats
	window  time.Duration
	cfg     config.ML
}

func NewFeatureCollector(cfg config.ML) *FeatureCollector {
	return &FeatureCollector{
		stats:  make(map[string]*ipStats),
		window: time.Duration(cfg.FeatureWindow) * time.Second,
		cfg:    cfg,
	}
}

func (fc *FeatureCollector) Record(ip, path string, failed bool) {
	fc.mu.Lock()
	defer fc.mu.Unlock()

	now := time.Now().UTC()
	s, ok := fc.stats[ip]
	if !ok {
		s = &ipStats{
			FirstSeen:      now,
			PathCategories: make(map[string]int),
		}
		fc.stats[ip] = s
	}

	if !s.LastSeen.IsZero() && now.Sub(s.LastSeen) > fc.window {
		s.TotalRequests = 0
		s.FailedRequests = 0
		s.FirstSeen = now
		s.HourlyCounts = [24]int{}
		s.PathCategories = make(map[string]int)
	}

	s.TotalRequests++
	if failed {
		s.FailedRequests++
	}
	s.LastSeen = now
	s.HourlyCounts[now.Hour()]++

	cat := categorizePath(path, fc.cfg.PathCategories)
	s.PathCategories[cat]++
}

func (fc *FeatureCollector) ExtractFeatures(ip string) (model.BehaviorFeatures, bool) {
	fc.mu.RLock()
	defer fc.mu.RUnlock()

	s, ok := fc.stats[ip]
	if !ok || s.TotalRequests == 0 {
		return model.BehaviorFeatures{}, false
	}

	now := time.Now().UTC()
	if !s.LastSeen.IsZero() && now.Sub(s.LastSeen) > fc.window {
		return model.BehaviorFeatures{}, false
	}

	elapsed := now.Sub(s.FirstSeen).Seconds()
	if elapsed < 1 {
		elapsed = 1
	}

	freq := float64(s.TotalRequests) / elapsed
	failRate := 0.0
	if s.TotalRequests > 0 {
		failRate = float64(s.FailedRequests) / float64(s.TotalRequests)
	}

	entropy := pathEntropy(s.PathCategories)
	uniquePaths := len(s.PathCategories)

	var daytime, nighttime int
	for h := 6; h < 18; h++ {
		daytime += s.HourlyCounts[h]
	}
	for h := 0; h < 6; h++ {
		nighttime += s.HourlyCounts[h]
	}
	for h := 18; h < 24; h++ {
		nighttime += s.HourlyCounts[h]
	}

	totalHours := 0
	for _, c := range s.HourlyCounts {
		totalHours += c
	}
	dayRatio := 0.0
	nightRatio := 0.0
	if totalHours > 0 {
		dayRatio = float64(daytime) / float64(totalHours)
		nightRatio = float64(nighttime) / float64(totalHours)
	}

	peakHour := 0
	peakCount := -1
	for h, c := range s.HourlyCounts {
		if c > peakCount {
			peakCount = c
			peakHour = h
		}
	}

	return model.BehaviorFeatures{
		RequestCount:   s.TotalRequests,
		RequestFreq:    freq,
		FailureRate:    failRate,
		PathEntropy:    entropy,
		UniquePaths:    uniquePaths,
		DaytimeRatio:   dayRatio,
		NighttimeRatio: nightRatio,
		PeakHour:       peakHour,
		LastSeen:       s.LastSeen,
	}, true
}

func categorizePath(path string, numCats int) string {
	parts := splitPath(path)
	if len(parts) == 0 {
		return "/"
	}
	top := parts[0]
	if numCats <= 0 {
		return top
	}
	if len(top) > 0 {
		hash := int(top[0]) % numCats
		return "cat_" + intToStr(hash)
	}
	return "/"
}

func splitPath(path string) []string {
	var parts []string
	start := 0
	for i := 0; i < len(path); i++ {
		if path[i] == '/' {
			if i > start {
				parts = append(parts, path[start:i])
			}
			start = i + 1
		}
	}
	if start < len(path) {
		parts = append(parts, path[start:])
	}
	return parts
}

func intToStr(n int) string {
	if n == 0 {
		return "0"
	}
	digits := []byte{}
	for n > 0 {
		digits = append([]byte{byte('0' + n%10)}, digits...)
		n /= 10
	}
	return string(digits)
}

func pathEntropy(cats map[string]int) float64 {
	total := 0
	for _, c := range cats {
		total += c
	}
	if total == 0 {
		return 0
	}
	entropy := 0.0
	for _, c := range cats {
		p := float64(c) / float64(total)
		if p > 0 {
			entropy -= p * math.Log2(p)
		}
	}
	return entropy
}

func (fc *FeatureCollector) Stats() map[string]int {
	fc.mu.RLock()
	defer fc.mu.RUnlock()
	return map[string]int{"tracked_ips": len(fc.stats)}
}
