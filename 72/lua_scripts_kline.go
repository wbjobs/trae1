package main

const KLineAggregateLuaScript = `
-- KEYS:
--   1 = kline:{code}:{period}   (sortedset of OHLC)
--
-- ARGV:
--   1 = period_seconds (as string)
--   2 = timestamp_ms (as string)
--   3 = price (as string)

local period = tonumber(ARGV[1])
local ts_ms = tonumber(ARGV[2])
local price = tonumber(ARGV[3])
local ts_sec = math.floor(ts_ms / 1000)

local bucket = math.floor(ts_sec / period) * period
local bucket_ms = bucket * 1000

local existing = redis.call('ZSCORE', KEYS[1], bucket)

if existing then
    local parts = {}
    for p in string.gmatch(existing, '[^:]+') do
        table.insert(parts, p)
    end
    local o = tonumber(parts[2])
    local h = tonumber(parts[3])
    local l = tonumber(parts[4])
    local v = tonumber(parts[6]) or 0

    if price > h then h = price end
    if price < l then l = price end
    v = v + 1

    local new_member = bucket .. ':' .. string.format('%.4f', o) .. ':'
        .. string.format('%.4f', h) .. ':' .. string.format('%.4f', l) .. ':'
        .. string.format('%.4f', price) .. ':' .. v

    redis.call('ZADD', KEYS[1], bucket, new_member)
    return 1
else
    local member = bucket .. ':' .. string.format('%.4f', price) .. ':'
        .. string.format('%.4f', price) .. ':' .. string.format('%.4f', price) .. ':'
        .. string.format('%.4f', price) .. ':1'
    redis.call('ZADD', KEYS[1], bucket, member)
    redis.call('ZREMRANGEBYRANK', KEYS[1], 0, -501)
    return 0
end
`

const PeriodIndicatorLuaScript = `
-- KEYS:
--   1 = kline:{code}:{period}       (sortedset of OHLC)
--   2 = stock:{code}:{period}:state  (hash for calc state)
--   3 = stock:{code}:{period}:MA5    (zset)
--   4 = stock:{code}:{period}:MA10   (zset)
--   5 = stock:{code}:{period}:MACD   (zset)
--   6 = stock:{code}:{period}:KDJ    (zset)
--   7 = stock:{code}:{period}:RSI    (zset)
--
-- ARGV:
--   1 = period_seconds (as string)
--   2 = bucket_timestamp_ms (as string)

local period = tonumber(ARGV[1])
local bucket_ms = tonumber(ARGV[2])

local existing = redis.call('ZSCORE', KEYS[1], bucket_ms / 1000)
if not existing then
    return -1
end

local parts = {}
for p in string.gmatch(existing, '[^:]+') do
    table.insert(parts, p)
end
local close_price = tonumber(parts[5])
local ts_str = parts[1]

local state = redis.call('HMGET', KEYS[2],
    'ema12', 'ema26', 'signal', 'k', 'd',
    'avg_gain', 'avg_loss', 'prev_close', 'count',
    'last_tick_ts', 'gap_fill_count', 'data_insufficient',
    'total_real', 'recovery_count')

local ema12           = tonumber(state[1])  or 0
local ema26           = tonumber(state[2])  or 0
local signal          = tonumber(state[3])  or 0
local k               = tonumber(state[4])  or 50
local d               = tonumber(state[5])  or 50
local avg_gain        = tonumber(state[6])  or 0
local avg_loss        = tonumber(state[7])  or 0
local prev_close      = tonumber(state[8])  or 0
local count           = tonumber(state[9])  or 0
local last_tick_ts    = tonumber(state[10]) or 0
local gap_fill_count  = tonumber(state[11]) or 0
local data_insufficient = tonumber(state[12]) or 0
local total_real      = tonumber(state[13]) or 0
local recovery_count  = tonumber(state[14]) or 0

count = count + 1
total_real = total_real + 1

local price = close_price

local EXPECTED_INTERVAL = period * 1000
local MAX_NORMAL_GAP    = EXPECTED_INTERVAL * 4
local MAX_FILL_POINTS   = 20
local RECOVERY_THRESHOLD = 26

if last_tick_ts ~= 0 then
    local gap = bucket_ms - last_tick_ts
    if gap > MAX_NORMAL_GAP then
        local missed = math.floor(gap / EXPECTED_INTERVAL)
        local remain_fill = MAX_FILL_POINTS - gap_fill_count
        if remain_fill < 0 then remain_fill = 0 end
        local to_fill = missed
        if to_fill > remain_fill then to_fill = remain_fill end
        if to_fill < 0 then to_fill = 0 end

        for i = 1, to_fill do
            redis.call('LPUSH', KEYS[2] .. ':prices', prev_close)
        end
        if to_fill > 0 then
            redis.call('LTRIM', KEYS[2] .. ':prices', 0, 199)
        end

        gap_fill_count = gap_fill_count + to_fill

        if gap_fill_count >= MAX_FILL_POINTS then
            data_insufficient = 1
        end
    else
        if gap_fill_count > 0 then
            gap_fill_count = gap_fill_count - 1
            if gap_fill_count < 0 then gap_fill_count = 0 end
        end
    end
end

redis.call('LPUSH', KEYS[2] .. ':prices', price)
redis.call('LTRIM', KEYS[2] .. ':prices', 0, 199)

if count == 1 then
    ema12 = price
    ema26 = price
else
    local k12 = 2 / 13
    local k26 = 2 / 27
    ema12 = price * k12 + ema12 * (1 - k12)
    ema26 = price * k26 + ema26 * (1 - k26)
end
local dif = ema12 - ema26
if count == 1 then
    signal = dif
else
    local k9 = 2 / 10
    signal = dif * k9 + signal * (1 - k9)
end

if count >= 9 then
    local closes9 = redis.call('LRANGE', KEYS[2] .. ':prices', 0, 8)
    local highest = tonumber(closes9[1])
    local lowest  = tonumber(closes9[1])
    for i = 2, 9 do
        local p = tonumber(closes9[i])
        if p > highest then highest = p end
        if p < lowest  then lowest  = p end
    end
    local rsv = 50
    if highest ~= lowest then
        rsv = (price - lowest) / (highest - lowest) * 100
    end
    k = (2 * k + rsv) / 3
    d = (2 * d + k) / 3
end

if count >= 2 then
    local diff = price - prev_close
    local gain = 0
    local loss = 0
    if diff > 0 then
        gain = diff
    elseif diff < 0 then
        loss = -diff
    end
    if count <= 15 then
        avg_gain = (avg_gain * (count - 2) + gain) / (count - 1)
        avg_loss = (avg_loss * (count - 2) + loss) / (count - 1)
    else
        avg_gain = (avg_gain * 13 + gain) / 14
        avg_loss = (avg_loss * 13 + loss) / 14
    end
end

if data_insufficient == 1 then
    recovery_count = recovery_count + 1
    if recovery_count >= RECOVERY_THRESHOLD then
        data_insufficient = 0
        recovery_count = 0
        gap_fill_count = 0
    end
end

if data_insufficient == 0 then
    if count >= 5 then
        local closes5 = redis.call('LRANGE', KEYS[2] .. ':prices', 0, 4)
        local sum5 = 0
        for i = 1, 5 do sum5 = sum5 + tonumber(closes5[i]) end
        local ma5 = sum5 / 5
        redis.call('ZADD', KEYS[3], bucket_ms,
            ts_str .. ':' .. string.format('%.4f', ma5))
        redis.call('ZREMRANGEBYRANK', KEYS[3], 0, -101)
    end

    if count >= 10 then
        local closes10 = redis.call('LRANGE', KEYS[2] .. ':prices', 0, 9)
        local sum10 = 0
        for i = 1, 10 do sum10 = sum10 + tonumber(closes10[i]) end
        local ma10 = sum10 / 10
        redis.call('ZADD', KEYS[4], bucket_ms,
            ts_str .. ':' .. string.format('%.4f', ma10))
        redis.call('ZREMRANGEBYRANK', KEYS[4], 0, -101)
    end

    if count >= 26 then
        local macd_val = (dif - signal) * 2
        redis.call('ZADD', KEYS[5], bucket_ms,
            ts_str .. ':' .. string.format('%.6f', dif) .. ':'
                  .. string.format('%.6f', signal) .. ':'
                  .. string.format('%.6f', macd_val))
        redis.call('ZREMRANGEBYRANK', KEYS[5], 0, -101)
    end

    if count >= 9 then
        local j = 3 * k - 2 * d
        redis.call('ZADD', KEYS[6], bucket_ms,
            ts_str .. ':' .. string.format('%.4f', k) .. ':'
                  .. string.format('%.4f', d) .. ':'
                  .. string.format('%.4f', j))
        redis.call('ZREMRANGEBYRANK', KEYS[6], 0, -101)
    end

    if count >= 15 then
        local rs = 100
        if avg_loss > 0 then
            rs = avg_gain / avg_loss
        end
        local rsi = 100 - 100 / (1 + rs)
        redis.call('ZADD', KEYS[7], bucket_ms,
            ts_str .. ':' .. string.format('%.4f', rsi))
        redis.call('ZREMRANGEBYRANK', KEYS[7], 0, -101)
    end
end

redis.call('HMSET', KEYS[2],
    'ema12',            ema12,
    'ema26',            ema26,
    'signal',           signal,
    'k',                k,
    'd',                d,
    'avg_gain',         avg_gain,
    'avg_loss',         avg_loss,
    'prev_close',       price,
    'count',            count,
    'last_tick_ts',     bucket_ms,
    'gap_fill_count',   gap_fill_count,
    'data_insufficient', data_insufficient,
    'total_real',       total_real,
    'recovery_count',   recovery_count)

return count
`
