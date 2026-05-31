#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_tracing.h>
#include "xdp_k8s_accel.h"

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, MAX_RULES);
	__type(key, struct rule_key);
	__type(value, struct rule_value);
} rules SEC(".maps");

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

static __always_inline void update_stats(__u64 bytes, __u8 action)
{
	__u32 key = 0;
	struct stats *s;

	s = bpf_map_lookup_elem(&stats, &key);
	if (!s)
		return;

	__sync_fetch_and_add(&s->packets_processed, 1);
	__sync_fetch_and_add(&s->bytes_processed, bytes);

	if (action == ACTION_ALLOW)
		__sync_fetch_and_add(&s->packets_allowed, 1);
	else
		__sync_fetch_and_add(&s->packets_denied, 1);
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

static __always_inline struct rule_value *lookup_rule(struct rule_key *key)
{
	struct rule_value *v;
	struct rule_key k;

	k = *key;
	k.src_port = 0;
	v = bpf_map_lookup_elem(&rules, &k);
	if (v)
		return v;

	k = *key;
	k.dst_port = 0;
	v = bpf_map_lookup_elem(&rules, &k);
	if (v)
		return v;

	k = *key;
	k.src_port = 0;
	k.dst_port = 0;
	v = bpf_map_lookup_elem(&rules, &k);
	if (v)
		return v;

	k = *key;
	k.src_ip = 0;
	v = bpf_map_lookup_elem(&rules, &k);
	if (v)
		return v;

	k = *key;
	k.dst_ip = 0;
	v = bpf_map_lookup_elem(&rules, &k);
	if (v)
		return v;

	k = *key;
	k.src_ip = 0;
	k.dst_ip = 0;
	v = bpf_map_lookup_elem(&rules, &k);
	if (v)
		return v;

	return bpf_map_lookup_elem(&rules, key);
}

SEC("xdp")
int xdp_k8s_accel_prog(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	struct iphdr *ip;
	struct rule_key key = {};
	struct rule_value *rule;
	__u64 pkt_len;
	__u16 sport = 0, dport = 0;
	__u8 action = ACTION_ALLOW;

	pkt_len = data_end - data;

	if ((void *)eth + sizeof(*eth) > data_end)
		goto pass;

	if (eth->h_proto != bpf_htons(ETH_P_IP))
		goto pass;

	ip = (void *)(eth + 1);
	if ((void *)ip + sizeof(*ip) > data_end)
		goto pass;

	key.src_ip = ip->saddr;
	key.dst_ip = ip->daddr;
	key.protocol = ip->protocol;

	void *l4 = (void *)(ip + 1);
	parse_ports(l4, data_end, ip->protocol, &sport, &dport);
	key.src_port = sport;
	key.dst_port = dport;

	rule = lookup_rule(&key);
	if (rule) {
		action = rule->action;
		sample_log(&key, rule);
	}

	update_stats(pkt_len, action);

	if (action == ACTION_DENY)
		return XDP_DROP;

pass:
	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
