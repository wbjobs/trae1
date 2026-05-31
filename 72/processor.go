package main

import (
	"context"
	"fmt"
	"log"
	"strconv"
	"sync/atomic"
	"time"
)

type Processor struct {
	redisCli     *RedisClient
	tickChan     chan *Tick
	klineChan    chan *Tick
	scriptSha    string
	batchSize    int
	batchInt     time.Duration
	stats        *ProcStats
	done         chan struct{}
}

type ProcStats struct {
	TicksProcessed atomic.Int64
	LuaErrors      atomic.Int64
	BatchCount     atomic.Int64
	AvgLatencyUs   atomic.Int64
}

func NewProcessor(redisCli *RedisClient, tickChan chan *Tick, klineChan chan *Tick,
	batchSize int, batchIntervalMs int) *Processor {
	return &Processor{
		redisCli:  redisCli,
		tickChan:  tickChan,
		klineChan: klineChan,
		batchSize: batchSize,
		batchInt:  time.Duration(batchIntervalMs) * time.Millisecond,
		stats:     &ProcStats{},
		done:      make(chan struct{}),
	}
}

func (p *Processor) Start(ctx context.Context) {
	scriptSha, err := p.redisCli.ScriptLoad(ctx, IndicatorLuaScript)
	if err != nil {
		log.Fatalf("Failed to load Lua script: %v", err)
	}
	p.scriptSha = scriptSha
	log.Printf("Tick indicator Lua script loaded, SHA: %s", scriptSha)

	go p.processLoop(ctx)
	go p.statsReporter()
}

func (p *Processor) processLoop(ctx context.Context) {
	ticker := time.NewTicker(p.batchInt)
	defer ticker.Stop()

	batch := make([]*Tick, 0, p.batchSize)

	for {
		select {
		case <-ctx.Done():
			close(p.done)
			return
		case tick := <-p.tickChan:
			if p.klineChan != nil {
				select {
				case p.klineChan <- tick:
				default:
				}
			}
			batch = append(batch, tick)
			if len(batch) >= p.batchSize {
				p.flushBatch(ctx, batch)
				batch = batch[:0]
			}
		case <-ticker.C:
			if len(batch) > 0 {
				p.flushBatch(ctx, batch)
				batch = batch[:0]
			}
		}
	}
}

func (p *Processor) flushBatch(ctx context.Context, ticks []*Tick) {
	start := time.Now()
	p.stats.BatchCount.Add(1)

	pipe := p.redisCli.NewPipeline()

	for _, tick := range ticks {
		keys := buildIndicatorKeys(tick.Code)
		args := []interface{}{strconv.FormatInt(tick.Timestamp, 10), strconv.FormatFloat(tick.Price, 'f', -1, 64)}
		pipe.EvalSha(ctx, p.scriptSha, keys, args...)
	}

	results, err := pipe.Exec(ctx)
	if err != nil {
		p.stats.LuaErrors.Add(int64(len(ticks)))
		log.Printf("Pipeline exec error: %v", err)
		return
	}

	successCount := int64(0)
	for i, result := range results {
		if result.Err() != nil {
			p.stats.LuaErrors.Add(1)
			log.Printf("Lua eval error for tick %d (%s): %v", i, ticks[i].Code, result.Err())
		} else {
			successCount++
		}
	}

	p.stats.TicksProcessed.Add(successCount)
	elapsed := time.Since(start)
	latencyUs := elapsed.Microseconds() / int64(len(ticks))
	if latencyUs > 0 {
		p.stats.AvgLatencyUs.Store(latencyUs)
	}
}

func buildIndicatorKeys(code string) []string {
	return []string{
		fmt.Sprintf("stock:%s:prices", code),
		fmt.Sprintf("stock:%s:state", code),
		fmt.Sprintf("stock:%s:MA5", code),
		fmt.Sprintf("stock:%s:MA10", code),
		fmt.Sprintf("stock:%s:MACD", code),
		fmt.Sprintf("stock:%s:KDJ", code),
		fmt.Sprintf("stock:%s:RSI", code),
	}
}

func (p *Processor) statsReporter() {
	ticker := time.NewTicker(10 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ticker.C:
			log.Printf("Tick Stats: processed=%d, batches=%d, errors=%d, avg_latency_us=%d",
				p.stats.TicksProcessed.Load(),
				p.stats.BatchCount.Load(),
				p.stats.LuaErrors.Load(),
				p.stats.AvgLatencyUs.Load())
		case <-p.done:
			return
		}
	}
}

func (p *Processor) GetStats() *ProcStats {
	return p.stats
}
