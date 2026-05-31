package api

import (
	"encoding/json"
	"net/http"
	"strconv"

	"cdn-cache-protector/internal/bloom"
	"cdn-cache-protector/internal/cache"
	"cdn-cache-protector/internal/detector"
	"cdn-cache-protector/internal/model"
)

type HandlerStats interface {
	GetShuntStats() model.ResponseShuntStats
}

type ManagementAPI struct {
	bloomFilter *bloom.Filter
	cache       *cache.Cache
	detector    *detector.HotKeyDetector
	handler     HandlerStats
}

func NewManagementAPI(
	bloomFilter *bloom.Filter,
	cache *cache.Cache,
	detector *detector.HotKeyDetector,
	handler HandlerStats,
) *ManagementAPI {
	return &ManagementAPI{
		bloomFilter: bloomFilter,
		cache:       cache,
		detector:    detector,
		handler:     handler,
	}
}

func (api *ManagementAPI) RegisterRoutes(mux *http.ServeMux) {
	mux.HandleFunc("/api/stats", api.GetStats)
	mux.HandleFunc("/api/hotkeys", api.GetHotKeys)
	mux.HandleFunc("/api/bloom/stats", api.GetBloomStats)
	mux.HandleFunc("/api/bloom/adaptive", api.GetAdaptiveBloomStats)
	mux.HandleFunc("/api/bloom/reset", api.ResetBloomFilter)
	mux.HandleFunc("/api/bloom/rebuild", api.RebuildBloomFilter)
	mux.HandleFunc("/api/bloom/config", api.ConfigBloomFilter)
	mux.HandleFunc("/api/shunt/stats", api.GetShuntStats)
}

func (api *ManagementAPI) GetStats(w http.ResponseWriter, r *http.Request) {
	hits, misses, originCalls := api.cache.GetStats()

	totalRequests := hits + misses + originCalls
	cacheHitRate := 0.0
	if totalRequests > 0 {
		cacheHitRate = float64(hits) / float64(totalRequests)
	}

	stats := model.SystemStats{
		HotKeys:       api.detector.GetHotKeys(),
		BloomStats:    api.bloomFilter.GetStats(),
		ShuntStats:    api.handler.GetShuntStats(),
		TotalRequests: totalRequests,
		CacheHits:     hits,
		CacheMisses:   misses,
		OriginFetches: originCalls,
		CacheHitRate:  cacheHitRate,
	}

	api.respondJSON(w, http.StatusOK, stats)
}

func (api *ManagementAPI) GetShuntStats(w http.ResponseWriter, r *http.Request) {
	stats := api.handler.GetShuntStats()
	api.respondJSON(w, http.StatusOK, stats)
}

func (api *ManagementAPI) GetHotKeys(w http.ResponseWriter, r *http.Request) {
	hotKeys := api.detector.GetHotKeys()
	api.respondJSON(w, http.StatusOK, map[string]interface{}{
		"hot_keys": hotKeys,
		"count":    len(hotKeys),
	})
}

func (api *ManagementAPI) GetBloomStats(w http.ResponseWriter, r *http.Request) {
	stats := api.bloomFilter.GetStats()
	api.respondJSON(w, http.StatusOK, stats)
}

func (api *ManagementAPI) ResetBloomFilter(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	api.bloomFilter.Reset()
	api.respondJSON(w, http.StatusOK, map[string]string{
		"status": "success",
		"message": "bloom filter reset successfully",
	})
}

func (api *ManagementAPI) GetAdaptiveBloomStats(w http.ResponseWriter, r *http.Request) {
	stats := api.bloomFilter.GetAdaptiveStats()
	api.respondJSON(w, http.StatusOK, stats)
}

func (api *ManagementAPI) RebuildBloomFilter(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	if err := api.bloomFilter.Rebuild(); err != nil {
		api.respondJSON(w, http.StatusInternalServerError, map[string]string{
			"status": "error",
			"message": err.Error(),
		})
		return
	}

	api.respondJSON(w, http.StatusOK, map[string]string{
		"status": "success",
		"message": "bloom filter rebuild triggered successfully",
	})
}

func (api *ManagementAPI) ConfigBloomFilter(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodGet {
		api.respondJSON(w, http.StatusOK, map[string]interface{}{
			"target_fpr": api.bloomFilter.TargetFPR(),
		})
		return
	}

	if r.Method != http.MethodPut && r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	fprStr := r.URL.Query().Get("fpr")
	if fprStr == "" {
		api.respondJSON(w, http.StatusBadRequest, map[string]string{
			"status": "error",
			"message": "fpr parameter is required (range: 0.001-0.05)",
		})
		return
	}

	fpr, err := strconv.ParseFloat(fprStr, 64)
	if err != nil {
		api.respondJSON(w, http.StatusBadRequest, map[string]string{
			"status": "error",
			"message": "invalid fpr value",
		})
		return
	}

	if fpr < 0.001 || fpr > 0.05 {
		api.respondJSON(w, http.StatusBadRequest, map[string]string{
			"status": "error",
			"message": "fpr must be between 0.001 (0.1%) and 0.05 (5%)",
		})
		return
	}

	api.bloomFilter.UpdateTargetFPR(fpr)
	api.respondJSON(w, http.StatusOK, map[string]interface{}{
		"status":     "success",
		"message":    "target FPR updated successfully",
		"target_fpr": fpr,
	})
}

func (api *ManagementAPI) respondJSON(w http.ResponseWriter, status int, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(data)
}
