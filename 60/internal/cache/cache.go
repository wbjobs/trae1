package cache

import (
	"context"
	"encoding/json"
	"fmt"
	"sync/atomic"
	"time"

	"github.com/redis/go-redis/v9"

	"cdn-cache-protector/internal/model"
)

type Cache struct {
	client       *redis.Client
	cacheHits    atomic.Int64
	cacheMisses  atomic.Int64
	originCalls  atomic.Int64
}

func New(redisAddr string, redisPassword string, db int) *Cache {
	client := redis.NewClient(&redis.Options{
		Addr:     redisAddr,
		Password: redisPassword,
		DB:       db,
	})

	return &Cache{
		client: client,
	}
}

func (c *Cache) Get(ctx context.Context, key string) (*model.CacheItem, error) {
	data, err := c.client.Get(ctx, key).Bytes()
	if err == redis.Nil {
		c.cacheMisses.Add(1)
		return nil, nil
	}
	if err != nil {
		return nil, fmt.Errorf("redis get failed: %w", err)
	}

	var item model.CacheItem
	if err := json.Unmarshal(data, &item); err != nil {
		return nil, fmt.Errorf("unmarshal cache item failed: %w", err)
	}

	c.cacheHits.Add(1)
	return &item, nil
}

func (c *Cache) Set(ctx context.Context, item *model.CacheItem) error {
	data, err := json.Marshal(item)
	if err != nil {
		return fmt.Errorf("marshal cache item failed: %w", err)
	}

	ttl := time.Duration(item.TTL) * time.Second
	if err := c.client.Set(ctx, item.Key, data, ttl).Err(); err != nil {
		return fmt.Errorf("redis set failed: %w", err)
	}

	return nil
}

func (c *Cache) Delete(ctx context.Context, key string) error {
	if err := c.client.Del(ctx, key).Err(); err != nil {
		return fmt.Errorf("redis delete failed: %w", err)
	}
	return nil
}

func (c *Cache) RecordOriginCall() {
	c.originCalls.Add(1)
}

func (c *Cache) GetStats() (hits, misses, originCalls int64) {
	return c.cacheHits.Load(), c.cacheMisses.Load(), c.originCalls.Load()
}

func (c *Cache) Close() error {
	return c.client.Close()
}

func (c *Cache) Ping(ctx context.Context) error {
	return c.client.Ping(ctx).Err()
}
