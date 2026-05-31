package main

import (
	"net"
	"strconv"
	"strings"
	"time"

	networkingv1 "k8s.io/api/networking/v1"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/labels"
	"k8s.io/apimachinery/pkg/util/intstr"
)

type Violation struct {
	SrcIP        string `json:"src_ip"`
	DstIP        string `json:"dst_ip"`
	SrcPod       string `json:"src_pod,omitempty"`
	SrcNamespace string `json:"src_namespace,omitempty"`
	DstPod       string `json:"dst_pod,omitempty"`
	DstNamespace string `json:"dst_namespace,omitempty"`
	DstPort      uint16 `json:"dst_port"`
	Proto        string `json:"proto"`
	Count        uint64 `json:"count"`
	Bytes        uint64 `json:"bytes"`
	FirstSeen    string `json:"first_seen"`
	LastSeen     string `json:"last_seen"`
	Reason       string `json:"reason"`
	SrcIPState   string `json:"src_ip_state,omitempty"`
	DstIPState   string `json:"dst_ip_state,omitempty"`
}

type AuditReport struct {
	GeneratedAt  string      `json:"generated_at"`
	TotalFlows   int         `json:"total_flows"`
	Violations   []Violation `json:"violations"`
	ImpactedPods []string    `json:"impacted_pods"`
	EnforceMode  bool        `json:"enforce_mode,omitempty"`
}

type AuditResult struct {
	Report     AuditReport
	BlockKeys  []flowMapKey
}

func Audit(cache *KubeCache, tracker *FlowTracker, whitelist *Whitelist, enforce bool) AuditResult {
	flows := tracker.Snapshot()
	netpols := cache.NetworkPolicies()

	impacted := map[string]struct{}{}
	violations := make([]Violation, 0)
	var blockKeys []flowMapKey

	for mk, stats := range flows {
		key := mk.FlowKey()
		srcRec := cache.LookupPodHistorical(key.SrcIP.String(), stats.FirstSeen)
		dstRec := cache.LookupPodHistorical(key.DstIP.String(), stats.FirstSeen)

		if srcRec == nil && dstRec == nil {
			continue
		}

		srcPod := recToPod(srcRec)
		dstPod := recToPod(dstRec)

		srcName := recDesc(srcRec)
		dstName := recDesc(dstRec)

		var blocked bool
		var reason string

		if dstPod != nil && egressBlocked(srcPod, dstPod, key, netpols) {
			blocked = true
			reason = "egress not allowed by source NetworkPolicy"
		} else if dstPod != nil && ingressBlocked(srcPod, dstPod, key, netpols) {
			blocked = true
			reason = "ingress denied by destination NetworkPolicy"
		}

		if !blocked {
			if (srcRec != nil && srcRec.State == PodIPReleased) || (dstRec != nil && dstRec.State == PodIPReleased) {
				blocked = true
				reason = "connection to/from released IP (pod may have been recycled)"
			}
		}

		if !blocked {
			continue
		}

		whitelisted := whitelist != nil && whitelist.Contains(key.SrcIP, key.DstIP, key.DstPort, key.Proto)
		if whitelisted {
			reason = reason + " (whitelisted)"
		}

		v := Violation{
			SrcIP:        key.SrcIP.String(),
			DstIP:        key.DstIP.String(),
			SrcPod:       srcName,
			DstPod:       dstName,
			DstPort:      key.DstPort,
			Proto:        key.Proto.String(),
			Count:        stats.Count,
			Bytes:        stats.Bytes,
			FirstSeen:    stats.FirstSeen.UTC().Format(time.RFC3339Nano),
			LastSeen:     stats.LastSeen.UTC().Format(time.RFC3339Nano),
			Reason:       reason,
			SrcIPState:   recState(srcRec),
			DstIPState:   recState(dstRec),
		}
		if srcRec != nil {
			v.SrcNamespace = srcRec.Namespace
		}
		if dstRec != nil {
			v.DstNamespace = dstRec.Namespace
		}
		violations = append(violations, v)

		if srcRec != nil {
			impacted[srcRec.Namespaced()] = struct{}{}
		}
		if dstRec != nil {
			impacted[dstRec.Namespaced()] = struct{}{}
		}

		if enforce && !whitelisted {
			blockKeys = append(blockKeys, mk)
		}
	}

	podsList := make([]string, 0, len(impacted))
	for p := range impacted {
		podsList = append(podsList, p)
	}

	return AuditResult{
		Report: AuditReport{
			TotalFlows:   len(flows),
			Violations:   violations,
			ImpactedPods: podsList,
			EnforceMode:  enforce,
		},
		BlockKeys: blockKeys,
	}
}

func recToPod(r *PodIPRecord) *corev1.Pod {
	if r == nil {
		return nil
	}
	return &corev1.Pod{
		ObjectMeta: metav1.ObjectMeta{
			Name:      r.PodName,
			Namespace: r.Namespace,
			Labels:    r.PodLabels,
		},
		Status: corev1.PodStatus{PodIP: r.IP},
	}
}

func recDesc(r *PodIPRecord) string {
	if r == nil {
		return ""
	}
	return r.Namespaced()
}

func recState(r *PodIPRecord) string {
	if r == nil {
		return ""
	}
	return r.State.String()
}

func egressBlocked(src, dst *corev1.Pod, key FlowKey, netpols []*networkingv1.NetworkPolicy) bool {
	if src == nil {
		return false
	}
	for _, np := range netpols {
		if np.Namespace != src.Namespace {
			continue
		}
		if !selectorMatches(np.Spec.PodSelector, src) {
			continue
		}
		if !policyTypeIncludes(np.Spec.PolicyTypes, networkingv1.PolicyTypeEgress) {
			continue
		}
		if len(np.Spec.Egress) == 0 {
			continue
		}
		matched := false
		for _, rule := range np.Spec.Egress {
			if !portsMatch(rule.Ports, key) {
				continue
			}
			if len(rule.To) == 0 {
				matched = true
				break
			}
			for _, to := range rule.To {
				if peerMatches(to, dst, key.DstIP) {
					matched = true
					break
				}
			}
			if matched {
				break
			}
		}
		if !matched {
			return true
		}
	}
	return false
}

func ingressBlocked(src, dst *corev1.Pod, key FlowKey, netpols []*networkingv1.NetworkPolicy) bool {
	if dst == nil {
		return false
	}
	for _, np := range netpols {
		if np.Namespace != dst.Namespace {
			continue
		}
		if !selectorMatches(np.Spec.PodSelector, dst) {
			continue
		}
		if !policyTypeIncludes(np.Spec.PolicyTypes, networkingv1.PolicyTypeIngress) {
			continue
		}
		if len(np.Spec.Ingress) == 0 {
			continue
		}
		matched := false
		for _, rule := range np.Spec.Ingress {
			if !portsMatch(rule.Ports, key) {
				continue
			}
			if len(rule.From) == 0 {
				matched = true
				break
			}
			for _, from := range rule.From {
				if peerMatches(from, src, key.SrcIP) {
					matched = true
					break
				}
			}
			if matched {
				break
			}
		}
		if !matched {
			return true
		}
	}
	return false
}

func selectorMatches(sel metav1.LabelSelector, pod *corev1.Pod) bool {
	if pod == nil {
		return false
	}
	s, err := metav1.LabelSelectorAsSelector(&sel)
	if err != nil {
		return false
	}
	if s.Empty() {
		return true
	}
	return s.Matches(labels.Set(pod.Labels))
}

func policyTypeIncludes(types []networkingv1.PolicyType, want networkingv1.PolicyType) bool {
	if len(types) == 0 {
		return true
	}
	for _, t := range types {
		if t == want {
			return true
		}
	}
	return false
}

func portsMatch(ports []networkingv1.NetworkPolicyPort, key FlowKey) bool {
	if len(ports) == 0 {
		return true
	}
	for _, p := range ports {
		if p.Protocol != nil && !strings.EqualFold(string(*p.Protocol), key.Proto.String()) {
			continue
		}
		if p.Port == nil {
			return true
		}
		switch p.Port.Type {
		case intstr.Int:
			if p.Port.IntValue() == int(key.DstPort) {
				return true
			}
		case intstr.String:
			if p.Port.StrVal == strconv.Itoa(int(key.DstPort)) {
				return true
			}
		}
	}
	return false
}

func peerMatches(peer networkingv1.NetworkPolicyPeer, pod *corev1.Pod, ip net.IP) bool {
	if peer.IPBlock != nil {
		_, cidr, err := net.ParseCIDR(peer.IPBlock.CIDR)
		if err == nil && cidr != nil && cidr.Contains(ip) {
			for _, ex := range peer.IPBlock.Except {
				_, exCidr, err := net.ParseCIDR(ex)
				if err == nil && exCidr != nil && exCidr.Contains(ip) {
					return false
				}
			}
			return true
		}
	}
	if peer.PodSelector == nil && peer.NamespaceSelector == nil {
		return true
	}
	if pod == nil {
		return false
	}
	if peer.NamespaceSelector != nil {
		sel, err := metav1.LabelSelectorAsSelector(peer.NamespaceSelector)
		if err != nil {
			return false
		}
		if !sel.Matches(labels.Set{"kubernetes.io/metadata.name": pod.Namespace}) {
			return false
		}
	}
	if peer.PodSelector != nil {
		sel, err := metav1.LabelSelectorAsSelector(peer.PodSelector)
		if err != nil {
			return false
		}
		if !sel.Matches(labels.Set(pod.Labels)) {
			return false
		}
	}
	return true
}
