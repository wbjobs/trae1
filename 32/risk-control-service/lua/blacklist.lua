-- Blacklist check (global + action-specific + user lookup)
-- KEYS[1]: global blacklist set key
-- KEYS[2]: action-specific blacklist set key
-- KEYS[3]: user blacklist hash key (for reason lookup)
-- ARGV[1]: user_id
-- ARGV[2]: action_type
-- Returns: { blocked (0/1), reason_or_empty }

local global_key = KEYS[1]
local action_key = KEYS[2]
local user_key = KEYS[3]
local user_id = ARGV[1]
local action_type = ARGV[2]

local in_global = redis.call('SISMEMBER', global_key, user_id)
if in_global == 1 then
    local reason = redis.call('HGET', user_key, 'reason') or 'user is in global blacklist'
    return {1, reason}
end

local in_action = redis.call('SISMEMBER', action_key, user_id)
if in_action == 1 then
    local reason = redis.call('HGET', user_key, 'reason') or ('user is blocked from ' .. action_type)
    return {1, reason}
end

return {0, ''}
