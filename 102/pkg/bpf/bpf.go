package bpf

import (
	"encoding/binary"
	"fmt"
	"net"
	"os"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/link"
	"github.com/cilium/ebpf/ringbuf"
	"github.com/cilium/ebpf/rlimit"
)

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go -cc clang -cflags "-O2 -target bpf -D__TARGET_ARCH_x86" bpf ../../bpf/xdp_k8s_accel.c -- -I../../bpf

type RuleKey struct {
	SrcIP    uint32
	DstIP    uint32
	SrcPort  uint16
	DstPort  uint16
	Protocol uint8
}

type RuleValue struct {
	Action uint8
	RuleID uint32
}

type Stats struct {
	PacketsProcessed uint64
	PacketsAllowed   uint64
	PacketsDenied    uint64
	BytesProcessed   uint64
}

type LogEvent struct {
	RuleID   uint32
	Action   uint8
	SrcIP    uint32
	DstIP    uint32
	SrcPort  uint16
	DstPort  uint16
	Protocol uint8
}

type XDPAccel struct {
	objs     *bpfObjects
	link     link.Link
	ifaceIdx int
}

func NewXDPAccel(iface string) (*XDPAccel, error) {
	if err := rlimit.RemoveMemlock(); err != nil {
		return nil, fmt.Errorf("remove memlock: %w", err)
	}

	ifaceIdx, err := net.InterfaceByName(iface)
	if err != nil {
		return nil, fmt.Errorf("get interface %s: %w", iface, err)
	}

	objs := bpfObjects{}
	if err := loadBpfObjects(&objs, nil); err != nil {
		return nil, fmt.Errorf("load bpf objects: %w", err)
	}

	link, err := link.AttachXDP(link.XDPOptions{
		Program:   objs.XdpK8sAccelProg,
		Interface: ifaceIdx.Index,
	})
	if err != nil {
		objs.Close()
		return nil, fmt.Errorf("attach xdp: %w", err)
	}

	accel := &XDPAccel{
		objs:     &objs,
		link:     link,
		ifaceIdx: ifaceIdx.Index,
	}

	if err := accel.initMaps(); err != nil {
		accel.Close()
		return nil, err
	}

	return accel, nil
}

func (a *XDPAccel) initMaps() error {
	key := uint32(0)
	stats := Stats{}
	if err := a.objs.Stats.Put(&key, &stats); err != nil {
		return fmt.Errorf("init stats map: %w", err)
	}

	counter := uint64(0)
	if err := a.objs.LogCounter.Put(&key, &counter); err != nil {
		return fmt.Errorf("init log counter map: %w", err)
	}

	return nil
}

func (a *XDPAccel) AddRule(key RuleKey, value RuleValue) error {
	return a.objs.Rules.Put(&key, &value)
}

func (a *XDPAccel) DeleteRule(key RuleKey) error {
	return a.objs.Rules.Delete(&key)
}

func (a *XDPAccel) GetStats() (Stats, error) {
	key := uint32(0)
	var stats Stats
	if err := a.objs.Stats.Lookup(&key, &stats); err != nil {
		return Stats{}, err
	}
	return stats, nil
}

func (a *XDPAccel) ReadEvents() (<-chan LogEvent, error) {
	rd, err := ringbuf.NewReader(a.objs.Events)
	if err != nil {
		return nil, err
	}

	ch := make(chan LogEvent, 100)
	go func() {
		defer rd.Close()
		for {
			record, err := rd.Read()
			if err != nil {
				if err == ringbuf.ErrClosed {
					close(ch)
					return
				}
				continue
			}

			var event LogEvent
			if len(record.RawSample) >= binary.Size(event) {
				event.RuleID = binary.LittleEndian.Uint32(record.RawSample[0:4])
				event.Action = record.RawSample[4]
				event.SrcIP = binary.BigEndian.Uint32(record.RawSample[5:9])
				event.DstIP = binary.BigEndian.Uint32(record.RawSample[9:13])
				event.SrcPort = binary.BigEndian.Uint16(record.RawSample[13:15])
				event.DstPort = binary.BigEndian.Uint16(record.RawSample[15:17])
				event.Protocol = record.RawSample[17]
				ch <- event
			}
		}
	}()

	return ch, nil
}

func (a *XDPAccel) Close() error {
	if a.link != nil {
		a.link.Close()
	}
	if a.objs != nil {
		a.objs.Close()
	}
	return nil
}

func IPToUint32(ip net.IP) uint32 {
	if ip4 := ip.To4(); ip4 != nil {
		return binary.BigEndian.Uint32(ip4)
	}
	return 0
}

func Uint32ToIP(u uint32) net.IP {
	ip := make(net.IP, 4)
	binary.BigEndian.PutUint32(ip, u)
	return ip
}
