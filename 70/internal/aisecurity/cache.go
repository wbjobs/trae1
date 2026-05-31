package aisecurity

import (
	"crypto/sha256"
	"encoding/hex"
	"sync"
	"time"
)

type cacheEntry struct {
	Assessment *SecurityAssessment
	ExpiresAt  time.Time
}

type Cache struct {
	entries map[string]*cacheEntry
	mu      sync.RWMutex
	ttl     time.Duration
}

func NewCache(ttl time.Duration) *Cache {
	c := &Cache{
		entries: make(map[string]*cacheEntry),
		ttl:     ttl,
	}
	go c.cleanupLoop()
	return c
}

func (c *Cache) Get(command string, args []string) (*SecurityAssessment, bool) {
	key := cacheKey(command, args)

	c.mu.RLock()
	defer c.mu.RUnlock()

	entry, ok := c.entries[key]
	if !ok {
		return nil, false
	}

	if time.Now().After(entry.ExpiresAt) {
		return nil, false
	}

	return entry.Assessment, true
}

func (c *Cache) Set(command string, args []string, assessment *SecurityAssessment) {
	key := cacheKey(command, args)

	c.mu.Lock()
	defer c.mu.Unlock()

	c.entries[key] = &cacheEntry{
		Assessment: assessment,
		ExpiresAt:  time.Now().Add(c.ttl),
	}
}

func (c *Cache) Delete(command string, args []string) {
	key := cacheKey(command, args)

	c.mu.Lock()
	defer c.mu.Unlock()

	delete(c.entries, key)
}

func (c *Cache) Clear() {
	c.mu.Lock()
	defer c.mu.Unlock()

	c.entries = make(map[string]*cacheEntry)
}

func (c *Cache) cleanupLoop() {
	ticker := time.NewTicker(10 * time.Minute)
	defer ticker.Stop()

	for range ticker.C {
		c.cleanup()
	}
}

func (c *Cache) cleanup() {
	c.mu.Lock()
	defer c.mu.Unlock()

	now := time.Now()
	for key, entry := range c.entries {
		if now.After(entry.ExpiresAt) {
			delete(c.entries, key)
		}
	}
}

func cacheKey(command string, args []string) string {
	h := sha256.New()
	h.Write([]byte(command))
	for _, arg := range args {
		h.Write([]byte("\x00"))
		h.Write([]byte(arg))
	}
	return hex.EncodeToString(h.Sum(nil))
}
