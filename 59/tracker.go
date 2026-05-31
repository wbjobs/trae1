package main

import (
	"fmt"
	"net"
	"sync"
	"time"
)

type Proto uint8

const (
	ProtoTCP Proto = 6
	ProtoUDP Proto = 17
)

func (p Proto) String() string {
	switch p {
	case ProtoTCP:
		return "TCP"
	case ProtoUDP:
		return "UDP"
	}
	return fmt.Sprintf("Unknown(%d)", p)
}

type FlowKey struct {
	SrcIP   net.IP
	DstIP   net.IP
	DstPort uint16
	Proto   Proto
}

func (k FlowKey) String() string {
	return fmt.Sprintf("%s:%s:%d/%s", k.SrcIP, k.DstIP, k.DstPort, k.Proto)
}

type FlowStats struct {
	Packets   uint64
	Bytes     uint64
	Count     uint64
	FirstSeen time.Time
	LastSeen  time.Time
}

type flowMapKey struct {
	SrcIP   string
	DstIP   string
	DstPort uint16
	Proto   Proto
}

func toMapKey(k FlowKey) flowMapKey {
	return flowMapKey{
		SrcIP:   string(k.SrcIP.To4()),
		DstIP:   string(k.DstIP.To4()),
		DstPort: k.DstPort,
		Proto:   k.Proto,
	}
}

type FlowTracker struct {
	mu    sync.RWMutex
	flows map[flowMapKey]*FlowStats
}

func NewFlowTracker() *FlowTracker {
	return &FlowTracker{flows: make(map[flowMapKey]*FlowStats)}
}

func (t *FlowTracker) Observe(k FlowKey, bytes uint64, ts time.Time) {
	mk := toMapKey(k)
	t.mu.Lock()
	defer t.mu.Unlock()
	if st, ok := t.flows[mk]; ok {
		st.Packets++
		st.Bytes += bytes
		st.Count++
		st.LastSeen = ts
	} else {
		t.flows[mk] = &FlowStats{
			Packets:   1,
			Bytes:     bytes,
			Count:     1,
			FirstSeen: ts,
			LastSeen:  ts,
		}
	}
}

func (t *FlowTracker) Snapshot() map[flowMapKey]FlowStats {
	t.mu.RLock()
	defer t.mu.RUnlock()
	out := make(map[flowMapKey]FlowStats, len(t.flows))
	for mk, v := range t.flows {
		out[mk] = *v
	}
	return out
}

func (k flowMapKey) FlowKey() FlowKey {
	return FlowKey{
		SrcIP:   net.IP(k.SrcIP).To4(),
		DstIP:   net.IP(k.DstIP).To4(),
		DstPort: k.DstPort,
		Proto:   k.Proto,
	}
}
