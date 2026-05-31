package main

const IndicatorLuaScript = `
-- KEYS:
--   1 = stock:{code}:prices   (list of price strings)
--   2 = stock:{code}:state    (hash for calculation state)
--   3 = stock:{code}:MA5      (zset)
--   4 = stock:{code}:MA10     (zset)
--   5 = stock:{code}:MACD     (zset)
--   6 = stock:{code}:KDJ      (zset)
--   7 = stock:{code}:RSI      (zset)
--
-- ARGV:
--   1 = timestamp (ms, as string)
--   2 = price (as string)

local ts = ARGV[1]
local price = tonumber(ARGV[2])
local ts_num = tonumber(ts)

-- ====== 1. Read extended state ======
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

-- ====== 2. Data gap detection and filling ======
local EXPECTED_INTERVAL = 500
local MAX_NORMAL_GAP    = 2000
local MAX_FILL_POINTS   = 20
local RECOVERY_THRESHOLD = 26

if last_tick_ts ~= 0 then
    local gap = ts_num - last_tick_ts
    if gap > MAX_NORMAL_GAP then
        local missed = math.floor(gap / EXPECTED_INTERVAL)
        local remain_fill = MAX_FILL_POINTS - gap_fill_count
        if remain_fill < 0 then remain_fill = 0 end
        local to_fill = missed
        if to_fill > remain_fill then to_fill = remain_fill end
        if to_fill < 0 then to_fill = 0 end

        for i = 1, to_fill do
            redis.call('LPUSH', KEYS[1], prev_close)
        end
        if to_fill > 0 then
            redis.call('LTRIM', KEYS[1], 0, 199)
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

total_real = total_real + 1

-- ====== 3. Push current price ======
redis.call('LPUSH', KEYS[1], price)
redis.call('LTRIM', KEYS[1], 0, 199)

count = count + 1

-- ====== 4. Update internal state (always, even when insufficient) ======
-- MACD state
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

-- KDJ state
if count >= 9 then
    local prices9 = redis.call('LRANGE', KEYS[1], 0, 8)
    local highest = tonumber(prices9[1])
    local lowest  = tonumber(prices9[1])
    for i = 2, 9 do
        local p = tonumber(prices9[i])
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

-- RSI state
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

-- ====== 5. Check recovery ======
if data_insufficient == 1 then
    recovery_count = recovery_count + 1
    if recovery_count >= RECOVERY_THRESHOLD then
        data_insufficient = 0
        recovery_count = 0
        gap_fill_count = 0
    end
end

-- ====== 6. Emit indicators only when data is sufficient ======
if data_insufficient == 0 then

    -- MA5
    if count >= 5 then
        local prices5 = redis.call('LRANGE', KEYS[1], 0, 4)
        local sum5 = 0
        for i = 1, 5 do sum5 = sum5 + tonumber(prices5[i]) end
        local ma5 = sum5 / 5
        redis.call('ZADD', KEYS[3], ts_num,
            ts .. ':' .. string.format('%.4f', ma5))
        redis.call('ZREMRANGEBYRANK', KEYS[3], 0, -101)
    end

    -- MA10
    if count >= 10 then
        local prices10 = redis.call('LRANGE', KEYS[1], 0, 9)
        local sum10 = 0
        for i = 1, 10 do sum10 = sum10 + tonumber(prices10[i]) end
        local ma10 = sum10 / 10
        redis.call('ZADD', KEYS[4], ts_num,
            ts .. ':' .. string.format('%.4f', ma10))
        redis.call('ZREMRANGEBYRANK', KEYS[4], 0, -101)
    end

    -- MACD
    if count >= 26 then
        local macd_val = (dif - signal) * 2
        redis.call('ZADD', KEYS[5], ts_num,
            ts .. ':' .. string.format('%.6f', dif) .. ':'
                  .. string.format('%.6f', signal) .. ':'
                  .. string.format('%.6f', macd_val))
        redis.call('ZREMRANGEBYRANK', KEYS[5], 0, -101)
    end

    -- KDJ
    if count >= 9 then
        local j = 3 * k - 2 * d
        redis.call('ZADD', KEYS[6], ts_num,
            ts .. ':' .. string.format('%.4f', k) .. ':'
                  .. string.format('%.4f', d) .. ':'
                  .. string.format('%.4f', j))
        redis.call('ZREMRANGEBYRANK', KEYS[6], 0, -101)
    end

    -- RSI
    if count >= 15 then
        local rs = 100
        if avg_loss > 0 then
            rs = avg_gain / avg_loss
        end
        local rsi = 100 - 100 / (1 + rs)
        redis.call('ZADD', KEYS[7], ts_num,
            ts .. ':' .. string.format('%.4f', rsi))
        redis.call('ZREMRANGEBYRANK', KEYS[7], 0, -101)
    end
end

-- ====== 7. Persist all state ======
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
    'last_tick_ts',     ts_num,
    'gap_fill_count',   gap_fill_count,
    'data_insufficient', data_insufficient,
    'total_real',       total_real,
    'recovery_count',   recovery_count)

return count
`
