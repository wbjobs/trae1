package main

import (
	"context"
	"fmt"
	"log"
	"strconv"
	"sync"
	"sync/atomic"
	"time"

	"github.com/redis/go-redis/v9"
)

type KLineAggregator struct {
	redisCli      *RedisClient
	periodMgr     *PeriodManager
	tickChan      chan *Tick
	aggregateSha  string
	indicatorSha  string
	stats         *KLineStats
	batchSize     int
	batchInt      time.Duration
	done          chan struct{}
	processedBkt  sync.Map
}

type KLineStats struct {
	TicksProcessed  atomic.Int64
	KLinesUpdated   atomic.Int64
	KLinesCreated   atomic.Int64
	IndicatorsCalc  atomic.Int64
	LuaErrors       atomic.Int64
}

func NewKLineAggregator(redisCli *RedisClient, periodMgr *PeriodManager, tickChan chan *Tick,
	batchSize, batchIntervalMs int) *KLineAggregator {
	return &KLineAggregator{
		redisCli:  redisCli,
		periodMgr: periodMgr,
		tickChan:  tickChan,
		stats:     &KLineStats{},
		batchSize: batchSize,
		batchInt:  time.Duration(batchIntervalMs) * time.Millisecond,
		done:      make(chan struct{}),
	}
}

func (ka *KLineAggregator) Start(ctx context.Context) {
	aggSha, err := ka.redisCli.ScriptLoad(ctx, KLineAggregateLuaScript)
	if err != nil {
		log.Fatalf("Failed to load K-line aggregate script: %v", err)
	}
	ka.aggregateSha = aggSha
	log.Printf("K-line aggregate Lua script loaded, SHA: %s", aggSha)

	indSha, err := ka.redisCli.ScriptLoad(ctx, PeriodIndicatorLuaScript)
	if err != nil {
		log.Fatalf("Failed to load period indicator script: %v", err)
	}
	ka.indicatorSha = indSha
	log.Printf("Period indicator Lua script loaded, SHA: %s", indSha)

	go ka.aggregateLoop(ctx)
	go ka.statsReporter()
}

func (ka *KLineAggregator) aggregateLoop(ctx context.Context) {
	ticker := time.NewTicker(ka.batchInt)
	defer ticker.Stop()

	batch := make([]*Tick, 0, ka.batchSize)

	for {
		select {
		case <-ctx.Done():
			close(ka.done)
			return
		case tick := <-ka.tickChan:
			batch = append(batch, tick)
			if len(batch) >= ka.batchSize {
				ka.flushAggregate(ctx, batch)
				batch = batch[:0]
			}
		case <-ticker.C:
			if len(batch) > 0 {
				ka.flushAggregate(ctx, batch)
				batch = batch[:0]
			}
		}
	}
}

func (ka *KLineAggregator) flushAggregate(ctx context.Context, ticks []*Tick) {
	ka.stats.TicksProcessed.Add(int64(len(ticks)))

	periods := ka.periodMgr.GetPeriods()
	if len(periods) == 0 {
		return
	}

	for _, period := range periods {
		pipe := ka.redisCli.NewPipeline()
		created := int64(0)
		updated := int64(0)

		for _, tick := range ticks {
			klineKey := fmt.Sprintf("kline:%s:%d", tick.Code, period)
			args := []interface{}{
				strconv.FormatInt(period, 10),
				strconv.FormatInt(tick.Timestamp, 10),
				strconv.FormatFloat(tick.Price, 'f', -1, 64),
			}
			pipe.EvalSha(ctx, ka.aggregateSha, []string{klineKey}, args...)
		}

		results, err := pipe.Exec(ctx)
		if err != nil {
			ka.stats.LuaErrors.Add(int64(len(ticks)))
			log.Printf("K-line aggregate pipeline error (period %d): %v", period, err)
			continue
		}

		for _, result := range results {
			if result.Err() != nil {
				ka.stats.LuaErrors.Add(1)
			} else {
				if cmd, ok := result.(*redis.Cmd); ok {
					if raw, err := cmd.Int64(); err == nil {
						if raw == 0 {
							created++
						} else {
							updated++
						}
					}
				}
			}
		}

		ka.stats.KLinesCreated.Add(created)
		ka.stats.KLinesUpdated.Add(updated)

		ka.calcPeriodIndicators(ctx, ticks, period)
	}
}

func (ka *KLineAggregator) calcPeriodIndicators(ctx context.Context, ticks []*Tick, period int64) {
	pipe := ka.redisCli.NewPipeline()
	periodStr := strconv.FormatInt(period, 10)

	for _, tick := range ticks {
		tsSec := tick.Timestamp / 1000
		bucket := (tsSec / period) * period
		bucketMs := bucket * 1000

		bucketKey := fmt.Sprintf("%s:%d", tick.Code, bucketMs)
		_, loaded := ka.processedBkt.LoadOrStore(bucketKey, true)
		if loaded {
			continue
		}

		keys := []string{
			fmt.Sprintf("kline:%s:%d", tick.Code, period),
			fmt.Sprintf("stock:%s:%d:state", tick.Code, period),
			fmt.Sprintf("stock:%s:%d:MA5", tick.Code, period),
			fmt.Sprintf("stock:%s:%d:MA10", tick.Code, period),
			fmt.Sprintf("stock:%s:%d:MACD", tick.Code, period),
			fmt.Sprintf("stock:%s:%d:KDJ", tick.Code, period),
			fmt.Sprintf("stock:%s:%d:RSI", tick.Code, period),
		}
		args := []interface{}{
			periodStr,
			strconv.FormatInt(bucketMs, 10),
		}
		pipe.EvalSha(ctx, ka.indicatorSha, keys, args...)
	}

	results, err := pipe.Exec(ctx)
	if err != nil {
		ka.stats.LuaErrors.Add(int64(len(ticks)))
		log.Printf("Period indicator pipeline error (period %d): %v", period, err)
		return
	}

	successCount := int64(0)
	for _, result := range results {
		if result.Err() != nil {
			ka.stats.LuaErrors.Add(1)
		} else {
			if cmd, ok := result.(*redis.Cmd); ok {
				if raw, err := cmd.Int64(); err == nil && raw >= 0 {
					successCount++
				}
			}
		}
	}
	ka.stats.IndicatorsCalc.Add(successCount)
}

func (ka *KLineAggregator) statsReporter() {
	ticker := time.NewTicker(15 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ticker.C:
			log.Printf("KLine Stats: ticks=%d, klines_created=%d, klines_updated=%d, indicators=%d, errors=%d",
				ka.stats.TicksProcessed.Load(),
				ka.stats.KLinesCreated.Load(),
				ka.stats.KLinesUpdated.Load(),
				ka.stats.IndicatorsCalc.Load(),
				ka.stats.LuaErrors.Load())
		case <-ka.done:
			return
		}
	}
}

func (ka *KLineAggregator) GetStats() *KLineStats {
	return ka.stats
}

func (ka *KLineAggregator) RecomputePeriod(ctx context.Context, code string, period int64, startTs, endTs int64) error {
	klineKey := fmt.Sprintf("kline:%s:%d", code, period)

	var minScore, maxScore string
	if startTs > 0 {
		minScore = strconv.FormatInt(startTs/1000, 10)
	} else {
		minScore = "-inf"
	}
	if endTs > 0 {
		maxScore = strconv.FormatInt(endTs/1000, 10)
	} else {
		maxScore = "+inf"
	}

	members, err := ka.redisCli.ZRangeByScore(ctx, klineKey, minScore, maxScore, 0, 500)
	if err != nil {
		return err
	}

	periodStr := strconv.FormatInt(period, 10)
	stateKey := fmt.Sprintf("stock:%s:%d:state", code, period)
	ka.redisCli.GetClient().Del(ctx,
		stateKey,
		stateKey+":prices",
		fmt.Sprintf("stock:%s:%d:MA5", code, period),
		fmt.Sprintf("stock:%s:%d:MA10", code, period),
		fmt.Sprintf("stock:%s:%d:MACD", code, period),
		fmt.Sprintf("stock:%s:%d:KDJ", code, period),
		fmt.Sprintf("stock:%s:%d:RSI", code, period),
	)

	pipe := ka.redisCli.NewPipeline()
	for _, member := range members {
		parts := splitMember(member)
		if len(parts) < 2 {
			continue
		}
		bucketSec, _ := strconv.ParseInt(parts[0], 10, 64)
		bucketMs := bucketSec * 1000

		keys := []string{
			klineKey,
			stateKey,
			fmt.Sprintf("stock:%s:%d:MA5", code, period),
			fmt.Sprintf("stock:%s:%d:MA10", code, period),
			fmt.Sprintf("stock:%s:%d:MACD", code, period),
			fmt.Sprintf("stock:%s:%d:KDJ", code, period),
			fmt.Sprintf("stock:%s:%d:RSI", code, period),
		}
		args := []interface{}{periodStr, strconv.FormatInt(bucketMs, 10)}
		pipe.EvalSha(ctx, ka.indicatorSha, keys, args...)
	}

	_, err = pipe.Exec(ctx)
	return err
}

func splitMember(s string) []string {
	result := make([]string, 0, 8)
	start := 0
	for i := 0; i < len(s); i++ {
		if s[i] == ':' {
			result = append(result, s[start:i])
			start = i + 1
		}
	}
	result = append(result, s[start:])
	return result
}
