package main

import (
	"context"
	"time"

	"github.com/redis/go-redis/v9"
)

type RedisClient struct {
	rdb *redis.Client
}

func NewRedisClient(addr, password string, db int) *RedisClient {
	rdb := redis.NewClient(&redis.Options{
		Addr:         addr,
		Password:     password,
		DB:         db,
		PoolSize:   100,
		MaxRetries: 3,
		DialTimeout:  5 * time.Second,
		ReadTimeout:  3 * time.Second,
		WriteTimeout: 3 * time.Second,
	})
	return &RedisClient{rdb: rdb}
}

func (r *RedisClient) Close() error {
	return r.rdb.Close()
}

func (r *RedisClient) Ping(ctx context.Context) error {
	return r.rdb.Ping(ctx).Err()
}

func (r *RedisClient) GetClient() *redis.Client {
	return r.rdb
}

func (r *RedisClient) NewPipeline() redis.Pipeliner {
	return r.rdb.Pipeline()
}

func (r *RedisClient) Eval(ctx context.Context, script string, keys []string, args ...interface{}) *redis.Cmd {
	return r.rdb.Eval(ctx, script, keys, args...)
}

func (r *RedisClient) ScriptLoad(ctx context.Context, script string) (string, error) {
	return r.rdb.ScriptLoad(ctx, script).Result()
}

func (r *RedisClient) EvalSha(ctx context.Context, sha string, keys []string, args ...interface{}) *redis.Cmd {
	return r.rdb.EvalSha(ctx, sha, keys, args...)
}

func (r *RedisClient) ZRangeByScore(ctx context.Context, key string, min, max string, offset, count int64) ([]string, error) {
	return r.rdb.ZRangeByScore(ctx, key, &redis.ZRangeBy{
		Min:    min,
		Max:    max,
		Offset: offset,
		Count:  count,
	}).Result()
}

func (r *RedisClient) ZRevRange(ctx context.Context, key string, start, stop int64) ([]string, error) {
	return r.rdb.ZRevRange(ctx, key, start, stop).Result()
}

func (r *RedisClient) HGetAll(ctx context.Context, key string) (map[string]string, error) {
	return r.rdb.HGetAll(ctx, key).Result()
}
