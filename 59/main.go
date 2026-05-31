package main

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go -cc clang -target bpfel bpf bpf/flow.c -- -I./bpf -O2 -g -Wall

import (
	"bytes"
	"context"
	"encoding/binary"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"os/signal"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/link"
	"github.com/cilium/ebpf/perf"
	"github.com/cilium/ebpf/rlimit"
	"github.com/vishvananda/netlink"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/tools/clientcmd"
)

type Config struct {
	Ifaces       []string
	KubeConfig   string
	Format       ReportFormat
	ReportPath   string
	ScanInterval time.Duration
	RunDuration  time.Duration
	Enforce      bool
	Whitelist    string
}

func main() {
	var (
		ifaceList  = flag.String("ifaces", "", "comma-separated list of host interfaces to attach tc (default: all up non-loopback)")
		kubeconfig = flag.String("kubeconfig", clientcmd.RecommendedHomeFile, "path to kubeconfig")
		format     = flag.String("format", "table", "report format: table or json")
		out        = flag.String("out", "", "output file (default: stdout)")
		interval   = flag.Duration("interval", 10*time.Second, "audit cycle interval")
		duration   = flag.Duration("duration", 0, "total run duration (0 = run until SIGINT)")
		enforce    = flag.Bool("enforce", false, "enable active enforcement (TC_ACT_SHOT + RST)")
		whitelist  = flag.String("whitelist", "", "path to whitelist JSON file")
	)
	flag.Parse()

	cfg := Config{
		Ifaces:       splitCSV(*ifaceList),
		KubeConfig:   *kubeconfig,
		Format:       ReportFormat(*format),
		ReportPath:   *out,
		ScanInterval: *interval,
		RunDuration:  *duration,
		Enforce:      *enforce,
		Whitelist:    *whitelist,
	}
	if cfg.Format != FormatTable && cfg.Format != FormatJSON {
		log.Fatalf("unsupported format: %s", cfg.Format)
	}

	if err := run(cfg); err != nil {
		log.Fatalf("error: %v", err)
	}
}

func run(cfg Config) error {
	if err := rlimit.RemoveMemlock(); err != nil {
		return fmt.Errorf("remove memlock: %w", err)
	}

	whitelist, err := LoadWhitelist(cfg.Whitelist)
	if err != nil {
		return fmt.Errorf("whitelist: %w", err)
	}
	if whitelist.Len() > 0 {
		log.Printf("loaded %d whitelist rules from %s", whitelist.Len(), cfg.Whitelist)
	}

	clientset, err := newKubeClient(cfg.KubeConfig)
	if err != nil {
		return fmt.Errorf("kube client: %w", err)
	}

	cache := NewKubeCache(clientset)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	if err := cache.Start(ctx); err != nil {
		return fmt.Errorf("kube cache: %w", err)
	}
	defer cache.Stop()

	bpfObjs, err := loadFlowObjects()
	if err != nil {
		return fmt.Errorf("load bpf objects: %w", err)
	}
	defer bpfObjs.Close()

	if cfg.Enforce {
		if err := bpfObjs.EnforceMode.Put(uint32(0), uint8(1)); err != nil {
			return fmt.Errorf("set enforce_mode: %w", err)
		}
		log.Printf("enforce mode: ON (violations will be dropped via TC_ACT_SHOT)")
	} else {
		if err := bpfObjs.EnforceMode.Put(uint32(0), uint8(0)); err != nil {
			return fmt.Errorf("set enforce_mode: %w", err)
		}
		log.Printf("enforce mode: OFF (audit only)")
	}

	cache.SetIPMetaBackend(
		func(ip net.IP, meta PodMetaValue) error {
			key := ipToUint32(ip)
			if key == 0 {
				return nil
			}
			return bpfObjs.IPMeta.Put(key, meta)
		},
		func(ip net.IP) error {
			key := ipToUint32(ip)
			if key == 0 {
				return nil
			}
			return bpfObjs.IPMeta.Delete(key)
		},
	)
	log.Printf("kube cache synced (%d pods, %d netpols, %d ip records)",
		len(cache.AllPods()), len(cache.NetworkPolicies()), cache.IPHistoryCount())

	ifaces, err := resolveIfaces(cfg.Ifaces)
	if err != nil {
		return fmt.Errorf("resolve ifaces: %w", err)
	}
	log.Printf("attaching tc to %d ifaces: %v", len(ifaces), ifaces)

	var netLinks []netlink.Link
	var bpfLinks []link.Link
	for _, name := range ifaces {
		nl, err := attachTC(name, bpfObjs)
		if err != nil {
			log.Printf("attach tc on %s: %v", name, err)
			continue
		}
		netLinks = append(netLinks, nl.link)
		if nl.ingress != nil { bpfLinks = append(bpfLinks, nl.ingress) }
		if nl.egress  != nil { bpfLinks = append(bpfLinks, nl.egress) }
	}
	defer cleanupTC(netLinks, bpfLinks)

	rd, err := perf.NewReader(bpfObjs.Events, os.Getpagesize()*16)
	if err != nil {
		return fmt.Errorf("perf reader: %w", err)
	}
	defer rd.Close()

	blockRd, err := perf.NewReader(bpfObjs.BlockEvents, os.Getpagesize()*16)
	if err != nil {
		return fmt.Errorf("block perf reader: %w", err)
	}
	defer blockRd.Close()

	var rstSender *RSTSender
	if cfg.Enforce {
		rstSender, err = NewRSTSender()
		if err != nil {
			log.Printf("RST sender disabled: %v", err)
		} else {
			defer rstSender.Close()
		}
	}

	tracker := NewFlowTracker()
	var blockedMu sync.Mutex
	blockedSet := make(map[flowMapKey]struct{})

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	ticker := time.NewTicker(cfg.ScanInterval)
	defer ticker.Stop()

	go func() {
		for {
			rec, err := rd.Read()
			if err != nil {
				if perf.IsClosed(err) {
					return
				}
				log.Printf("perf read: %v", err)
				continue
			}
			if rec.LostSamples != 0 {
				log.Printf("perf lost %d samples", rec.LostSamples)
				continue
			}
			ev, err := decodeFlowEvent(rec.RawSample)
			if err != nil {
				log.Printf("decode: %v", err)
				continue
			}
			tracker.Observe(FlowKey{
				SrcIP:   intToIP(ev.Saddr),
				DstIP:   intToIP(ev.Daddr),
				DstPort: ev.Dport,
				Proto:   Proto(ev.Proto),
			}, ev.Bytes, time.Unix(0, int64(ev.TsNs)))
		}
	}()

	go func() {
		for {
			rec, err := blockRd.Read()
			if err != nil {
				if perf.IsClosed(err) {
					return
				}
				log.Printf("block perf read: %v", err)
				continue
			}
			if rec.LostSamples != 0 {
				log.Printf("block perf lost %d samples", rec.LostSamples)
				continue
			}
			ev, err := decodeBlockEvent(rec.RawSample)
			if err != nil {
				log.Printf("block decode: %v", err)
				continue
			}
			srcIP := intToIP(ev.Saddr)
			dstIP := intToIP(ev.Daddr)
			log.Printf("BLOCKED %s:%d -> %s:%d %s (seq=%d ack=%d flags=0x%x)",
				srcIP, ev.Sport, dstIP, ev.Dport, Proto(ev.Proto), ev.Seq, ev.Ack, ev.TcpFlags)
			if rstSender != nil && ev.Proto == uint8(ProtoTCP) {
				rstSender.SendRSTBoth(srcIP, dstIP, ev.Sport, ev.Dport, ev.Seq, ev.Ack)
			}
		}
	}()

	applyBlocked := func(keys []flowMapKey) {
		blockedMu.Lock()
		defer blockedMu.Unlock()
		for _, k := range keys {
			if _, ok := blockedSet[k]; ok {
				continue
			}
			blockedSet[k] = struct{}{}
			ebpfKey := struct {
				Saddr uint32
				Daddr uint32
				Dport uint16
				Proto uint8
				Pad   uint8
			}{
				Saddr: ipToUint32EBPF(net.IP(k.SrcIP)),
				Daddr: ipToUint32EBPF(net.IP(k.DstIP)),
				Dport: portToEBPF(k.DstPort),
				Proto: uint8(k.Proto),
			}
			val := BlockedValue{
				FirstTs: uint64(time.Now().UnixNano()),
				LastTs:  uint64(time.Now().UnixNano()),
			}
			if err := bpfObjs.Blocked.Put(ebpfKey, val); err != nil {
				log.Printf("blocked put: %v", err)
			}
		}
	}

	var deadline <-chan time.Time
	if cfg.RunDuration > 0 {
		deadline = time.After(cfg.RunDuration)
	}

	finalize := func() {
		result := Audit(cache, tracker, whitelist, cfg.Enforce)
		result.Report.GeneratedAt = time.Now().UTC().Format(time.RFC3339)
		if err := emitReport(result.Report, cfg); err != nil {
			log.Printf("emit report: %v", err)
		}
	}

	for {
		select {
		case <-ticker.C:
			result := Audit(cache, tracker, whitelist, cfg.Enforce)
			result.Report.GeneratedAt = time.Now().UTC().Format(time.RFC3339)
			if cfg.Enforce {
				applyBlocked(result.BlockKeys)
			}
			if err := emitReport(result.Report, cfg); err != nil {
				log.Printf("emit report: %v", err)
			}
		case <-sigCh:
			log.Printf("received signal, finalizing...")
			finalize()
			return nil
		case <-deadline:
			log.Printf("run duration reached")
			finalize()
			return nil
		}
	}
}

func emitReport(report AuditReport, cfg Config) error {
	if cfg.ReportPath == "" {
		return WriteReport(report, cfg.Format, os.Stdout)
	}
	return SaveReport(report, cfg.Format, cfg.ReportPath)
}

type attachedTC struct {
	link    netlink.Link
	ingress link.Link
	egress  link.Link
}

func attachTC(ifaceName string, objs *flowObjects) (*attachedTC, error) {
	nl, err := netlink.LinkByName(ifaceName)
	if err != nil {
		return nil, fmt.Errorf("link %s: %w", ifaceName, err)
	}

	qdisc := &netlink.GenericQdisc{
		QdiscAttrs: netlink.QdiscAttrs{
			LinkIndex: nl.Attrs().Index,
			Parent:    netlink.HANDLE_CLSACT,
			Handle:    netlink.MakeHandle(0xffff, 0),
		},
		QdiscType: "clsact",
	}
	if err := netlink.QdiscAdd(qdisc); err != nil && !isEEXIST(err) {
		return nil, fmt.Errorf("qdisc clsact: %w", err)
	}

	result := &attachedTC{link: nl}

	if objs.HandleIngress != nil {
		ingress, err := link.AttachTCX(link.TCXOptions{
			Program:   objs.HandleIngress,
			Attach:    ebpf.AttachTCXIngress,
			Interface: uint32(nl.Attrs().Index),
		})
		if err != nil {
			return nil, fmt.Errorf("attach ingress: %w", err)
		}
		result.ingress = ingress
	}

	if objs.HandleEgress != nil {
		egress, err := link.AttachTCX(link.TCXOptions{
			Program:   objs.HandleEgress,
			Attach:    ebpf.AttachTCXEgress,
			Interface: uint32(nl.Attrs().Index),
		})
		if err != nil {
			return nil, fmt.Errorf("attach egress: %w", err)
		}
		result.egress = egress
	}

	return result, nil
}

func cleanupTC(links []netlink.Link, bpfLinks []link.Link) {
	for _, l := range bpfLinks {
		if l != nil { _ = l.Close() }
	}
	for _, l := range links {
		_ = netlink.QdiscDel(&netlink.GenericQdisc{
			QdiscAttrs: netlink.QdiscAttrs{
				LinkIndex: l.Attrs().Index,
				Parent:    netlink.HANDLE_CLSACT,
				Handle:    netlink.MakeHandle(0xffff, 0),
			},
			QdiscType: "clsact",
		})
	}
}

func isEEXIST(err error) bool {
	return err != nil && hasErrno(err, syscall.EEXIST)
}

func hasErrno(err error, want syscall.Errno) bool {
	if e, ok := err.(syscall.Errno); ok { return e == want }
	return false
}

func newKubeClient(kubeconfig string) (*kubernetes.Clientset, error) {
	loadingRules := clientcmd.NewDefaultClientConfigLoadingRules()
	if kubeconfig != "" {
		loadingRules.ExplicitPath = kubeconfig
	}
	config, err := clientcmd.NewNonInteractiveDeferredLoadingClientConfig(loadingRules, &clientcmd.ConfigOverrides{}).ClientConfig()
	if err != nil {
		return nil, err
	}
	return kubernetes.NewForConfig(config)
}

func resolveIfaces(names []string) ([]string, error) {
	if len(names) > 0 {
		return names, nil
	}
	links, err := netlink.LinkList()
	if err != nil {
		return nil, err
	}
	out := make([]string, 0)
	for _, l := range links {
		name := l.Attrs().Name
		if name == "lo" {
			continue
		}
		if l.Attrs().Flags&net.FlagUp == 0 {
			continue
		}
		if l.Type() != "ether" && l.Type() != "veth" {
			continue
		}
		out = append(out, name)
	}
	return out, nil
}

type flowEvent struct {
	Saddr uint32
	Daddr uint32
	Sport uint16
	Dport uint16
	Proto uint8
	Pad   [3]uint8
	Bytes uint64
	TsNs  uint64
}

func decodeFlowEvent(b []byte) (*flowEvent, error) {
	var e flowEvent
	if len(b) < binary.Size(e) {
		return nil, fmt.Errorf("short sample: %d", len(b))
	}
	r := bytes.NewReader(b)
	if err := binary.Read(r, binary.LittleEndian, &e); err != nil {
		return nil, err
	}
	return &e, nil
}

func decodeBlockEvent(b []byte) (*BlockEvent, error) {
	var e BlockEvent
	if len(b) < binary.Size(e) {
		return nil, fmt.Errorf("short block sample: %d", len(b))
	}
	r := bytes.NewReader(b)
	if err := binary.Read(r, binary.LittleEndian, &e); err != nil {
		return nil, err
	}
	return &e, nil
}

func intToIP(v uint32) net.IP {
	return net.IPv4(byte(v), byte(v>>8), byte(v>>16), byte(v>>24))
}

func ipToUint32EBPF(ip net.IP) uint32 {
	if ip4 := ip.To4(); ip4 != nil {
		return binary.LittleEndian.Uint32(ip4)
	}
	return 0
}

func portToEBPF(port uint16) uint16 {
	return (port >> 8) | (port << 8)
}

func splitCSV(s string) []string {
	if s == "" {
		return nil
	}
	parts := strings.Split(s, ",")
	out := make([]string, 0, len(parts))
	for _, p := range parts {
		p = strings.TrimSpace(p)
		if p != "" {
			out = append(out, p)
		}
	}
	return out
}
