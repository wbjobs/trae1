package model

import "time"

type CacheItem struct {
	Key       string      `json:"key"`
	Value     interface{} `json:"value"`
	TTL       int64       `json:"ttl"`
	CreatedAt time.Time   `json:"created_at"`
}

type HotKeyInfo struct {
	Key         string    `json:"key"`
	Count       int64     `json:"count"`
	WindowStart time.Time `json:"window_start"`
	IsHot       bool      `json:"is_hot"`
}

type BloomFilterStats struct {
	TotalRequests    int64   `json:"total_requests"`
	BloomHits        int64   `json:"bloom_hits"`
	BloomMisses      int64   `json:"bloom_misses"`
	FalsePositives   int64   `json:"false_positives"`
	FalsePositiveRate float64 `json:"false_positive_rate"`
	EstimatedSize    int64   `json:"estimated_size"`
}

type AdaptiveBloomStats struct {
	TargetFPR        float64 `json:"target_fpr"`
	CurrentFPR       float64 `json:"current_fpr"`
	ExpectedItems    uint64  `json:"expected_items"`
	CurrentItems     int64   `json:"current_items"`
	BFSizeBytes      int64   `json:"bf_size_bytes"`
	HashFunctions    uint64  `json:"hash_functions"`
	IsRebuilding     bool    `json:"is_rebuilding"`
	RebuildCount     int64   `json:"rebuild_count"`
	LastRebuildTime  string  `json:"last_rebuild_time"`
	TrackedKeys      int     `json:"tracked_keys"`
}

type ResponseShuntStats struct {
	PrimaryResponses   int64 `json:"primary_responses"`
	RedirectResponses  int64 `json:"redirect_responses"`
	LargeResponses     int64 `json:"large_responses"`
	TotalRedirectCount int64 `json:"total_redirect_count"`
}

type SystemStats struct {
	HotKeys         []HotKeyInfo      `json:"hot_keys"`
	BloomStats      BloomFilterStats  `json:"bloom_stats"`
	ShuntStats      ResponseShuntStats `json:"shunt_stats"`
	TotalRequests   int64             `json:"total_requests"`
	CacheHits       int64             `json:"cache_hits"`
	CacheMisses     int64             `json:"cache_misses"`
	OriginFetches   int64             `json:"origin_fetches"`
	CacheHitRate    float64           `json:"cache_hit_rate"`
}

type RequestContext struct {
	Key       string
	StartTime time.Time
}
