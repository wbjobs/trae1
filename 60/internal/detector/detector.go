package detector

import (
	"sync"
	"time"

	"cdn-cache-protector/internal/model"
)

type HotKeyDetector struct {
	mu          sync.RWMutex
	window      time.Duration
	threshold   int64
	requests    map[string]*requestCounter
	hotKeys     map[string]bool
	onHotKey    func(string)
}

type requestCounter struct {
	count       int64
	windowStart time.Time
}

func New(window time.Duration, threshold int64, onHotKey func(string)) *HotKeyDetector {
	return &HotKeyDetector{
		window:    window,
		threshold: threshold,
		requests:  make(map[string]*requestCounter),
		hotKeys:   make(map[string]bool),
		onHotKey:  onHotKey,
	}
}

func (d *HotKeyDetector) RecordRequest(key string) {
	d.mu.Lock()
	defer d.mu.Unlock()

	now := time.Now()

	counter, exists := d.requests[key]
	if !exists {
		d.requests[key] = &requestCounter{
			count:       1,
			windowStart: now,
		}
		return
	}

	counter.count++

	if now.Sub(counter.windowStart) > d.window {
		counter.count = 1
		counter.windowStart = now
		return
	}

	if counter.count >= d.threshold && !d.hotKeys[key] {
		d.hotKeys[key] = true
		if d.onHotKey != nil {
			d.onHotKey(key)
		}
	}
}

func (d *HotKeyDetector) IsHotKey(key string) bool {
	d.mu.RLock()
	defer d.mu.RUnlock()
	return d.hotKeys[key]
}

func (d *HotKeyDetector) GetHotKeys() []model.HotKeyInfo {
	d.mu.RLock()
	defer d.mu.RUnlock()

	var hotKeys []model.HotKeyInfo
	for key, isHot := range d.hotKeys {
		if !isHot {
			continue
		}

		if counter, exists := d.requests[key]; exists {
			hotKeys = append(hotKeys, model.HotKeyInfo{
				Key:         key,
				Count:       counter.count,
				WindowStart: counter.windowStart,
				IsHot:       true,
			})
		}
	}
	return hotKeys
}

func (d *HotKeyDetector) GetKeyCount(key string) int64 {
	d.mu.RLock()
	defer d.mu.RUnlock()

	if counter, exists := d.requests[key]; exists {
		return counter.count
	}
	return 0
}

func (d *HotKeyDetector) Cleanup() {
	d.mu.Lock()
	defer d.mu.Unlock()

	now := time.Now()
	for key, counter := range d.requests {
		if now.Sub(counter.windowStart) > d.window {
			delete(d.requests, key)
			delete(d.hotKeys, key)
		}
	}
}

func (d *HotKeyDetector) StartCleanup(interval time.Duration) {
	go func() {
		ticker := time.NewTicker(interval)
		defer ticker.Stop()
		for range ticker.C {
			d.Cleanup()
		}
	}()
}
