#ifndef __XDP_K8S_ACCEL_H
#define __XDP_K8S_ACCEL_H

#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>

#define MAX_RULES 10000
#define LOG_SAMPLE_RATE 1000
#define MAX_SUBPROGS 5
#define RULES_PER_SUBPROG 1000
#define MAX_L7_RULES 5000
#define MAX_BYPASS_ENTRIES 1000

enum rule_action {
	ACTION_ALLOW = 0,
	ACTION_DENY = 1,
};

enum {
	TAIL_CALL_INDEX = 0,
};

enum l7_protocol {
	L7_PROTOCOL_UNKNOWN = 0,
	L7_PROTOCOL_HTTP = 1,
	L7_PROTOCOL_GRPC = 2,
	L7_PROTOCOL_HTTP2 = 3,
};

enum l7_decision {
	L7_DECISION_PASS = 0,
	L7_DECISION_DENY = 1,
	L7_DECISION_REDIRECT = 2,
};

struct rule_key {
	__be32 src_ip;
	__be32 dst_ip;
	__be16 src_port;
	__be16 dst_port;
	__u8 protocol;
};

struct l1_key {
	__be32 dst_ip;
};

struct l1_value {
	__u32 l2_map_fd;
	__u32 rule_count;
};

struct l2_key {
	__be16 dst_port;
	__be16 src_port;
	__u8 protocol;
};

struct rule_value {
	__u8 action;
	__u32 rule_id;
};

struct bypass_key {
	__be32 src_ip;
	__be32 dst_ip;
	__be16 src_port;
	__be16 dst_port;
	__u8 protocol;
};

struct bypass_value {
	__u8 bypass_l7;
	__u64 last_access;
};

struct stats {
	__u64 packets_processed;
	__u64 packets_allowed;
	__u64 packets_denied;
	__u64 bytes_processed;
	__u64 lookup_time_ns;
	__u64 tail_calls;
	__u64 l7_redirects;
	__u64 l7_bypasses;
	__u64 bypass_hits;
};

struct perf_test_result {
	__u64 total_packets;
	__u64 total_lookups;
	__u64 avg_lookup_ns;
	__u64 max_lookup_ns;
	__u64 min_lookup_ns;
	__u64 dropped;
};

struct log_event {
	__u32 rule_id;
	__u8 action;
	__be32 src_ip;
	__be32 dst_ip;
	__be16 src_port;
	__be16 dst_port;
	__u8 protocol;
};

struct packet_ctx {
	__be32 src_ip;
	__be32 dst_ip;
	__be16 src_port;
	__be16 dst_port;
	__u8 protocol;
	__u64 timestamp;
	__u32 rule_id;
	__u8 action;
};

struct l7_context {
	__u32 src_ip;
	__u32 dst_ip;
	__u16 src_port;
	__u16 dst_port;
	__u8 protocol;
	__u8 l7_protocol;
	__u64 flow_id;
	__u64 timestamp;
};

struct l7_decision {
	__u64 flow_id;
	__u8 decision;
	__u8 action;
	__u32 rule_id;
	__u64 processing_time_ns;
	char http_host[128];
	char http_path[256];
	char grpc_method[64];
};

struct l7_stats {
	__u64 total_l7_requests;
	__u64 l7_allowed;
	__u64 l7_denied;
	__u64 l7_redirects;
	__u64 l7_bypassed;
	__u64 avg_processing_ns;
	__u64 p50_latency_ns;
	__u64 p90_latency_ns;
	__u64 p99_latency_ns;
	__u64 wasm_executions;
	__u64 wasm_exec_time_ns;
};

struct l7_rule_key {
	__be32 dst_ip;
	__be16 dst_port;
	__u8 protocol;
	__u8 l7_protocol;
	char http_host[64];
	char http_path[128];
	char grpc_method[32];
};

struct l7_rule_value {
	__u8 action;
	__u32 rule_id;
	__u64 hits;
	__u8 wasm_plugin_id;
};

struct wasm_result {
	__u64 flow_id;
	__u8 decision;
	__u64 exec_time_ns;
	char error_msg[128];
};

struct bypass_stats {
	__u64 total_bypasses;
	__u64 bypass_allowed;
	__u64 bypass_denied;
	__u64 avg_bypass_latency_ns;
};

#endif /* __XDP_K8S_ACCEL_H */
