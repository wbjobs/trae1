//go:build ignore

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_endian.h>
#include <linux/pkt_cls.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>

#define FLOWMAP_MAX_ENTRIES    65536
#define IPMETA_MAX_ENTRIES     65536
#define BLOCKED_MAX_ENTRIES    32768
#define EVENTS_PERF_SIZE       4096

#define IP_STATE_ACTIVE        1
#define IP_STATE_RELEASED      2

#define ENFORCE_OFF            0
#define ENFORCE_ON             1

struct flow_key {
    __u32 saddr;
    __u32 daddr;
    __u16 dport;
    __u8  proto;
    __u8  pad;
};

struct flow_value {
    __u64 packets;
    __u64 bytes;
    __u64 first_ts;
    __u64 last_ts;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, FLOWMAP_MAX_ENTRIES);
    __type(key, struct flow_key);
    __type(value, struct flow_value);
} flows SEC(".maps");

struct pod_meta {
    __u8  state;
    __u8  pad[7];
    __u64 updated_at_ns;
    __u64 name_hash;
};

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, IPMETA_MAX_ENTRIES);
    __type(key, __u32);
    __type(value, struct pod_meta);
} ip_meta SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u8);
} enforce_mode SEC(".maps");

struct blocked_value {
    __u64 packets;
    __u64 bytes;
    __u64 first_ts;
    __u64 last_ts;
};

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, BLOCKED_MAX_ENTRIES);
    __type(key, struct flow_key);
    __type(value, struct blocked_value);
} blocked SEC(".maps");

struct flow_event {
    __u32 saddr;
    __u32 daddr;
    __u16 sport;
    __u16 dport;
    __u8  proto;
    __u8  pad[3];
    __u64 bytes;
    __u64 ts_ns;
};

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(max_entries, 1024);
} events SEC(".maps");

struct block_event {
    __u32 saddr;
    __u32 daddr;
    __u16 sport;
    __u16 dport;
    __u8  proto;
    __u8  pad[3];
    __u32 seq;
    __u32 ack;
    __u16 tcp_flags;
    __u16 pad2;
    __u64 ts_ns;
};

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(max_entries, 1024);
} block_events SEC(".maps");

static __always_inline int is_enforce_on(void) {
    __u32 key = 0;
    __u8 *val = bpf_map_lookup_elem(&enforce_mode, &key);
    return val && *val == ENFORCE_ON;
}

static __always_inline int handle_packet(struct __sk_buff *skb, int is_egress) {
    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end) return TC_ACT_OK;

    if (eth->h_proto != __constant_htons(ETH_P_IP)) return TC_ACT_OK;

    struct iphdr *ip = data + sizeof(*eth);
    if ((void *)(ip + 1) > data_end) return TC_ACT_OK;

    __u8 proto = ip->protocol;
    if (proto != IPPROTO_TCP && proto != IPPROTO_UDP) return TC_ACT_OK;

    __u32 saddr = ip->saddr;
    __u32 daddr = ip->daddr;

    __u16 sport = 0;
    __u16 dport = 0;
    __u32 l4_off = sizeof(*eth) + ip->ihl * 4;

    __u32 tcp_seq = 0;
    __u32 tcp_ack = 0;
    __u16 tcp_flags = 0;

    if (proto == IPPROTO_TCP) {
        struct tcphdr *tcp = data + l4_off;
        if ((void *)(tcp + 1) > data_end) return TC_ACT_OK;
        sport = tcp->source;
        dport = tcp->dest;
        tcp_seq = tcp->seq;
        tcp_ack = tcp->ack_seq;
        tcp_flags = *((__u8 *)tcp + 13);
    } else if (proto == IPPROTO_UDP) {
        struct udphdr *udp = data + l4_off;
        if ((void *)(udp + 1) > data_end) return TC_ACT_OK;
        sport = udp->source;
        dport = udp->dest;
    }

    struct flow_key key = {};
    key.saddr = saddr;
    key.daddr = daddr;
    key.dport = dport;
    key.proto = proto;

    __u64 now = bpf_ktime_get_ns();
    __u64 pkt_bytes = skb->len;

    if (is_enforce_on()) {
        struct blocked_value *bv = bpf_map_lookup_elem(&blocked, &key);
        if (bv) {
            __sync_fetch_and_add(&bv->packets, 1);
            __sync_fetch_and_add(&bv->bytes, pkt_bytes);
            bv->last_ts = now;

            bpf_printk("BLOCK saddr=%x daddr=%x sport=%u dport=%u proto=%u bytes=%llu\n",
                       saddr, daddr, __builtin_bswap16(sport), __builtin_bswap16(dport), proto, pkt_bytes);

            if (proto == IPPROTO_TCP) {
                struct block_event bev = {};
                bev.saddr = saddr;
                bev.daddr = daddr;
                bev.sport = __builtin_bswap16(sport);
                bev.dport = __builtin_bswap16(dport);
                bev.proto = proto;
                bev.seq = tcp_seq;
                bev.ack = tcp_ack;
                bev.tcp_flags = tcp_flags;
                bev.ts_ns = now;
                bpf_perf_event_output(skb, &block_events, BPF_F_CURRENT_CPU, &bev, sizeof(bev));
            }

            return TC_ACT_SHOT;
        }
    }

    struct flow_value *val = bpf_map_lookup_elem(&flows, &key);
    if (val) {
        __sync_fetch_and_add(&val->packets, 1);
        __sync_fetch_and_add(&val->bytes, pkt_bytes);
        val->last_ts = now;
    } else {
        struct flow_value nv = {};
        nv.packets = 1;
        nv.bytes = pkt_bytes;
        nv.first_ts = now;
        nv.last_ts = now;
        bpf_map_update_elem(&flows, &key, &nv, BPF_ANY);

        struct flow_event ev = {};
        ev.saddr = saddr;
        ev.daddr = daddr;
        ev.sport = __builtin_bswap16(sport);
        ev.dport = __builtin_bswap16(dport);
        ev.proto = proto;
        ev.bytes = pkt_bytes;
        ev.ts_ns = now;
        bpf_perf_event_output(skb, &events, BPF_F_CURRENT_CPU, &ev, sizeof(ev));
    }

    return TC_ACT_OK;
}

SEC("classifier/ingress")
int handle_ingress(struct __sk_buff *skb) {
    return handle_packet(skb, 0);
}

SEC("classifier/egress")
int handle_egress(struct __sk_buff *skb) {
    return handle_packet(skb, 1);
}

char LICENSE[] SEC("license") = "GPL";
