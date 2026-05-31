package handler

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"sync/atomic"
	"time"

	"cdn-cache-protector/internal/bloom"
	"cdn-cache-protector/internal/cache"
	"cdn-cache-protector/internal/detector"
	"cdn-cache-protector/internal/model"
	"cdn-cache-protector/internal/singleflight"
)

const (
	LargeResponseThreshold = 1 * 1024 * 1024
	RedirectRetryAfter     = 5
)

type Handler struct {
	bloomFilter  *bloom.Filter
	cache        *cache.Cache
	detector     *detector.HotKeyDetector
	singleflight *singleflight.Group
	originFunc   func(ctx context.Context, key string) (interface{}, error)

	primaryResponses   atomic.Int64
	redirectResponses  atomic.Int64
	largeResponses     atomic.Int64
	totalRedirectCount atomic.Int64
}

func New(
	bloomFilter *bloom.Filter,
	cache *cache.Cache,
	detector *detector.HotKeyDetector,
	singleflight *singleflight.Group,
	originFunc func(ctx context.Context, key string) (interface{}, error),
) *Handler {
	return &Handler{
		bloomFilter:  bloomFilter,
		cache:        cache,
		detector:     detector,
		singleflight: singleflight,
		originFunc:   originFunc,
	}
}

func (h *Handler) GetShuntStats() model.ResponseShuntStats {
	return model.ResponseShuntStats{
		PrimaryResponses:   h.primaryResponses.Load(),
		RedirectResponses:  h.redirectResponses.Load(),
		LargeResponses:     h.largeResponses.Load(),
		TotalRedirectCount: h.totalRedirectCount.Load(),
	}
}

func (h *Handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	key := r.URL.Query().Get("key")
	if key == "" {
		http.Error(w, "key parameter is required", http.StatusBadRequest)
		return
	}

	h.detector.RecordRequest(key)

	if h.detector.IsHotKey(key) {
		h.bloomFilter.Add(key)
	}

	exists := h.bloomFilter.Contains(key)
	h.bloomFilter.RecordRequest(key, exists)

	if !exists {
		h.fetchFromOrigin(w, r, key)
		return
	}

	ctx := r.Context()
	item, err := h.cache.Get(ctx, key)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	if item != nil {
		h.respondJSON(w, http.StatusOK, map[string]interface{}{
			"source": "cache",
			"data":   item.Value,
			"key":    key,
		})
		return
	}

	h.fetchWithSingleflight(w, r, key)
}

func (h *Handler) fetchWithSingleflight(w http.ResponseWriter, r *http.Request, key string) {
	ctx := r.Context()

	result := h.singleflight.DoWithPosition(key, func() (interface{}, error) {
		h.cache.RecordOriginCall()
		data, err := h.originFunc(ctx, key)
		if err != nil {
			return nil, err
		}

		item := &model.CacheItem{
			Key:       key,
			Value:     data,
			TTL:       300,
			CreatedAt: time.Now(),
		}

		if err := h.cache.Set(ctx, item); err != nil {
			return nil, err
		}

		return data, nil
	})

	if result.Err != nil {
		http.Error(w, result.Err.Error(), http.StatusInternalServerError)
		return
	}

	isLarge := estimateSize(result.Val) >= LargeResponseThreshold

	if isLarge {
		h.largeResponses.Add(1)
	}

	if isLarge && !result.IsPrimary {
		h.redirectResponses.Add(1)
		h.totalRedirectCount.Add(result.WaitCount)
		h.respondRedirect(w, r, key)
		return
	}

	if isLarge && result.IsPrimary {
		h.primaryResponses.Add(1)
		w.Header().Set("X-Cache-Warmup", "primary")
		w.Header().Set("X-Waiting-Count", fmt.Sprintf("%d", result.WaitCount))
	}

	h.respondJSON(w, http.StatusOK, map[string]interface{}{
		"source": "origin",
		"data":   result.Val,
		"key":    key,
	})
}

func (h *Handler) fetchFromOrigin(w http.ResponseWriter, r *http.Request, key string) {
	ctx := r.Context()
	h.cache.RecordOriginCall()

	data, err := h.originFunc(ctx, key)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	h.respondJSON(w, http.StatusOK, map[string]interface{}{
		"source": "origin-direct",
		"data":   data,
		"key":    key,
	})
}

func (h *Handler) respondRedirect(w http.ResponseWriter, r *http.Request, key string) {
	retryURL := fmt.Sprintf("%s?key=%s", r.URL.Path, key)
	w.Header().Set("Location", retryURL)
	w.Header().Set("Retry-After", fmt.Sprintf("%d", RedirectRetryAfter))
	w.Header().Set("X-Cache-Warmup", "redirected")
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusTemporaryRedirect)

	json.NewEncoder(w).Encode(map[string]interface{}{
		"status":      "warming_up",
		"message":     "cache is warming up, please retry later",
		"retry_after": RedirectRetryAfter,
		"key":         key,
	})
}

func estimateSize(v interface{}) int64 {
	switch val := v.(type) {
	case string:
		return int64(len(val))
	case []byte:
		return int64(len(val))
	case map[string]interface{}:
		if data, err := json.Marshal(val); err == nil {
			return int64(len(data))
		}
	}

	data, err := json.Marshal(v)
	if err != nil {
		return 0
	}
	return int64(len(data))
}

func (h *Handler) respondJSON(w http.ResponseWriter, status int, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(data)
}
