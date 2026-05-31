-- [DEPRECATED] Use Redis Cell (CL.THROTTLE) instead.
-- Redis Cell implements GCRA at C level with <1ms latency,
-- replacing the need for a Lua-based token bucket.
-- Kept only for reference; the engine no longer loads it.
--
-- Token bucket rate limiter
-- KEYS[1]: bucket key (e.g. "tb:order:<user_id>")
-- ARGV[1]: current timestamp in seconds (with ms precision)
-- ARGV[2]: bucket capacity
-- ARGV[3]: refill rate (tokens per second)
-- ARGV[4]: requested tokens (usually 1)
-- Returns: { allowed (0/1), remaining_tokens, wait_time_ms }

local key = KEYS[1]
local now = tonumber(ARGV[1])
local capacity = tonumber(ARGV[2])
local rate = tonumber(ARGV[3])
local requested = tonumber(ARGV[4])

local data = redis.call('HMGET', key, 'tokens', 'last_ts')
local tokens = tonumber(data[1])
local last_ts = tonumber(data[2])

if tokens == nil then
    tokens = capacity
    last_ts = now
end

local delta = math.max(0, now - last_ts)
tokens = math.min(capacity, tokens + delta * rate)

local allowed = 0
local wait_time = 0

if tokens >= requested then
    tokens = tokens - requested
    allowed = 1
else
    wait_time = math.ceil((requested - tokens) / rate * 1000)
end

redis.call('HMSET', key, 'tokens', tokens, 'last_ts', now)
redis.call('EXPIRE', key, math.ceil(capacity / rate) + 60)

return {allowed, tostring(tokens), wait_time}
