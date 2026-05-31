#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_jit.h>
#include "xdp_k8s_accel.h"

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, MAX_SUBPROGS);
	__type(key, __u32);
	__type(value, __u32);
} l1_table SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, MAX_SUBPROGS);
	__type(key, __u32);
	__type(value, __u32);
	__uint(map_flags, BPF_F_INNER_MAP);
} l2_tables SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, MAX_BYPASS_ENTRIES);
	__type(key, struct bypass_key);
	__type(value, struct bypass_value);
} bypass_table SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, MAX_L7_RULES);
	__type(key, struct l7_rule_key);
	__type(value, struct l7_rule_value);
} l7_rules SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct stats);
} stats SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 1 << 20);
} events SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 1 << 22);
} l7_decisions SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} log_counter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct perf_test_result);
} perf_results SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct l7_stats);
} l7_stats SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct bypass_stats);
} bypass_stats_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_XSKMAP);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} xsk_map SEC(".maps");

static __always_inline __u16 parse_ports(void *data, void *data_end, __u8 protocol,
					  __u16 *sport, __u16 *dport)
{
	if (protocol == IPPROTO_TCP) {
		struct tcphdr *tcp = data;
		if ((void *)tcp + sizeof(*tcp) > data_end)
			return 0;
		*sport = tcp->source;
		*dport = tcp->dest;
		return 1;
	} else if (protocol == IPPROTO_UDP) {
		struct udphdr *udp = data;
		if ((void *)udp + sizeof(*udp) > data_end)
			return 0;
		*sport = udp->source;
		*dport = udp->dest;
		return 1;
	}
	return 1;
}

static __always_inline void update_stats(__u64 bytes, __u8 action, __u64 lookup_ns)
{
	__u32 key = 0;
	struct stats *s;

	s = bpf_map_lookup_elem(&stats, &key);
	if (!s)
		return;

	__sync_fetch_and_add(&s->packets_processed, 1);
	__sync_fetch_and_add(&s->bytes_processed, bytes);
	__sync_fetch_and_add(&s->lookup_time_ns, lookup_ns);

	if (action == ACTION_ALLOW)
		__sync_fetch_and_add(&s->packets_allowed, 1);
	else
		__sync_fetch_and_add(&s->packets_denied, 1);
}

static __always_inline void update_l7_stats(__u64 processing_ns, __u8 decision)
{
	__u32 key = 0;
	struct l7_stats *s;

	s = bpf_map_lookup_elem(&l7_stats, &key);
	if (!s)
		return;

	__sync_fetch_and_add(&s->total_l7_requests, 1);
	__sync_fetch_and_add(&s->avg_processing_ns, processing_ns);

	if (decision == L7_DECISION_PASS)
		__sync_fetch_and_add(&s->l7_allowed, 1);
	else if (decision == L7_DECISION_DENY)
		__sync_fetch_and_add(&s->l7_denied, 1);
	else
		__sync_fetch_and_add(&s->l7_redirects, 1);
}

static __always_inline void update_perf_stats(__u64 lookup_ns)
{
	__u32 key = 0;
	struct perf_test_result *r;

	r = bpf_map_lookup_elem(&perf_results, &key);
	if (!r)
		return;

	__sync_fetch_and_add(&r->total_packets, 1);
	__sync_fetch_and_add(&r->total_lookups, 1);

	__u64 old_avg = r->avg_lookup_ns;
	__u64 old_count = r->total_lookups - 1;
	if (old_count > 0) {
		__sync_fetch_and_add(&r->avg_lookup_ns,
				   (lookup_ns - old_avg) / old_count);
	}

	__u64 old_max = r->max_lookup_ns;
	while (lookup_ns > old_max) {
		__sync_bool_cmp_shy(&r->max_lookup_ns, old_max, lookup_ns);
		old_max = r->max_lookup_ns;
	}

	__u64 old_min = r->min_lookup_ns;
	if (old_min == 0 || lookup_ns < old_min) {
		__sync_bool_cmp_shy(&r->min_lookup_ns, old_min, lookup_ns);
	}
}

static __always_inline void update_bypass_stats(__u64 latency_ns, __u8 action)
{
	__u32 key = 0;
	struct bypass_stats *s;

	s = bpf_map_lookup_elem(&bypass_stats_map, &key);
	if (!s)
		return;

	__sync_fetch_and_add(&s->total_bypasses, 1);
	__sync_fetch_and_add(&s->avg_bypass_latency_ns, latency_ns);

	if (action == ACTION_ALLOW)
		__sync_fetch_and_add(&s->bypass_allowed, 1);
	else
		__sync_fetch_and_add(&s->bypass_denied, 1);
}

static __always_inline struct rule_value* l2_lookup(__u32 subprog_idx, struct l2_key *l2_key)
{
	void *inner_map;
	__u32 key = subprog_idx;

	inner_map = bpf_map_lookup_elem(&l2_tables, &key);
	if (!inner_map)
		return NULL;

	return bpf_map_lookup_elem(inner_map, l2_key);
}

static __always_inline __u8 check_bypass(struct rule_key *key)
{
	struct bypass_key bk = {
		.src_ip = key->src_ip,
		.dst_ip = key->dst_ip,
		.src_port = key->src_port,
		.dst_port = key->dst_port,
		.protocol = key->protocol,
	};
	struct bypass_value *bv;

	bv = bpf_map_lookup_elem(&bypass_table, &bk);
	if (bv && bv->bypass_l7) {
		return 1;
	}

	bk.src_port = 0;
	bv = bpf_map_lookup_elem(&bypass_table, &bk);
	if (bv && bv->bypass_l7) {
		return 1;
	}

	bk.dst_port = 0;
	bk.src_port = key->src_port;
	bv = bpf_map_lookup_elem(&bypass_table, &bk);
	if (bv && bv->bypass_l7) {
		return 1;
	}

	return 0;
}

static __always_inline void send_l7_decision(struct l7_context *ctx, __u8 decision, __u32 rule_id)
{
	struct l7_decision *d;

	d = bpf_ringbuf_reserve(&l7_decisions, sizeof(*d), 0);
	if (!d)
		return;

	d->flow_id = ctx->flow_id;
	d->decision = decision;
	d->action = ACTION_ALLOW;
	d->rule_id = rule_id;
	d->processing_time_ns = bpf_ktime_get_ns() - ctx->timestamp;

	bpf_ringbuf_submit(d, 0);
}

static __always_inline void sample_log(struct rule_key *key, struct rule_value *value)
{
	__u32 counter_key = 0;
	__u64 *counter;

	counter = bpf_map_lookup_elem(&log_counter, &counter_key);
	if (!counter)
		return;

	__u64 current = *counter;
	*counter = current + 1;

	if (current % LOG_SAMPLE_RATE != 0)
		return;

	struct log_event *e;
	e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
	if (!e)
		return;

	e->rule_id = value->rule_id;
	e->action = value->action;
	e->src_ip = key->src_ip;
	e->dst_ip = key->dst_ip;
	e->src_port = key->src_port;
	e->dst_port = key->dst_port;
	e->protocol = key->protocol;

	bpf_ringbuf_submit(e, 0);
}

static __always_inline __u8 l7_needs_check(__be16 dst_port)
{
	if (dst_port == bpf_htons(80) || dst_port == bpf_htons(8080))
		return L7_PROTOCOL_HTTP;
	if (dst_port == bpf_htons(443) || dst_port == bpf_htons(8443))
		return L7_PROTOCOL_HTTP2;
	if (dst_port == bpf_htons(50051))
		return L7_PROTOCOL_GRPC;
	return L7_PROTOCOL_UNKNOWN;
}

SEC("xdp")
int xdp_l7_filter(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	struct iphdr *ip;
	struct l1_key l1k = {};
	struct l1_value *l1v;
	struct l2_key l2k = {};
	struct rule_value *rule;
	__u64 pkt_len, start_ns, lookup_ns;
	__u16 sport = 0, dport = 0;
	__u8 action = ACTION_ALLOW;
	__u32 subprog_idx;

	pkt_len = data_end - data;
	start_ns = bpf_ktime_get_ns();

	if ((void *)eth + sizeof(*eth) > data_end)
		goto pass;

	if (eth->h_proto != bpf_htons(ETH_P_IP))
		goto pass;

	ip = (void *)(eth + 1);
	if ((void *)ip + sizeof(*ip) > data_end)
		goto pass;

	struct rule_key rule_key = {
		.src_ip = ip->saddr,
		.dst_ip = ip->daddr,
		.protocol = ip->protocol,
	};

	void *l4 = (void *)(ip + 1);
	parse_ports(l4, data_end, ip->protocol, &sport, &dport);
	rule_key.src_port = sport;
	rule_key.dst_port = dport;

	if (check_bypass(&rule_key)) {
		__u32 stats_key = 0;
		struct stats *s = bpf_map_lookup_elem(&stats, &stats_key);
		if (s)
			__sync_fetch_and_add(&s->l7_bypasses, 1);

		update_bypass_stats(bpf_ktime_get_ns() - start_ns, ACTION_ALLOW);
		goto pass;
	}

	l1k.dst_ip = ip->daddr;

	l1v = bpf_map_lookup_elem(&l1_table, &l1k);
	if (l1v) {
		subprog_idx = l1v->l2_map_fd;

		l2k.dst_port = dport;
		l2k.src_port = sport;
		l2k.protocol = ip->protocol;

		rule = l2_lookup(subprog_idx, &l2k);
		if (rule) {
			action = rule->action;
			sample_log(&rule_key, rule);

			if (action == ACTION_DENY) {
				update_stats(pkt_len, action, bpf_ktime_get_ns() - start_ns);
				return XDP_DROP;
			}

			__u8 l7_proto = l7_needs_check(dport);
			if (l7_proto != L7_PROTOCOL_UNKNOWN) {
				__u32 stats_key = 0;
				struct stats *s = bpf_map_lookup_elem(&stats, &stats_key);
				if (s)
					__sync_fetch_and_add(&s->l7_redirects, 1);

				struct l7_context l7_ctx = {
					.src_ip = ip->saddr,
					.dst_ip = ip->daddr,
					.src_port = sport,
					.dst_port = dport,
					.protocol = ip->protocol,
					.l7_protocol = l7_proto,
					.flow_id = (__u64)ip->saddr << 32 | (__u64)dport,
					.timestamp = bpf_ktime_get_ns(),
				};

				send_l7_decision(&l7_ctx, L7_DECISION_REDIRECT, rule->rule_id);

				return XDP_REDIRECT;
			}
		}
	}

	lookup_ns = bpf_ktime_get_ns() - start_ns;
	update_stats(pkt_len, action, lookup_ns);
	update_perf_stats(lookup_ns);

pass:
	return XDP_PASS;
}

SEC("xdp")
int xdp_l7_bypass_only(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	struct iphdr *ip;
	struct rule_key rule_key = {};
	__u64 start_ns;

	start_ns = bpf_ktime_get_ns();

	if ((void *)eth + sizeof(*eth) > data_end)
		return XDP_PASS;

	if (eth->h_proto != bpf_htons(ETH_P_IP))
		return XDP_PASS;

	ip = (void *)(eth + 1);
	if ((void *)ip + sizeof(*ip) > data_end)
		return XDP_PASS;

	rule_key.src_ip = ip->saddr;
	rule_key.dst_ip = ip->daddr;
	rule_key.protocol = ip->protocol;

	void *l4 = (void *)(ip + 1);
	parse_ports(l4, data_end, ip->protocol, &rule_key.src_port, &rule_key.dst_port);

	if (check_bypass(&rule_key)) {
		__u32 stats_key = 0;
		struct stats *s = bpf_map_lookup_elem(&stats, &stats_key);
		if (s)
			__sync_fetch_and_add(&s->bypass_hits, 1);

		update_bypass_stats(bpf_ktime_get_ns() - start_ns, ACTION_ALLOW);
		return XDP_PASS;
	}

	return XDP_DROP;
}

SEC("xdp.subprog")
int xdp_subprog_0(struct xdp_md *ctx)
{
	return XDP_PASS;
}

SEC("xdp.subprog")
int xdp_subprog_1(struct xdp_md *ctx)
{
	return XDP_PASS;
}

SEC("xdp.subprog")
int xdp_subprog_2(struct xdp_md *ctx)
{
	return XDP_PASS;
}

SEC("xdp.subprog")
int xdp_subprog_3(struct xdp_md *ctx)
{
	return XDP_PASS;
}

SEC("xdp.subprog")
int xdp_subprog_4(struct xdp_md *ctx)
{
	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
