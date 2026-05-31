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

static __always_inline struct rule_value* l2_lookup(__u32 subprog_idx, struct l2_key *l2_key)
{
	void *inner_map;
	__u32 key = subprog_idx;

	inner_map = bpf_map_lookup_elem(&l2_tables, &key);
	if (!inner_map)
		return NULL;

	return bpf_map_lookup_elem(inner_map, l2_key);
}

SEC("xdp")
int xdp_dispatcher(struct xdp_md *ctx)
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

	l1k.dst_ip = ip->daddr;

	l1v = bpf_map_lookup_elem(&l1_table, &l1k);
	if (l1v) {
		subprog_idx = l1v->l2_map_fd;

		void *l4 = (void *)(ip + 1);
		parse_ports(l4, data_end, ip->protocol, &sport, &dport);

		l2k.dst_port = dport;
		l2k.src_port = sport;
		l2k.protocol = ip->protocol;

		rule = l2_lookup(subprog_idx, &l2k);
		if (rule) {
			action = rule->action;
			struct rule_key full_key = {
				.src_ip = ip->saddr,
				.dst_ip = ip->daddr,
				.src_port = sport,
				.dst_port = dport,
				.protocol = ip->protocol
			};
			sample_log(&full_key, rule);
		}
	}

	lookup_ns = bpf_ktime_get_ns() - start_ns;
	update_stats(pkt_len, action, lookup_ns);
	update_perf_stats(lookup_ns);

	if (action == ACTION_DENY)
		return XDP_DROP;

pass:
	return XDP_PASS;
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

SEC("xdp")
int xdp_perf_test(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	struct iphdr *ip;
	struct l1_key l1k = {};
	struct l1_value *l1v;
	struct l2_key l2k = {};
	__u64 start_ns, lookup_ns;
	__u32 subprog_idx;

	if ((void *)eth + sizeof(*eth) > data_end)
		return XDP_PASS;

	if (eth->h_proto != bpf_htons(ETH_P_IP))
		return XDP_PASS;

	ip = (void *)(eth + 1);
	if ((void *)ip + sizeof(*ip) > data_end)
		return XDP_PASS;

	start_ns = bpf_ktime_get_ns();

	l1k.dst_ip = ip->daddr;
	l1v = bpf_map_lookup_elem(&l1_table, &l1k);

	if (l1v) {
		subprog_idx = l1v->l2_map_fd;
		l2k.dst_port = 0;
		l2k.src_port = 0;
		l2k.protocol = ip->protocol;

		l2_lookup(subprog_idx, &l2k);
	}

	lookup_ns = bpf_ktime_get_ns() - start_ns;
	update_perf_stats(lookup_ns);

	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
