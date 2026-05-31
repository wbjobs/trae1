-- Lightweight fixed-window counter (O(1), < 1ms)
-- Used as a fallback when Redis Cell (CL.THROTTLE) is unavailable.
--
-- KEYS[1]: counter key (e.g. "fw:login:<user_id>")
-- ARGV[1]: window size in seconds
-- ARGV[2]: max requests allowed in window
-- Returns: { allowed (0/1), current_count, remaining_ttl }

local key = KEYS[1]
local window = tonumber(ARGV[1])
local max_req = tonumber(ARGV[2])

local count = redis.call('INCR', key)
local ttl = redis.call('TTL', key)

if count == 1 then
    redis.call('EXPIRE', key, window)
    ttl = window
end

if count > max_req then
    return {0, count, ttl}
end

return {1, count, ttl}
