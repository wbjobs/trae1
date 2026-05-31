package threatintel

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"sync"
	"time"
)

type ThreatIntelClient struct {
	mu          sync.RWMutex
	apiURL      string
	apiKey      string
	httpClient  *http.Client
	cache       map[string]*IPReport
	cacheTTL    time.Duration
}

type IPReport struct {
	IPAddress    string   `json:"ipAddress"`
	IsMalicious  bool     `json:"isMalicious"`
	ThreatTypes  []string `json:"threatTypes"`
	Confidence   float64  `json:"confidence"`
	LastReported time.Time `json:"lastReported"`
	Provider     string   `json:"provider"`
	Country      string   `json:"country"`
	ISP          string   `json:"isp"`
	UsageType    string   `json:"usageType"`
}

type cacheEntry struct {
	report    *IPReport
	expiresAt time.Time
}

var threatCache = make(map[string]*cacheEntry)
var cacheMu sync.RWMutex

func NewThreatIntelClient(apiURL, apiKey string) *ThreatIntelClient {
	return &ThreatIntelClient{
		apiURL:     apiURL,
		apiKey:     apiKey,
		httpClient: &http.Client{
			Timeout: 5 * time.Second,
		},
		cache:    make(map[string]*IPReport),
		cacheTTL: 1 * time.Hour,
	}
}

func (c *ThreatIntelClient) LookupIP(ip string) (*IPReport, error) {
	if ip == "" || ip == "127.0.0.1" || ip == "::1" {
		return &IPReport{
			IPAddress:   ip,
			IsMalicious: false,
			Confidence:  0,
			Provider:    "local",
			Country:     "local",
			ISP:         "localhost",
			UsageType:   "local",
		}, nil
	}

	cacheMu.RLock()
	if entry, exists := threatCache[ip]; exists && time.Now().Before(entry.expiresAt) {
		cacheMu.RUnlock()
		return entry.report, nil
	}
	cacheMu.RUnlock()

	report, err := c.queryThreatIntelAPI(ip)
	if err != nil {
		return c.getMockReport(ip), nil
	}

	cacheMu.Lock()
	threatCache[ip] = &cacheEntry{
		report:    report,
		expiresAt: time.Now().Add(c.cacheTTL),
	}
	cacheMu.Unlock()

	return report, nil
}

func (c *ThreatIntelClient) queryThreatIntelAPI(ip string) (*IPReport, error) {
	if c.apiURL == "" {
		return nil, fmt.Errorf("threat intel API not configured")
	}

	url := fmt.Sprintf("%s/api/v1/ip/%s", c.apiURL, ip)

	req, err := http.NewRequest("GET", url, nil)
	if err != nil {
		return nil, err
	}

	if c.apiKey != "" {
		req.Header.Set("Authorization", "Bearer "+c.apiKey)
	}
	req.Header.Set("Accept", "application/json")

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("threat intel API returned status %d", resp.StatusCode)
	}

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}

	var report IPReport
	if err := json.Unmarshal(body, &report); err != nil {
		return nil, err
	}

	return &report, nil
}

func (c *ThreatIntelClient) getMockReport(ip string) *IPReport {
	parts := splitIP(ip)
	isPrivate := isPrivateIP(parts)

	if isPrivate {
		return &IPReport{
			IPAddress:   ip,
			IsMalicious: false,
			Confidence:  0,
			Provider:    "mock",
			Country:     "internal",
			ISP:         "internal-network",
			UsageType:   "private",
		}
	}

	return &IPReport{
		IPAddress:   ip,
		IsMalicious: false,
		Confidence:  0.1,
		Provider:    "mock",
		Country:     "unknown",
		ISP:         "unknown-isp",
		UsageType:   "commercial",
	}
}

func splitIP(ip string) []int {
	parts := make([]int, 4)
	for i, part := range []byte(ip) {
		if i < 4 {
			parts[i] = int(part)
		}
	}
	return parts
}

func isPrivateIP(parts []int) bool {
	if len(parts) < 4 {
		return false
	}
	if parts[0] == 10 {
		return true
	}
	if parts[0] == 172 && parts[1] >= 16 && parts[1] <= 31 {
		return true
	}
	if parts[0] == 192 && parts[1] == 168 {
		return true
	}
	return false
}

func ClearCache() {
	cacheMu.Lock()
	defer cacheMu.Unlock()
	threatCache = make(map[string]*cacheEntry)
}

func GetCacheStats() map[string]interface{} {
	cacheMu.RLock()
	defer cacheMu.RUnlock()

	return map[string]interface{}{
		"size": len(threatCache),
	}
}
