package main

import (
	"context"
	"encoding/binary"
	"fmt"
	"hash/fnv"
	"net"
	"sync"
	"time"

	networkingv1 "k8s.io/api/networking/v1"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/client-go/informers"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/tools/cache"
)

const (
	PodIPHistoryTTL = 24 * time.Hour
)

type PodIPState uint8

const (
	PodIPActive    PodIPState = 1
	PodIPReleased  PodIPState = 2
)

func (s PodIPState) String() string {
	switch s {
	case PodIPActive:
		return "active"
	case PodIPReleased:
		return "released"
	}
	return "unknown"
}

type PodIPRecord struct {
	IP          string
	PodName     string
	Namespace   string
	State       PodIPState
	AssignedAt  time.Time
	ReleasedAt  time.Time
	PodLabels   map[string]string
}

func (r PodIPRecord) Namespaced() string {
	return r.Namespace + "/" + r.PodName
}

type KubeCache struct {
	clientset kubernetes.Interface
	factory   informers.SharedInformerFactory

	stop chan struct{}

	mu         sync.RWMutex
	ipHistory  map[string][]PodIPRecord
	podsByName map[string]*corev1.Pod
	netpols    []*networkingv1.NetworkPolicy

	ipMetaMu sync.Mutex
	ipMeta   *eBPFIPMeta
}

type eBPFIPMeta struct {
	put    func(ip net.IP, meta PodMetaValue) error
	delete func(ip net.IP) error
}

func NewKubeCache(clientset kubernetes.Interface) *KubeCache {
	return &KubeCache{
		clientset:  clientset,
		factory:    informers.NewSharedInformerFactory(clientset, 30*time.Second),
		stop:       make(chan struct{}),
		ipHistory:  make(map[string][]PodIPRecord),
		podsByName: make(map[string]*corev1.Pod),
	}
}

func (k *KubeCache) SetIPMetaBackend(put func(net.IP, PodMetaValue) error, del func(net.IP) error) {
	k.ipMetaMu.Lock()
	defer k.ipMetaMu.Unlock()
	k.ipMeta = &eBPFIPMeta{put: put, delete: del}
}

func (k *KubeCache) Start(ctx context.Context) error {
	podInf := k.factory.Core().V1().Pods()
	npInf := k.factory.Networking().V1().NetworkPolicies()

	podInf.Informer().AddEventHandler(cache.ResourceEventHandlerFuncs{
		AddFunc: func(obj interface{}) {
			if p, ok := obj.(*corev1.Pod); ok {
				k.addPod(p)
			}
		},
		UpdateFunc: func(old, obj interface{}) {
			if p, ok := obj.(*corev1.Pod); ok {
				if op, ok2 := old.(*corev1.Pod); ok2 {
					k.updatePod(op, p)
					return
				}
				k.addPod(p)
			}
		},
		DeleteFunc: func(obj interface{}) {
			if p, ok := obj.(*corev1.Pod); ok {
				k.deletePod(p)
			}
		},
	})

	npInf.Informer().AddEventHandler(cache.ResourceEventHandlerFuncs{
		AddFunc: func(obj interface{}) {
			if np, ok := obj.(*networkingv1.NetworkPolicy); ok {
				k.addNetPol(np)
			}
		},
		UpdateFunc: func(old, obj interface{}) {
			if np, ok := obj.(*networkingv1.NetworkPolicy); ok {
				k.addNetPol(np)
			}
		},
		DeleteFunc: func(obj interface{}) {
			if np, ok := obj.(*networkingv1.NetworkPolicy); ok {
				k.delNetPol(np)
			}
		},
	})

	go podInf.Informer().Run(k.stop)
	go npInf.Informer().Run(k.stop)

	deadline := time.After(30 * time.Second)
	for {
		if podInf.Informer().HasSynced() && npInf.Informer().HasSynced() {
			break
		}
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-deadline:
			return fmt.Errorf("informer sync timeout")
		case <-time.After(100 * time.Millisecond):
		}
	}

	go k.gcIPHistory(ctx)
	return nil
}

func (k *KubeCache) Stop() {
	close(k.stop)
}

func (k *KubeCache) addPod(p *corev1.Pod) {
	k.mu.Lock()
	defer k.mu.Unlock()
	k.podsByName[namespaced(p.Namespace, p.Name)] = p
	if p.Status.PodIP != "" {
		k.assignIPLocked(p)
	}
}

func (k *KubeCache) updatePod(old, cur *corev1.Pod) {
	k.mu.Lock()
	defer k.mu.Unlock()
	k.podsByName[namespaced(cur.Namespace, cur.Name)] = cur

	oldIP := old.Status.PodIP
	curIP := cur.Status.PodIP

	if oldIP == curIP {
		return
	}

	if oldIP != "" {
		k.releaseIPLocked(oldIP, time.Now())
	}
	if curIP != "" {
		k.assignIPLocked(cur)
	}
}

func (k *KubeCache) deletePod(p *corev1.Pod) {
	k.mu.Lock()
	defer k.mu.Unlock()
	delete(k.podsByName, namespaced(p.Namespace, p.Name))
	if p.Status.PodIP != "" {
		k.releaseIPLocked(p.Status.PodIP, time.Now())
	}
}

func (k *KubeCache) assignIPLocked(p *corev1.Pod) {
	ip := p.Status.PodIP
	now := time.Now()
	rec := PodIPRecord{
		IP:         ip,
		PodName:    p.Name,
		Namespace:  p.Namespace,
		State:      PodIPActive,
		AssignedAt: now,
		PodLabels:  copyLabels(p.Labels),
	}
	recs := k.ipHistory[ip]
	for i, r := range recs {
		if r.PodName == p.Name && r.Namespace == p.Namespace {
			recs[i] = rec
			k.ipHistory[ip] = recs
			k.syncIPMetaLocked(ip, rec)
			return
		}
	}
	for i, r := range recs {
		if r.State == PodIPReleased {
			recs[i] = rec
			k.ipHistory[ip] = recs
			k.syncIPMetaLocked(ip, rec)
			return
		}
	}
	k.ipHistory[ip] = append(recs, rec)
	k.syncIPMetaLocked(ip, rec)
}

func (k *KubeCache) releaseIPLocked(ip string, at time.Time) {
	recs, ok := k.ipHistory[ip]
	if !ok {
		return
	}
	for i, r := range recs {
		if r.State == PodIPActive {
			recs[i].State = PodIPReleased
			recs[i].ReleasedAt = at
			k.syncIPMetaLocked(ip, recs[i])
		}
	}
}

func (k *KubeCache) syncIPMetaLocked(ip string, rec PodIPRecord) {
	k.ipMetaMu.Lock()
	meta := k.ipMeta
	k.ipMetaMu.Unlock()
	if meta == nil {
		return
	}
	parsedIP := net.ParseIP(ip)
	if parsedIP == nil {
		return
	}
	val := PodMetaValue{
		UpdatedAtNs: uint64(rec.AssignedAt.UnixNano()),
		NameHash:    hashPodName(rec.Namespace, rec.PodName),
	}
	switch rec.State {
	case PodIPActive:
		val.State = IPStateActive
		if meta.put != nil {
			if err := meta.put(parsedIP, val); err != nil {
				fmt.Printf("ip_meta put %s: %v\n", ip, err)
			}
		}
	case PodIPReleased:
		val.State = IPStateReleased
		if rec.ReleasedAt.After(rec.AssignedAt) {
			val.UpdatedAtNs = uint64(rec.ReleasedAt.UnixNano())
		}
		if meta.put != nil {
			if err := meta.put(parsedIP, val); err != nil {
				fmt.Printf("ip_meta put(released) %s: %v\n", ip, err)
			}
		}
	}
}

func (k *KubeCache) gcIPHistory(ctx context.Context) {
	t := time.NewTicker(1 * time.Hour)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			cutoff := time.Now().Add(-PodIPHistoryTTL)
			k.mu.Lock()
			for ip, recs := range k.ipHistory {
				kept := recs[:0]
				for _, r := range recs {
					base := r.AssignedAt
					if r.State == PodIPReleased && !r.ReleasedAt.IsZero() {
						base = r.ReleasedAt
					}
					if base.After(cutoff) {
						kept = append(kept, r)
					}
				}
				if len(kept) == 0 {
					delete(k.ipHistory, ip)
				} else {
					k.ipHistory[ip] = kept
				}
			}
			k.mu.Unlock()
		}
	}
}

func (k *KubeCache) PodByIP(ip string) *corev1.Pod {
	rec := k.LookupActivePodByIP(ip)
	if rec == nil {
		return nil
	}
	k.mu.RLock()
	defer k.mu.RUnlock()
	return k.podsByName[namespaced(rec.Namespace, rec.PodName)]
}

func (k *KubeCache) LookupActivePodByIP(ip string) *PodIPRecord {
	k.mu.RLock()
	defer k.mu.RUnlock()
	return k.lookupActiveLocked(ip)
}

func (k *KubeCache) lookupActiveLocked(ip string) *PodIPRecord {
	for _, r := range k.ipHistory[ip] {
		if r.State == PodIPActive {
			cp := r
			return &cp
		}
	}
	return nil
}

func (k *KubeCache) LookupPodHistorical(ip string, at time.Time) *PodIPRecord {
	k.mu.RLock()
	defer k.mu.RUnlock()
	var best *PodIPRecord
	var bestScore int64 = -1
	for _, r := range k.ipHistory[ip] {
		var score int64
		switch r.State {
		case PodIPActive:
			if at.Before(r.AssignedAt) {
				continue
			}
			score = at.Sub(r.AssignedAt).Nanoseconds()
		case PodIPReleased:
			if at.Before(r.AssignedAt) {
				continue
			}
			if !r.ReleasedAt.IsZero() && at.After(r.ReleasedAt) {
				continue
			}
			score = at.Sub(r.AssignedAt).Nanoseconds()
			if score < 0 {
				score = -score
			}
		}
		if best == nil || score < bestScore {
			cp := r
			best = &cp
			bestScore = score
		}
	}
	return best
}

func (k *KubeCache) AllPods() []*corev1.Pod {
	k.mu.RLock()
	defer k.mu.RUnlock()
	out := make([]*corev1.Pod, 0, len(k.podsByName))
	for _, p := range k.podsByName {
		if p.Status.PodIP != "" {
			out = append(out, p)
		}
	}
	return out
}

func (k *KubeCache) NetworkPolicies() []*networkingv1.NetworkPolicy {
	k.mu.RLock()
	defer k.mu.RUnlock()
	out := make([]*networkingv1.NetworkPolicy, len(k.netpols))
	copy(out, k.netpols)
	return out
}

func (k *KubeCache) IPHistoryCount() int {
	k.mu.RLock()
	defer k.mu.RUnlock()
	n := 0
	for _, recs := range k.ipHistory {
		n += len(recs)
	}
	return n
}

func (k *KubeCache) addNetPol(np *networkingv1.NetworkPolicy) {
	k.mu.Lock()
	defer k.mu.Unlock()
	key := namespaced(np.Namespace, np.Name)
	for i, x := range k.netpols {
		if namespaced(x.Namespace, x.Name) == key {
			k.netpols[i] = np
			return
		}
	}
	k.netpols = append(k.netpols, np)
}

func (k *KubeCache) delNetPol(np *networkingv1.NetworkPolicy) {
	k.mu.Lock()
	defer k.mu.Unlock()
	key := namespaced(np.Namespace, np.Name)
	for i, x := range k.netpols {
		if namespaced(x.Namespace, x.Name) == key {
			k.netpols = append(k.netpols[:i], k.netpols[i+1:]...)
			return
		}
	}
}

func namespaced(ns, name string) string {
	return ns + "/" + name
}

func copyLabels(m map[string]string) map[string]string {
	if len(m) == 0 {
		return nil
	}
	out := make(map[string]string, len(m))
	for k, v := range m {
		out[k] = v
	}
	return out
}

func hashPodName(ns, name string) uint64 {
	h := fnv.New64a()
	_, _ = h.Write([]byte(ns))
	_, _ = h.Write([]byte{'/'})
	_, _ = h.Write([]byte(name))
	return h.Sum64()
}

func ipToUint32(ip net.IP) uint32 {
	if ip4 := ip.To4(); ip4 != nil {
		return binary.BigEndian.Uint32(ip4)
	}
	return 0
}

func uint32ToIP(v uint32) net.IP {
	return net.IPv4(byte(v>>24), byte(v>>16), byte(v>>8), byte(v))
}
