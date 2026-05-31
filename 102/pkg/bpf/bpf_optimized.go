package bpf

import (
	"encoding/binary"
	"fmt"
	"net"
	"sync"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/asm"
	"github.com/cilium/ebpf/link"
	"github.com/cilium/ebpf/ringbuf"
	"github.com/cilium/ebpf/rlimit"
)

const (
	MaxSubprogs     = 5
	RulesPerSubprog = 1000
)

type L1Key struct {
	DstIP uint32
}

type L1Value struct {
	L2MapFD   uint32
	RuleCount uint32
}

type L2Key struct {
	DstPort  uint16
	SrcPort  uint16
	Protocol uint8
}

type PerfTestResult struct {
	TotalPackets   uint64
	TotalLookups   uint64
	AvgLookupNS    uint64
	MaxLookupNS    uint64
	MinLookupNS    uint64
	Dropped        uint64
}

type XDPAccelOptimized struct {
	l1Table    *ebpf.Map
	l2Tables   []*ebpf.Map
	subprogFDs []ebpf.Program
	link       link.Link
	ifaceIdx   int
	mu         sync.RWMutex
	ruleCount  int
}

func NewXDPAccelOptimized(iface string) (*XDPAccelOptimized, error) {
	if err := rlimit.RemoveMemlock(); err != nil {
		return nil, fmt.Errorf("remove memlock: %w", err)
	}

	ifaceIdx, err := net.InterfaceByName(iface)
	if err != nil {
		return nil, fmt.Errorf("get interface %s: %w", iface, err)
	}

	l2Templates := make([]*ebpf.Map, MaxSubprogs)
	for i := 0; i < MaxSubprogs; i++ {
		l2Map, err := ebpf.NewMap(&ebpf.MapSpec{
			Type:       ebpf.Hash,
			KeySize:    8,
			ValueSize:  8,
			MaxEntries: RulesPerSubprog,
			Flags:      ebpf.MapFlag(0),
		})
		if err != nil {
			for j := 0; j < i; j++ {
				l2Templates[j].Close()
			}
			return nil, fmt.Errorf("create l2 map %d: %w", i, err)
		}
		l2Templates[i] = l2Map
	}

	l2ArrayMap, err := ebpf.NewMap(&ebpf.MapSpec{
		Type:       ebpf.ArrayOfMaps,
		KeySize:    4,
		ValueSize:  4,
		MaxEntries: MaxSubprogs,
		InnerMap:   l2Templates[0],
	})
	if err != nil {
		for i := 0; i < MaxSubprogs; i++ {
			l2Templates[i].Close()
		}
		return nil, fmt.Errorf("create l2 array: %w", err)
	}

	l1Map, err := ebpf.NewMap(&ebpf.MapSpec{
		Type:       ebpf.Hash,
		KeySize:    4,
		ValueSize:  8,
		MaxEntries: 10000,
	})
	if err != nil {
		l2ArrayMap.Close()
		for i := 0; i < MaxSubprogs; i++ {
			l2Templates[i].Close()
		}
		return nil, fmt.Errorf("create l1 map: %w", err)
	}

	specs, err := loadXDPSpecs()
	if err != nil {
		l1Map.Close()
		l2ArrayMap.Close()
		for i := 0; i < MaxSubprogs; i++ {
			l2Templates[i].Close()
		}
		return nil, fmt.Errorf("load xdp specs: %w", err)
	}

	prog, err := attachXDPDualProgram(specs, ifaceIdx.Index, l2Templates)
	if err != nil {
		l1Map.Close()
		l2ArrayMap.Close()
		for i := 0; i < MaxSubprogs; i++ {
			l2Templates[i].Close()
		}
		return nil, fmt.Errorf("attach xdp program: %w", err)
	}

	accel := &XDPAccelOptimized{
		l1Table:    l1Map,
		l2Tables:   l2Templates,
		subprogFDs: []ebpf.Program{prog},
		ifaceIdx:   ifaceIdx.Index,
	}

	if err := accel.initMaps(); err != nil {
		accel.Close()
		return nil, err
	}

	return accel, nil
}

func loadXDPSpecs() (map[string]*ebpf.Program, error) {
	specs := map[string]*ebpf.ProgramSpec{
		"xdp_dispatcher": {
			Name:     "xdp_dispatcher",
			Type:     ebpf.XDP,
			Flag:     ebpf.FBatch | ebpf.FReplace,
			AttachFn: asm.FD(),
		},
	}

	_ = specs
	return map[string]*ebpf.Program{}, nil
}

func attachXDPDualProgram(specs map[string]*ebpf.Program, ifaceIdx int, l2Maps []*ebpf.Map) (ebpf.Program, error) {
	return nil, nil
}

func (a *XDPAccelOptimized) initMaps() error {
	key := uint32(0)
	stats := Stats{}
	if err := a.l1Table.Put(&key, &stats); err != nil {
		return fmt.Errorf("init stats: %w", err)
	}

	counter := uint64(0)
	if err := a.l1Table.Put(&key, &counter); err != nil {
		return fmt.Errorf("init counter: %w", err)
	}

	return nil
}

func (a *XDPAccelOptimized) AddRule(dstIP uint32, dstPort, srcPort uint16, protocol uint8, ruleID uint32, action uint8) error {
	a.mu.Lock()
	defer a.mu.Unlock()

	subprogIdx := uint32(a.ruleCount / RulesPerSubprog)
	if subprogIdx >= MaxSubprogs {
		return fmt.Errorf("exceeds maximum subprog capacity")
	}

	l2Key := L2Key{
		DstPort:  dstPort,
		SrcPort:  srcPort,
		Protocol: protocol,
	}

	l2Map := a.l2Tables[subprogIdx]
	ruleValue := RuleValue{
		Action: action,
		RuleID: ruleID,
	}

	if err := l2Map.Put(&l2Key, &ruleValue); err != nil {
		return fmt.Errorf("add l2 rule: %w", err)
	}

	l1Key := L1Key{DstIP: dstIP}
	var l1Val L1Value
	if err := a.l1Table.Lookup(&l1Key, &l1Val); err == nil {
		if uint32(l1Val.RuleCount) >= RulesPerSubprog {
			return fmt.Errorf("subprog %d is full", subprogIdx)
		}
		l1Val.RuleCount++
	} else {
		l1Val.L2MapFD = subprogIdx
		l1Val.RuleCount = 1
	}

	if err := a.l1Table.Put(&l1Key, &l1Val); err != nil {
		return fmt.Errorf("add l1 rule: %w", err)
	}

	a.ruleCount++
	return nil
}

func (a *XDPAccelOptimized) DeleteRule(dstIP uint32, dstPort, srcPort uint16, protocol uint8) error {
	a.mu.Lock()
	defer a.mu.Unlock()

	var l1Val L1Value
	l1Key := L1Key{DstIP: dstIP}
	if err := a.l1Table.Lookup(&l1Key, &l1Val); err != nil {
		return fmt.Errorf("lookup l1: %w", err)
	}

	l2Map := a.l2Tables[l1Val.L2MapFD]
	l2Key := L2Key{
		DstPort:  dstPort,
		SrcPort:  srcPort,
		Protocol: protocol,
	}

	if err := l2Map.Delete(&l2Key); err != nil {
		return fmt.Errorf("delete l2 rule: %w", err)
	}

	l1Val.RuleCount--
	if l1Val.RuleCount == 0 {
		if err := a.l1Table.Delete(&l1Key); err != nil {
			return fmt.Errorf("delete l1 rule: %w", err)
		}
	} else {
		if err := a.l1Table.Put(&l1Key, &l1Val); err != nil {
			return fmt.Errorf("update l1 rule: %w", err)
		}
	}

	a.ruleCount--
	return nil
}

func (a *XDPAccelOptimized) GetStats() (Stats, error) {
	key := uint32(0)
	var stats Stats
	if err := a.l1Table.Lookup(&key, &stats); err != nil {
		return Stats{}, err
	}
	return stats, nil
}

func (a *XDPAccelOptimized) GetPerfResults() (PerfTestResult, error) {
	key := uint32(0)
	var result PerfTestResult
	if err := a.l1Table.Lookup(&key, &result); err != nil {
		return PerfTestResult{}, err
	}
	return result, nil
}

func (a *XDPAccelOptimized) ReadEvents() (<-chan LogEvent, error) {
	return nil, nil
}

func (a *XDPAccelOptimized) Close() error {
	a.mu.Lock()
	defer a.mu.Unlock()

	if a.link != nil {
		a.link.Close()
	}

	for _, prog := range a.subprogFDs {
		prog.Close()
	}

	if a.l1Table != nil {
		a.l1Table.Close()
	}

	for _, m := range a.l2Tables {
		if m != nil {
			m.Close()
		}
	}

	return nil
}

func (a *XDPAccelOptimized) RuleCount() int {
	a.mu.RLock()
	defer a.mu.RUnlock()
	return a.ruleCount
}
