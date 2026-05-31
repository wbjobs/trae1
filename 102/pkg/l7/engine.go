package l7

import (
	"context"
	"fmt"
	"log"
	"net"
	"sort"
	"sync"
	"sync/atomic"
	"time"
)

type Protocol int

const (
	ProtocolUnknown Protocol = 0
	ProtocolHTTP    Protocol = 1
	ProtocolGRPC    Protocol = 2
	ProtocolHTTP2   Protocol = 3
)

type Decision int

const (
	DecisionPass     Decision = 0
	DecisionDeny     Decision = 1
	DecisionRedirect Decision = 2
)

type L7Context struct {
	SrcIP      uint32
	DstIP      uint32
	SrcPort    uint16
	DstPort    uint16
	Protocol   uint8
	L7Protocol Protocol
	FlowID     uint64
	Timestamp  uint64

	HTTPHost string
	HTTPPath string
	GRPCMethod string
}

type L7Rule struct {
	DstIP      net.IP
	DstPort    uint16
	Protocol   uint8
	L7Protocol Protocol
	HTTPHost   string
	HTTPPath   string
	GRPCMethod string
	Action     uint8
	RuleID     uint32
	WasmPlugin string
}

type L7Decision struct {
	FlowID            uint64
	Decision          Decision
	Action            uint8
	RuleID            uint32
	ProcessingTimeNS   uint64
	HTTPHost          string
	HTTPPath          string
	GRPCMethod        string
}

type WasmPlugin interface {
	Execute(ctx *L7Context) (Decision, error)
	GetName() string
	Close() error
}

type LatencyStats struct {
	P50 uint64
	P90 uint64
	P99 uint64
}

type L7Stats struct {
	TotalRequests    uint64
	Allowed          uint64
	Denied           uint64
	Redirects        uint64
	Bypassed         uint64
	AvgLatencyNS     uint64
	P50LatencyNS     uint64
	P90LatencyNS     uint64
	P99LatencyNS     uint64
	WasmExecutions   uint64
	WasmExecTimeNS   uint64
	latencies        []uint64
	mu               sync.Mutex
}

type Engine struct {
	rules           map[uint32]*L7Rule
	plugins         map[string]WasmPlugin
	bypassTable     map[BypassKey]bool
	stats           *L7Stats
	decisionCh      chan *L7Context
	statsCh         chan *L7Decision
	wg              sync.WaitGroup
	ctx             context.Context
	cancel          context.CancelFunc
	latencyBuckets  []uint64
}

type BypassKey struct {
	SrcIP    uint32
	DstIP    uint32
	SrcPort  uint16
	DstPort  uint16
	Protocol uint8
}

func NewEngine() *Engine {
	ctx, cancel := context.WithCancel(context.Background())
	return &Engine{
		rules:          make(map[uint32]*L7Rule),
		plugins:        make(map[string]WasmPlugin),
		bypassTable:    make(map[BypassKey]bool),
		stats:          &L7Stats{},
		decisionCh:     make(chan *L7Context, 10000),
		statsCh:        make(chan *L7Decision, 10000),
		ctx:            ctx,
		cancel:         cancel,
		latencyBuckets: make([]uint64, 0, 1000000),
	}
}

func (e *Engine) Start() {
	e.wg.Add(2)
	go e.decisionProcessor()
	go e.statsCollector()

	log.Println("L7 Engine started")
}

func (e *Engine) Stop() {
	e.cancel()
	e.wg.Wait()

	for _, plugin := range e.plugins {
		plugin.Close()
	}

	log.Println("L7 Engine stopped")
}

func (e *Engine) AddRule(rule *L7Rule) error {
	e.stats.mu.Lock()
	defer e.stats.mu.Unlock()

	rule.RuleID = uint32(len(e.rules) + 1)
	e.rules[rule.RuleID] = rule

	return nil
}

func (e *Engine) DeleteRule(ruleID uint32) error {
	e.stats.mu.Lock()
	defer e.stats.mu.Unlock()

	delete(e.rules, ruleID)
	return nil
}

func (e *Engine) RegisterPlugin(name string, plugin WasmPlugin) error {
	e.plugins[name] = plugin
	log.Printf("Registered Wasm plugin: %s", name)
	return nil
}

func (e *Engine) AddBypass(key BypassKey) {
	atomic.StoreUint32((*uint32)(&key), 0)
	e.bypassTable[key] = true
}

func (e *Engine) RemoveBypass(key BypassKey) {
	delete(e.bypassTable, key)
}

func (e *Engine) IsBypassed(ctx *L7Context) bool {
	key := BypassKey{
		SrcIP:    ctx.SrcIP,
		DstIP:    ctx.DstIP,
		SrcPort:  ctx.SrcPort,
		DstPort:  ctx.DstPort,
		Protocol: ctx.Protocol,
	}

	if e.bypassTable[key] {
		return true
	}

	key.SrcPort = 0
	if e.bypassTable[key] {
		return true
	}

	key.DstPort = 0
	key.SrcPort = ctx.SrcPort
	if e.bypassTable[key] {
		return true
	}

	return false
}

func (e *Engine) ProcessL7Request(ctx *L7Context) *L7Decision {
	startTime := time.Now()
	decision := &L7Decision{
		FlowID:          ctx.FlowID,
		Decision:        DecisionPass,
		Action:          0,
		RuleID:          0,
		ProcessingTimeNS: uint64(time.Since(startTime).Nanoseconds()),
		HTTPHost:        ctx.HTTPHost,
		HTTPPath:        ctx.HTTPPath,
		GRPCMethod:      ctx.GRPCMethod,
	}

	if e.IsBypassed(ctx) {
		decision.Decision = DecisionPass
		decision.Action = 0
		atomic.AddUint64(&e.stats.Bypassed, 1)
		return decision
	}

	for _, rule := range e.rules {
		if e.matchRule(ctx, rule) {
			decision.RuleID = rule.RuleID

			if rule.WasmPlugin != "" {
				plugin, ok := e.plugins[rule.WasmPlugin]
				if ok {
					wasmDecision, err := plugin.Execute(ctx)
					decision.Decision = wasmDecision
					atomic.AddUint64(&e.stats.WasmExecutions, 1)
					if err != nil {
						log.Printf("Wasm plugin error: %v", err)
					}
				}
			} else {
				decision.Decision = Decision(decision.Action)
			}

			atomic.AddUint64(&e.stats.TotalRequests, 1)

			if decision.Decision == DecisionPass {
				decision.Action = 0
				atomic.AddUint64(&e.stats.Allowed, 1)
			} else {
				decision.Action = 1
				atomic.AddUint64(&e.stats.Denied, 1)
			}

			return decision
		}
	}

	decision.Decision = DecisionPass
	decision.Action = 0
	atomic.AddUint64(&e.stats.TotalRequests, 1)
	atomic.AddUint64(&e.stats.Allowed, 1)

	return decision
}

func (e *Engine) matchRule(ctx *L7Context, rule *L7Rule) bool {
	if rule.DstIP != nil && !rule.DstIP.Equal(net.IP(ctx.DstIP)) {
		return false
	}

	if rule.DstPort != 0 && rule.DstPort != ctx.DstPort {
		return false
	}

	if rule.Protocol != 0 && rule.Protocol != ctx.Protocol {
		return false
	}

	if rule.L7Protocol != 0 && rule.L7Protocol != ctx.L7Protocol {
		return false
	}

	if rule.L7Protocol == ProtocolHTTP || rule.L7Protocol == ProtocolHTTP2 {
		if rule.HTTPHost != "" && rule.HTTPHost != ctx.HTTPHost {
			return false
		}
		if rule.HTTPPath != "" && !matchPath(ctx.HTTPPath, rule.HTTPPath) {
			return false
		}
	}

	if rule.L7Protocol == ProtocolGRPC {
		if rule.GRPCMethod != "" && rule.GRPCMethod != ctx.GRPCMethod {
			return false
		}
	}

	return true
}

func matchPath(path, pattern string) bool {
	if pattern == "" {
		return true
	}
	if pattern == "*" {
		return true
	}
	if len(pattern) > 0 && pattern[len(pattern)-1] == '*' {
		prefix := pattern[:len(pattern)-1]
		return len(path) >= len(prefix) && path[:len(prefix)] == prefix
	}
	return path == pattern
}

func (e *Engine) decisionProcessor() {
	defer e.wg.Done()

	for {
		select {
		case <-e.ctx.Done():
			return
		case ctx := <-e.decisionCh:
			decision := e.ProcessL7Request(ctx)
			e.statsCh <- decision
		}
	}
}

func (e *Engine) statsCollector() {
	defer e.wg.Done()

	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-e.ctx.Done():
			return
		case <-ticker.C:
			e.updateLatencyStats()
		case decision := <-e.statsCh:
			latency := decision.ProcessingTimeNS
			e.stats.mu.Lock()
			e.stats.latencies = append(e.stats.latencies, latency)
			e.stats.AvgLatencyNS = (e.stats.AvgLatencyNS*(e.stats.TotalRequests-1) + latency) / e.stats.TotalRequests
			e.stats.mu.Unlock()
		}
	}
}

func (e *Engine) updateLatencyStats() {
	e.stats.mu.Lock()
	defer e.stats.mu.Unlock()

	if len(e.stats.latencies) == 0 {
		return
	}

	latencies := make([]uint64, len(e.stats.latencies))
	copy(latencies, e.stats.latencies)

	sort.Slice(latencies, func(i, j int) bool {
		return latencies[i] < latencies[j]
	})

	n := len(latencies)
	e.stats.P50LatencyNS = latencies[n*50/100]
	e.stats.P90LatencyNS = latencies[n*90/100]
	e.stats.P99LatencyNS = latencies[n*99/100]

	if len(e.stats.latencies) > 100000 {
		e.stats.latencies = e.stats.latencies[len(e.stats.latencies)-100000:]
	}
}

func (e *Engine) GetStats() L7Stats {
	return L7Stats{
		TotalRequests:  atomic.LoadUint64(&e.stats.TotalRequests),
		Allowed:        atomic.LoadUint64(&e.stats.Allowed),
		Denied:         atomic.LoadUint64(&e.stats.Denied),
		Redirects:      atomic.LoadUint64(&e.stats.Redirects),
		Bypassed:       atomic.LoadUint64(&e.stats.Bypassed),
		AvgLatencyNS:   atomic.LoadUint64(&e.stats.AvgLatencyNS),
		P50LatencyNS:   e.stats.P50LatencyNS,
		P90LatencyNS:   e.stats.P90LatencyNS,
		P99LatencyNS:   e.stats.P99LatencyNS,
		WasmExecutions: atomic.LoadUint64(&e.stats.WasmExecutions),
	}
}

func (e *Engine) SubmitRequest(ctx *L7Context) {
	select {
	case e.decisionCh <- ctx:
	default:
		log.Printf("L7 request queue full, dropping request")
	}
}
