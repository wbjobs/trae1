-- [DEPRECATED] Use Redis Cell (CL.THROTTLE) instead.
-- This ZSET-based sliding window is O(log N) and blocks Redis at high QPS.
-- Kept only for reference; the engine no longer loads it.
--
-- Sliding window counter using sorted set
-- KEYS[1]: window key (e.g. "sw:login:<user_id>")
-- ARGV[1]: current timestamp in ms
-- ARGV[2]: window size in ms
-- ARGV[3]: max requests allowed in window
-- Returns: { allowed (0/1), current_count }

local key = KEYS[1]
local now = tonumber(ARGV[1])
local window = tonumber(ARGV[2])
local max_req = tonumber(ARGV[3])

local min_score = now - window

redis.call('ZREMRANGEBYSCORE', key, '-inf', min_score)

local member = now .. ':' .. math.random(1000000)
redis.call('ZADD', key, now, member)

local count = redis.call('ZCARD', key)

if count > max_req then
    redis.call('ZREM', key, member)
    return {0, count - 1}
end

redis.call('PEXPIRE', key, window)

return {1, count}
