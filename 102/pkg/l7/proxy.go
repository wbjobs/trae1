package l7

import (
	"context"
	"fmt"
	"log"
	"net"
	"os"
	"sync"
	"syscall"
	"unsafe"

	"github.com/xdp-k8s-accel/pkg/bpf"
)

type Proxy struct {
	xdpAccel     *bpf.XDPAccelOptimized
	l7Engine     *Engine
	socketFD     int
	umem         *UmemRegion
	queueID      int
	ctx          context.Context
	cancel       context.CancelFunc
	wg           sync.WaitGroup
	stats        ProxyStats
}

type ProxyStats struct {
	TotalPackets    uint64
	ProcessedL7     uint64
	Redirected      uint64
	Dropped         uint64
	AvgLatencyNS    uint64
}

type UmemRegion struct {
	addr     uintptr
	size     uint64
	headroom uint64
	frameSize uint64
}

const (
	AF_XDP    = 44
	XDP_FLAGS = 0x02 | 0x04

	XSK_REGPopulate        = 0
	XSK_REGProduce         = 1
	XSK_REGProduceCompleted = 2

	UMEM_DEFAULT_FRAME_SIZE = 4096
	UMEM_DEFAULT_HEADROOM   = 0
	UMEM_DEFAULT_FILL_SIZE  = 4096
	UMEM_DEFAULT_COMP_SIZE  = 4096
)

func NewProxy(xdpAccel *bpf.XDPAccelOptimized, l7Engine *Engine, ifname string, queueID int) (*Proxy, error) {
	ctx, cancel := context.WithCancel(context.Background())

	proxy := &Proxy{
		xdpAccel: xdpAccel,
		l7Engine: l7Engine,
		queueID:  queueID,
		ctx:      ctx,
		cancel:   cancel,
	}

	if err := proxy.setupXSK(ifname); err != nil {
		cancel()
		return nil, err
	}

	return proxy, nil
}

func (p *Proxy) setupXSK(ifname string) error {
	sockFD, err := syscall.Socket(AF_XDP, syscall.SOCK_STREAM, 0)
	if err != nil {
		return fmt.Errorf("create xsk socket: %w", err)
	}

	if err := syscall.SetsockoptInt(sockFD, syscall.SOL_XDP, XSK_REGPopulate, 1); err != nil {
		syscall.Close(sockFD)
		return fmt.Errorf("set socket option: %w", err)
	}

	umemAddr, err := syscall.Mmap(0, 128*1024*1024, syscall.PROT_READ|syscall.PROT_WRITE,
		syscall.MAP_ANONYMOUS|syscall.MAP_PRIVATE, 0)
	if err != nil {
		syscall.Close(sockFD)
		return fmt.Errorf("allocate umem: %w", err)
	}

	p.umem = &UmemRegion{
		addr:     uintptr(unsafe.Pointer(&umemAddr[0])),
		size:     uint64(len(umemAddr)),
		headroom: UMEM_DEFAULT_HEADROOM,
		frameSize: UMEM_DEFAULT_FRAME_SIZE,
	}

	if err := syscall.Bind(sockFD, &syscall.SockaddrXDP{
		Ifindex: 0,
		QueueID: uint32(p.queueID),
		Flags:   XDP_FLAGS,
	}); err != nil {
		syscall.Close(sockFD)
		syscall.Munmap(umemAddr)
		return fmt.Errorf("bind xsk socket: %w", err)
	}

	p.socketFD = sockFD

	return nil
}

func (p *Proxy) Start() {
	p.wg.Add(2)
	go p.packetProcessor()
	go p.decisionReceiver()

	log.Printf("L7 Proxy started (XSK fd: %d)", p.socketFD)
}

func (p *Proxy) Stop() {
	p.cancel()
	p.wg.Wait()

	if p.socketFD > 0 {
		syscall.Close(p.socketFD)
	}

	if p.umem != nil {
		syscall.Munmap([]byte{})
	}

	log.Println("L7 Proxy stopped")
}

func (p *Proxy) packetProcessor() {
	defer p.wg.Done()

	buffer := make([]byte, 4096)

	for {
		select {
		case <-p.ctx.Done():
			return
		default:
			n, _, _, _, err := syscall.Recvfrom(p.socketFD, buffer, 0)
			if err != nil {
				if err == syscall.EAGAIN {
					continue
				}
				log.Printf("Recv error: %v", err)
				continue
			}

			if n > 0 {
				p.processPacket(buffer[:n])
			}
		}
	}
}

func (p *Proxy) processPacket(data []byte) {
	p.stats.TotalPackets++

	ctx := &L7Context{
		SrcIP:      bpf.IPToUint32(net.IP(data[12:16])),
		DstIP:      bpf.IPToUint32(net.IP(data[16:20])),
		SrcPort:    uint16(data[20])<<8 | uint16(data[21]),
		DstPort:    uint16(data[22])<<8 | uint16(data[23]),
		Protocol:   data[23],
		L7Protocol: detectL7Protocol(uint16(data[22])<<8 | uint16(data[23])),
		FlowID:     uint64(bpf.IPToUint32(net.IP(data[12:16])))<<32 | uint64(uint16(data[20])<<8|uint16(data[21])),
	}

	if ctx.L7Protocol == ProtocolHTTP {
		ctx.HTTPHost = parseHTTPHost(data)
		ctx.HTTPPath = parseHTTPPath(data)
	} else if ctx.L7Protocol == ProtocolGRPC {
		ctx.GRPCMethod = parseGRPCMethod(data)
	}

	p.l7Engine.SubmitRequest(ctx)
	p.stats.ProcessedL7++
}

func (p *Proxy) decisionReceiver() {
	defer p.wg.Done()

	ticker := 0
	for {
		select {
		case <-p.ctx.Done():
			return
		case decision := <-p.l7Engine.statsCh:
			p.handleDecision(decision)
			ticker++
			if ticker%1000 == 0 {
				p.logStats()
			}
		}
	}
}

func (p *Proxy) handleDecision(decision *L7Decision) {
	if decision.Decision == DecisionDeny {
		p.stats.Dropped++
		return
	}

	p.forwardPacket(decision)
	p.stats.Redirected++
}

func (p *Proxy) forwardPacket(decision *L7Decision) {
	return
}

func (p *Proxy) logStats() {
	avgLatency := uint64(0)
	if p.stats.ProcessedL7 > 0 {
		avgLatency = p.stats.AvgLatencyNS / p.stats.ProcessedL7
	}

	log.Printf("[L7 Proxy] Packets: %d | L7 Processed: %d | Forwarded: %d | Dropped: %d | Avg Latency: %d ns",
		p.stats.TotalPackets, p.stats.ProcessedL7, p.stats.Redirected, p.stats.Dropped, avgLatency)
}

func detectL7Protocol(port uint16) Protocol {
	switch port {
	case 80, 8080:
		return ProtocolHTTP
	case 443, 8443:
		return ProtocolHTTP2
	case 50051:
		return ProtocolGRPC
	default:
		return ProtocolUnknown
	}
}

func parseHTTPHost(data []byte) string {
	str := string(data)
	for i := 0; i < len(str)-10; i++ {
		if str[i:i+10] == "Host: " {
			start := i + 6
			end := start
			for end < len(str) && str[end] != '\r' {
				end++
			}
			return str[start:end]
		}
	}
	return ""
}

func parseHTTPPath(data []byte) string {
	str := string(data)
	for i := 0; i < len(str)-5; i++ {
		if str[i:i+4] == "GET " || str[i:i+4] == "POST" || str[i:i+3] == "PUT" || str[i:i+6] == "DELETE" {
			start := i + 4
			end := start
			for end < len(str) && str[end] != ' ' && str[end] != '\r' {
				end++
			}
			if end > start {
				return str[start:end]
			}
		}
	}
	return ""
}

func parseGRPCMethod(data []byte) string {
	str := string(data)
	for i := 0; i < len(str)-7; i++ {
		if str[i:i+7] == "/grpc/" {
			start := i + 7
			end := start
			for end < len(str) && str[end] != '\r' && str[end] != '\n' {
				end++
			}
			return "/" + str[start:end]
		}
	}
	return ""
}

func (p *Proxy) GetStats() ProxyStats {
	return ProxyStats{
		TotalPackets: p.stats.TotalPackets,
		ProcessedL7:  p.stats.ProcessedL7,
		Redirected:   p.stats.Redirected,
		Dropped:      p.stats.Dropped,
		AvgLatencyNS: p.stats.AvgLatencyNS,
	}
}
