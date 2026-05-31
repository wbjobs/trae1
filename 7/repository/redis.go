package repository

import (
	"context"
	"encoding/json"
	"fmt"
	"time"

	"api-signature/config"
	"api-signature/model"

	"github.com/go-redis/redis/v8"
)

var RedisClient *redis.Client

var Ctx = context.Background()

func InitRedis() error {
	cfg := config.AppConfig.Redis

	RedisClient = redis.NewClient(&redis.Options{
		Addr:     fmt.Sprintf("%s:%d", cfg.Host, cfg.Port),
		Password: cfg.Password,
		DB:       cfg.DB,
		PoolSize: cfg.PoolSize,
	})

	ctx, cancel := context.WithTimeout(Ctx, 5*time.Second)
	defer cancel()

	if err := RedisClient.Ping(ctx).Err(); err != nil {
		return fmt.Errorf("failed to connect to redis: %w", err)
	}

	return nil
}

func CloseRedis() error {
	if RedisClient != nil {
		return RedisClient.Close()
	}
	return nil
}

func SetSignatureNonce(nonce string, clientID string, expiration time.Duration) error {
	key := fmt.Sprintf("signature:nonce:%s", nonce)
	return RedisClient.Set(Ctx, key, clientID, expiration).Err()
}

func CheckNonceExists(nonce string) (bool, error) {
	key := fmt.Sprintf("signature:nonce:%s", nonce)
	result, err := RedisClient.Exists(Ctx, key).Result()
	if err != nil {
		return false, err
	}
	return result > 0, nil
}

func SetRequestSignature(clientID string, signature string, expiration time.Duration) error {
	key := fmt.Sprintf("signature:request:%s:%s", clientID, signature)
	return RedisClient.Set(Ctx, key, "1", expiration).Err()
}

func CheckRequestSignature(clientID string, signature string) (bool, error) {
	key := fmt.Sprintf("signature:request:%s:%s", clientID, signature)
	result, err := RedisClient.Exists(Ctx, key).Result()
	if err != nil {
		return false, err
	}
	return result > 0, nil
}

func AddIPBlacklist(ip string, reason string, duration time.Duration) error {
	key := "blacklist:ip"
	field := ip
	value := fmt.Sprintf("%s|%d", reason, time.Now().Add(duration).Unix())
	return RedisClient.HSet(Ctx, key, field, value).Err()
}

func RemoveIPBlacklist(ip string) error {
	key := "blacklist:ip"
	return RedisClient.HDel(Ctx, key, ip).Err()
}

func IsIPBlacklisted(ip string) (bool, string, error) {
	key := "blacklist:ip"
	result, err := RedisClient.HGet(Ctx, key, ip).Result()
	if err == redis.Nil {
		return false, "", nil
	}
	if err != nil {
		return false, "", err
	}
	return true, result, nil
}

func AddIPWhitelist(ip string) error {
	key := "whitelist:ip"
	return RedisClient.SAdd(Ctx, key, ip).Err()
}

func RemoveIPWhitelist(ip string) error {
	key := "whitelist:ip"
	return RedisClient.SRem(Ctx, key, ip).Err()
}

func IsIPWhitelisted(ip string) (bool, error) {
	key := "whitelist:ip"
	result, err := RedisClient.SIsMember(Ctx, key, ip).Result()
	if err != nil {
		return false, err
	}
	return result, nil
}

func IncrementRateLimit(clientID string, window time.Duration) (int64, error) {
	key := fmt.Sprintf("ratelimit:global:%s", clientID)
	pipe := RedisClient.TxPipeline()
	incr := pipe.Incr(Ctx, key)
	pipe.Expire(Ctx, key, window)
	_, err := pipe.Exec(Ctx)
	if err != nil {
		return 0, err
	}
	return incr.Val(), nil
}

func GetRateLimitCount(clientID string) (int64, error) {
	key := fmt.Sprintf("ratelimit:global:%s", clientID)
	result, err := RedisClient.Get(Ctx, key).Int64()
	if err == redis.Nil {
		return 0, nil
	}
	if err != nil {
		return 0, err
	}
	return result, nil
}

func IncrementEndpointRateLimit(clientID string, path string, window time.Duration) (int64, error) {
	key := fmt.Sprintf("ratelimit:endpoint:%s:%s", clientID, path)
	pipe := RedisClient.TxPipeline()
	incr := pipe.Incr(Ctx, key)
	pipe.Expire(Ctx, key, window)
	_, err := pipe.Exec(Ctx)
	if err != nil {
		return 0, err
	}
	return incr.Val(), nil
}

func GetEndpointRateLimitCount(clientID string, path string) (int64, error) {
	key := fmt.Sprintf("ratelimit:endpoint:%s:%s", clientID, path)
	result, err := RedisClient.Get(Ctx, key).Int64()
	if err == redis.Nil {
		return 0, nil
	}
	if err != nil {
		return 0, err
	}
	return result, nil
}

func SaveAuditLog(logEntry interface{}) error {
	key := "audit:logs"
	jsonData, err := json.Marshal(logEntry)
	if err != nil {
		return err
	}
	return RedisClient.LPush(Ctx, key, string(jsonData)).Err()
}

func SetClientToken(clientID string, token string, expiration time.Duration) error {
	key := fmt.Sprintf("client:token:%s", clientID)
	return RedisClient.Set(Ctx, key, token, expiration).Err()
}

func GetClientToken(clientID string) (string, error) {
	key := fmt.Sprintf("client:token:%s", clientID)
	result, err := RedisClient.Get(Ctx, key).Result()
	if err == redis.Nil {
		return "", nil
	}
	if err != nil {
		return "", err
	}
	return result, nil
}

func DeleteClientToken(clientID string) error {
	key := fmt.Sprintf("client:token:%s", clientID)
	return RedisClient.Del(Ctx, key).Err()
}

func SetClientSecret(clientID string, secret string, version int, expiration time.Duration) error {
	key := fmt.Sprintf("client:secret:%s", clientID)
	data := map[string]interface{}{
		"secret":  secret,
		"version": version,
	}
	jsonData, err := json.Marshal(data)
	if err != nil {
		return err
	}
	return RedisClient.Set(Ctx, key, string(jsonData), expiration).Err()
}

func GetClientSecret(clientID string) (string, int, error) {
	key := fmt.Sprintf("client:secret:%s", clientID)
	result, err := RedisClient.Get(Ctx, key).Result()
	if err == redis.Nil {
		return "", 0, nil
	}
	if err != nil {
		return "", 0, err
	}

	var data map[string]interface{}
	if err := json.Unmarshal([]byte(result), &data); err != nil {
		return "", 0, err
	}

	secret, _ := data["secret"].(string)
	versionFloat, _ := data["version"].(float64)
	return secret, int(versionFloat), nil
}

func SetSecretHistory(clientID string, history []model.SecretVersion) error {
	key := fmt.Sprintf("client:secret:history:%s", clientID)
	jsonData, err := json.Marshal(history)
	if err != nil {
		return err
	}
	return RedisClient.Set(Ctx, key, string(jsonData), 0).Err()
}

func GetSecretHistory(clientID string) ([]model.SecretVersion, error) {
	key := fmt.Sprintf("client:secret:history:%s", clientID)
	result, err := RedisClient.Get(Ctx, key).Result()
	if err == redis.Nil {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}

	var history []model.SecretVersion
	if err := json.Unmarshal([]byte(result), &history); err != nil {
		return nil, err
	}
	return history, nil
}

func IncrementStatsCounter(counterType string, field string, value int64) error {
	key := fmt.Sprintf("stats:%s", counterType)
	return RedisClient.HIncrBy(Ctx, key, field, value).Err()
}

func GetStatsCounter(counterType string) (map[string]string, error) {
	key := fmt.Sprintf("stats:%s", counterType)
	return RedisClient.HGetAll(Ctx, key).Result()
}

func IncrementErrorStats(errorType string, clientID string, ip string) error {
	pipe := RedisClient.TxPipeline()

	pipe.HIncrBy(Ctx, "stats:errors", errorType, 1)

	if clientID != "" {
		pipe.HIncrBy(Ctx, "stats:errors_by_client", fmt.Sprintf("%s:%s", errorType, clientID), 1)
	}

	if ip != "" {
		pipe.HIncrBy(Ctx, "stats:errors_by_ip", fmt.Sprintf("%s:%s", errorType, ip), 1)
	}

	_, err := pipe.Exec(Ctx)
	return err
}

func IncrementEndpointStats(path string, clientID string) error {
	pipe := RedisClient.TxPipeline()
	pipe.HIncrBy(Ctx, "stats:endpoints", path, 1)
	if clientID != "" {
		pipe.HIncrBy(Ctx, "stats:client_endpoints", fmt.Sprintf("%s:%s", clientID, path), 1)
	}
	_, err := pipe.Exec(Ctx)
	return err
}

func GetAllStats() (*model.AbnormalStats, error) {
	stats := &model.AbnormalStats{
		ClientStats:    make(map[string]int64),
		EndpointStats:  make(map[string]int64),
		IPStats:        make(map[string]int64),
		ErrorTypeStats: make(map[string]int64),
	}

	errorStats, _ := GetStatsCounter("errors")
	for k, v := range errorStats {
		var count int64
		for _, c := range v {
			count = count*10 + int64(c-'0')
		}
		stats.ErrorTypeStats[k] = count
	}

	endpointStats, _ := GetStatsCounter("endpoints")
	for k, v := range endpointStats {
		var count int64
		for _, c := range v {
			count = count*10 + int64(c-'0')
		}
		stats.EndpointStats[k] = count
	}

	return stats, nil
}

func SaveSecurityAlert(alert *model.SecurityAlert) error {
	key := "security:alerts"
	jsonData, err := json.Marshal(alert)
	if err != nil {
		return err
	}
	return RedisClient.LPush(Ctx, key, string(jsonData)).Err()
}

func GetSecurityAlerts(limit int64) ([]model.SecurityAlert, error) {
	key := "security:alerts"
	if limit <= 0 {
		limit = 100
	}

	results, err := RedisClient.LRange(Ctx, key, 0, limit-1).Result()
	if err != nil {
		return nil, err
	}

	var alerts []model.SecurityAlert
	for _, result := range results {
		var alert model.SecurityAlert
		if err := json.Unmarshal([]byte(result), &alert); err == nil {
			alerts = append(alerts, alert)
		}
	}
	return alerts, nil
}

func GetRateLimitResetTime(clientID string) (int64, error) {
	key := fmt.Sprintf("ratelimit:global:%s", clientID)
	ttl, err := RedisClient.TTL(Ctx, key).Result()
	if err != nil {
		return 0, err
	}
	return time.Now().Add(ttl).Unix(), nil
}
