/* SPDX-License-Identifier: BSD-3-Clause
 * DPDK Traffic Parsing Engine - main / DPDK init / lcore pipelines
 *
 * Pipeline (per RX lcore):
 *   rte_eth_rx_burst (zero-copy)
 *     -> te_frag_reassemble (IP fragment reassembly, Teardrop detection)
 *     -> te_parse_packet
 *     -> te_flow_get_or_create (hash table aggregation)
 *     -> te_classify (port + DPI)
 *     -> te_result_enqueue (ring buffer)
 *     -> rte_pktmbuf_free
 *
 * Stats lcore:
 *   - periodic dump of packet rate / classification stats
 *   - flow expiry sweep
 *   - IP fragment death row drain
 *
 * Signal handler:
 *   - SIGINT -> graceful shutdown
 */
#include "te_header.h"
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

/* ------------------------------------------------------------------ */
/*  Global context                                                    */
/* ------------------------------------------------------------------ */
struct te_config g_te;

/* ------------------------------------------------------------------ */
/*  CLI                                                               */
/* ------------------------------------------------------------------ */
static void print_usage(const char *prog)
{
    printf("Usage: %s [EAL options] -- [APP options]\n", prog);
    printf("  -p PORT            Port ID to manage (default 0)\n");
    printf("  --stats            Dump per-second statistics\n");
    printf("  --frag-table-size  IP frag reassembly table size (default %u)\n",
           TE_FRAG_TABLE_SIZE);
    printf("  --frag-timeout     IP frag reassembly timeout in seconds (default %u)\n",
           TE_FRAG_TIMEOUT_SEC);
    printf("  --ml-model PATH    Path to ONNX Random Forest model (enables ML)\n");
    printf("  --ml-disable       Disable ML classifier (default: enabled if model provided)\n");
    printf("  --rpc-port PORT    RPC port for model hot update (default %u)\n",
           TE_ML_RPC_PORT);
    printf("  --rpc-disable      Disable RPC server\n");
    printf("  -h                 Show this help\n");
}

static int parse_app_args(int argc, char **argv,
                          uint16_t *port_id, int *show_stats,
                          uint32_t *frag_table_size, uint32_t *frag_timeout_sec,
                          char *ml_model_path, int *ml_disable,
                          int *rpc_disable, uint16_t *rpc_port)
{
    static struct option long_opts[] = {
        { "stats",          no_argument,       NULL, 's' },
        { "frag-table-size",required_argument, NULL, 'f' },
        { "frag-timeout",   required_argument, NULL, 't' },
        { "ml-model",       required_argument, NULL, 'm' },
        { "ml-disable",     no_argument,       NULL, 'M' },
        { "rpc-port",       required_argument, NULL, 'r' },
        { "rpc-disable",    no_argument,       NULL, 'R' },
        { "help",           no_argument,       NULL, 'h' },
        { NULL,             0,                 NULL,  0  },
    };

    int opt;
    int opt_idx;
    while ((opt = getopt_long(argc, argv, "p:h", long_opts, &opt_idx)) != -1) {
        switch (opt) {
        case 'p':
            *port_id = (uint16_t)atoi(optarg);
            break;
        case 's':
            *show_stats = 1;
            break;
        case 'f':
            *frag_table_size = (uint32_t)atoi(optarg);
            break;
        case 't':
            *frag_timeout_sec = (uint32_t)atoi(optarg);
            break;
        case 'm':
            strncpy(ml_model_path, optarg, TE_ML_MODEL_PATH_MAX - 1);
            ml_model_path[TE_ML_MODEL_PATH_MAX - 1] = '\0';
            break;
        case 'M':
            *ml_disable = 1;
            break;
        case 'r':
            *rpc_port = (uint16_t)atoi(optarg);
            break;
        case 'R':
            *rpc_disable = 1;
            break;
        case 'h':
        default:
            print_usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Signal handler                                                    */
/* ------------------------------------------------------------------ */
static void signal_handler(int sig)
{
    (void)sig;
    g_te.force_quit = 1;
}

/* ------------------------------------------------------------------ */
/*  Ring buffer helper (called by flow_table and RX lcores)           */
/* ------------------------------------------------------------------ */
void te_result_enqueue(const struct te_5tuple *key, enum te_category cat,
                       uint32_t pkt_len, uint64_t tsc)
{
    if (unlikely(g_te.result_ring == NULL || key == NULL))
        return;

    struct te_result_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.seq       = rte_atomic64_add_return(&g_te.seq_counter, 1);
    msg.src_ip    = key->src_ip;
    msg.dst_ip    = key->dst_ip;
    msg.src_port  = key->src_port;
    msg.dst_port  = key->dst_port;
    msg.protocol  = key->protocol;
    msg.category  = (uint8_t)cat;
    msg.pkts      = 1;
    msg.bytes     = pkt_len;
    msg.first_tsc = tsc;
    msg.last_tsc  = tsc;

    if (unlikely(rte_ring_enqueue(g_te.result_ring, &msg) != 0)) {
        if (g_te.lcore_stats)
            g_te.lcore_stats[0].ring_enq_fail++;
    }
}

void te_result_enqueue_flow_aggregate(const struct te_flow *flow)
{
    if (unlikely(g_te.result_ring == NULL || flow == NULL))
        return;

    struct te_result_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.seq       = rte_atomic64_add_return(&g_te.seq_counter, 1);
    msg.src_ip    = flow->key.src_ip;
    msg.dst_ip    = flow->key.dst_ip;
    msg.src_port  = flow->key.src_port;
    msg.dst_port  = flow->key.dst_port;
    msg.protocol  = flow->key.protocol;
    msg.category  = (uint8_t)flow->category;
    msg.pkts      = flow->pkts;
    msg.bytes     = flow->bytes;
    msg.first_tsc = flow->first_tsc;
    msg.last_tsc  = flow->last_tsc;
    msg.dpi_category = flow->dpi_category;
    msg.ml_category  = flow->ml_category;
    msg.ml_confidence = flow->ml_confidence;

    if (unlikely(rte_ring_enqueue(g_te.result_ring, &msg) != 0)) {
        if (g_te.lcore_stats)
            g_te.lcore_stats[0].ring_enq_fail++;
    }
}

/* ------------------------------------------------------------------ */
/*  DPDK port setup                                                   */
/* ------------------------------------------------------------------ */
static int port_init(uint16_t port_id, struct rte_mempool *mp)
{
    struct rte_eth_conf port_conf;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_rxconf rxq_conf;
    int ret;

    memset(&port_conf, 0, sizeof(port_conf));
    port_conf.rxmode.max_lro_pkt_size = RTE_ETHER_MAX_LEN;
    port_conf.rxmode.offloads =
        RTE_ETH_RX_OFFLOAD_CHECKSUM |
        RTE_ETH_RX_OFFLOAD_VLAN_STRIP;

    ret = rte_eth_dev_configure(port_id,
                                TE_RX_QUEUES_PER_PORT,
                                TE_TX_QUEUES_PER_PORT,
                                &port_conf);
    if (ret != 0) {
        RTE_LOG(ERR, USER1, "rte_eth_dev_configure failed: %s\n",
                rte_strerror(-ret));
        return -1;
    }

    ret = rte_eth_dev_info_get(port_id, &dev_info);
    if (ret != 0)
        return -1;

    /* Adjust to actual device capabilities */
    uint16_t nb_rx_q = TE_RX_QUEUES_PER_PORT;
    if (nb_rx_q > dev_info.max_rx_queues)
        nb_rx_q = dev_info.max_rx_queues;

    for (uint16_t q = 0; q < nb_rx_q; q++) {
        memset(&rxq_conf, 0, sizeof(rxq_conf));
        rxq_conf.offloads = port_conf.rxmode.offloads;

        ret = rte_eth_rx_queue_setup(port_id, q, TE_RX_RING_SIZE,
                                     rte_eth_dev_socket_id(port_id),
                                     &rxq_conf, mp);
        if (ret != 0) {
            RTE_LOG(ERR, USER1,
                    "rte_eth_rx_queue_setup(q=%u) failed: %s\n",
                    q, rte_strerror(-ret));
            return -1;
        }
    }

    ret = rte_eth_dev_adjust_nb_rx_tx_desc(port_id, &nb_rx_q, NULL);
    if (ret != 0)
        return -1;

    ret = rte_eth_dev_start(port_id);
    if (ret != 0) {
        RTE_LOG(ERR, USER1, "rte_eth_dev_start failed: %s\n",
                rte_strerror(-ret));
        return -1;
    }

    /* Promiscuous mode so we see all traffic on the wire */
    ret = rte_eth_promiscuous_enable(port_id);
    if (ret != 0)
        RTE_LOG(WARNING, USER1, "promiscuous enable failed: %s\n",
                rte_strerror(-ret));

    RTE_LOG(INFO, USER1, "Port %u started (rx_queues=%u)\n",
            port_id, nb_rx_q);
    return 0;
}

static void port_fini(uint16_t port_id)
{
    rte_eth_dev_stop(port_id);
    rte_eth_dev_close(port_id);
}

/* ------------------------------------------------------------------ */
/*  IP fragment reassembly subsystem                                  */
/* ------------------------------------------------------------------ */

/* Per-flow attack log rate-limiting: track last log tsc per flow key */
#define TE_ATTACK_LOG_MAX_ENTRIES  (1U << 12)  /* 4096 entries */

struct attack_log_entry {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t frag_id;
    uint8_t  protocol;
    uint64_t last_log_tsc;
};

static struct attack_log_entry g_attack_log[TE_ATTACK_LOG_MAX_ENTRIES];
static rte_spinlock_t g_attack_log_lock = RTE_SPINLOCK_INITIALIZER;

void te_frag_log_attack(const struct te_frag_attack_log *log)
{
    uint64_t rate_limit_cycles =
        (uint64_t)TE_ATTACK_LOG_RATE_LIMIT * g_te.tsc_hz;

    rte_spinlock_lock(&g_attack_log_lock);

    /* Find or create entry */
    uint32_t slot = rte_jhash(log, sizeof(*log), 0) % TE_ATTACK_LOG_MAX_ENTRIES;
    struct attack_log_entry *e = &g_attack_log[slot];

    if (e->src_ip   == log->src_ip &&
        e->dst_ip   == log->dst_ip &&
        e->frag_id  == log->frag_id &&
        e->protocol == log->protocol) {
        if ((log->tsc - e->last_log_tsc) < rate_limit_cycles) {
            rte_spinlock_unlock(&g_attack_log_lock);
            return; /* rate limited */
        }
    } else {
        e->src_ip   = log->src_ip;
        e->dst_ip   = log->dst_ip;
        e->frag_id  = log->frag_id;
        e->protocol = log->protocol;
    }
    e->last_log_tsc = log->tsc;
    rte_spinlock_unlock(&g_attack_log_lock);

    struct in_addr saddr, daddr;
    saddr.s_addr = log->src_ip;
    daddr.s_addr = log->dst_ip;
    RTE_LOG(WARNING, USER1,
            "[FRAG-ATTACK] potential Teardrop: src=%s dst=%s id=%u proto=%u\n",
            inet_ntoa(saddr), inet_ntoa(daddr),
            log->frag_id, log->protocol);
}

int te_frag_init(void)
{
    uint32_t tbl_size = g_te.frag_table_size ?
                        g_te.frag_table_size : TE_FRAG_TABLE_SIZE;
    uint32_t timeout  = g_te.frag_timeout_sec ?
                        g_te.frag_timeout_sec : TE_FRAG_TIMEOUT_SEC;

    /* Direct mbuf pool for reassembled packets (large, no headroom) */
    char pool_name[64];
    snprintf(pool_name, sizeof(pool_name), "frag_direct_pool");
    g_te.frag_pool = rte_pktmbuf_pool_create(
        pool_name, TE_FRAG_POOL_SIZE, TE_MBUF_CACHE_SIZE,
        0, TE_FRAG_MAX_PKT_SIZE, rte_socket_id());
    if (g_te.frag_pool == NULL) {
        RTE_LOG(ERR, USER1, "frag direct pool create failed\n");
        return -1;
    }

    /* Create IP fragment reassembly table */
    g_te.frag_tbl = rte_ip_frag_table_create(
        tbl_size,                    /* max number of fragment entries */
        0,                           /* flags */
        timeout * MS_PER_S,          /* TTL in ms */
        rte_socket_id());
    if (g_te.frag_tbl == NULL) {
        RTE_LOG(ERR, USER1, "ip_frag_table_create failed (size=%u, ttl=%us)\n",
                tbl_size, timeout);
        rte_mempool_free(g_te.frag_pool);
        g_te.frag_pool = NULL;
        return -1;
    }

    RTE_LOG(INFO, USER1,
            "IP frag table created: size=%u, timeout=%us\n",
            tbl_size, timeout);
    return 0;
}

void te_frag_fini(void)
{
    if (g_te.frag_tbl) {
        rte_ip_frag_table_destroy(g_te.frag_tbl);
        g_te.frag_tbl = NULL;
    }
    if (g_te.frag_pool) {
        rte_mempool_free(g_te.frag_pool);
        g_te.frag_pool = NULL;
    }
}

/*
 * Reassemble IP fragments.
 *
 * Output:
 *   *out_mbuf is set to:
 *     - the original mbuf if not a fragment (caller proceeds normally)
 *     - a reassembled mbuf if all fragments collected
 *     - NULL if not yet complete (mbuf owned by frag table, caller must NOT free)
 *     - NULL on error (caller must free original mbuf)
 *
 *   Return:
 *     -1 : error (caller frees original mbuf)
 *      0 : stored but not complete (caller must NOT free original mbuf)
 *      1 : complete, *out_mbuf is the reassembled packet (caller frees original)
 *      2 : not a fragment, *out_mbuf == original mbuf (caller proceeds)
 *
 * Teardrop detection:
 *   A classic Teardrop attack sends fragments with overlapping offsets
 *   where the second fragment claims to start before the first ends
 *   AND has a smaller length, causing OS kernel panic on old systems.
 *   We detect this by watching for overlapping fragments within the same
 *   (src_ip, dst_ip, id, protocol) flow.
 */
int te_frag_reassemble(struct rte_mbuf *m, uint32_t lcore_idx,
                        struct rte_mbuf **out_mbuf)
{
    struct te_lcore_stats *st = &g_te.lcore_stats[lcore_idx];
    struct rte_mbuf *reassembled;
    uint32_t more_flag;
    uint32_t frag_offset;
    int ret;

    *out_mbuf = NULL;

    if (unlikely(g_te.frag_tbl == NULL)) {
        *out_mbuf = m;
        return 2; /* frag disabled */
    }

    /* Quick check: is this an IPv4 packet at all? */
    if (unlikely(rte_pktmbuf_data_len(m) <
                 sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr))) {
        *out_mbuf = m;
        return 2;
    }

    struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    if (unlikely(rte_be_to_cpu_16(eth->ether_type) != RTE_ETHER_TYPE_IPV4)) {
        *out_mbuf = m;
        return 2;
    }

    struct rte_ipv4_hdr *ipv4 = (struct rte_ipv4_hdr *)(eth + 1);
    uint8_t ip_ver = ipv4->version_ihl >> 4;
    if (unlikely(ip_ver != 4)) {
        *out_mbuf = m;
        return 2;
    }

    /* Check fragmentation flags: MF bit and fragment offset */
    uint16_t frag_off_raw = rte_be_to_cpu_16(ipv4->fragment_offset);
    more_flag  = (frag_off_raw & IPV4_HDR_MF_FLAG) ? 1 : 0;
    frag_offset = (frag_off_raw & IPV4_HDR_OFFSET_MASK) * 8;

    /* Not a fragment: MF=0 AND offset=0 => complete packet */
    if (more_flag == 0 && frag_offset == 0) {
        *out_mbuf = m;
        return 2;
    }

    /* It IS a fragment */
    st->frag_pkts++;

    /* Teardrop heuristic: if fragment offset is non-zero but less than
     * the minimum payload size that would make sense for a non-first
     * fragment, flag it. Also flag zero-offset non-first fragments.
     * Classic Teardrop: frag2.offset < frag1.offset + frag1.length,
     * AND frag2.offset + frag2.length < frag1.offset + frag1.length.
     * We can't fully detect without tracking per-flow offsets here
     * (that's done inside rte_ipv4_frag_reassemble_packet), but we
     * can catch the most obvious malformed fragments. */
    if (unlikely(frag_offset > 0 &&
                 frag_offset < sizeof(struct rte_tcp_hdr) &&
                 ipv4->next_proto_id == IPPROTO_TCP)) {
        /* Suspicious: non-first fragment claiming to start in the middle
         * of a TCP header. Could be Teardrop. */
        struct te_frag_attack_log log;
        memset(&log, 0, sizeof(log));
        log.src_ip   = rte_be_to_cpu_32(ipv4->src_addr);
        log.dst_ip   = rte_be_to_cpu_32(ipv4->dst_addr);
        log.frag_id  = rte_be_to_cpu_16(ipv4->packet_id);
        log.protocol = ipv4->next_proto_id;
        log.tsc      = te_rdtsc();
        te_frag_log_attack(&log);
        st->frag_attack_logs++;
    }

    /* Attempt reassembly */
    ret = rte_ipv4_frag_reassemble_packet(
        g_te.frag_tbl,
        m,
        ipv4,
        &reassembled);

    if (unlikely(ret < 0)) {
        /* Error: frag table full, TTL expiry, etc. */
        st->frag_dropped++;
        return -1;
    }

    if (reassembled != NULL) {
        /* Successfully reassembled into a complete packet */
        st->frag_reassembled++;
        *out_mbuf = reassembled;
        return 1;
    }

    /* Fragment stored but packet not yet complete */
    return 0;
}

void te_frag_drain_death_row(uint64_t now_tsc)
{
    if (g_te.frag_tbl == NULL)
        return;

    /* Free expired fragment entries on the death row.
     * rte_ip_frag_free_death_row is void; expired entries are
     * counted indirectly via frag_dropped / frag_timeouts stats
     * populated in te_frag_reassemble error path. */
    rte_ip_frag_free_death_row(g_te.frag_tbl, 3 /* max loops */);
    (void)now_tsc;
}

/* ------------------------------------------------------------------ */
/*  Per-lcore RX pipeline                                             */
/* ------------------------------------------------------------------ */
struct rx_lcore_arg {
    uint16_t port_id;
    uint16_t queue_id;
    uint32_t lcore_idx;  /* index into g_te.lcore_stats */
};

static int rx_lcore_main(void *arg)
{
    struct rx_lcore_arg *a = (struct rx_lcore_arg *)arg;
    struct te_lcore_stats *st = &g_te.lcore_stats[a->lcore_idx];
    struct rte_mbuf *bufs[TE_BURST_SIZE];
    uint64_t drain_tsc = rte_get_tsc_hz();

    RTE_LOG(INFO, USER1,
            "lcore %u start rx port=%u q=%u\n",
            rte_lcore_id(), a->port_id, a->queue_id);

    while (!g_te.force_quit) {
        uint16_t nb_rx = rte_eth_rx_burst(a->port_id,
                                          a->queue_id,
                                          bufs, TE_BURST_SIZE);
        if (unlikely(nb_rx == 0)) {
            rte_pause();
            continue;
        }

        uint64_t now = te_rdtsc();

        for (uint16_t i = 0; i < nb_rx; i++) {
            struct rte_mbuf *m = bufs[i];
            uint32_t pkt_len = m->pkt_len;

            st->rx_pkts  += 1;
            st->rx_bytes += pkt_len;

            /* ---- IP fragment reassembly (pre-parse) ---- */
            struct rte_mbuf *complete;
            int fr = te_frag_reassemble(m, a->lcore_idx, &complete);

            if (fr < 0) {
                /* Error: frag table full or TTL expiry; free mbuf */
                rte_pktmbuf_free(m);
                continue;
            }
            if (fr == 0) {
                /* Stored but not complete; frag table owns mbuf, do NOT free */
                continue;
            }
            if (fr == 1) {
                /* Reassembled complete; free original fragment mbuf */
                rte_pktmbuf_free(m);
                m = complete;
            }
            /* fr == 2: not a fragment; proceed with original mbuf */

            struct te_5tuple key;
            uint16_t l4_len;
            uint8_t *l4_payload;
            uint16_t payload_len;

            int rc = te_parse_packet(m, &key, &l4_len,
                                     &l4_payload, &payload_len);
            if (unlikely(rc != 0)) {
                st->parse_err++;
                rte_pktmbuf_free(m);
                continue;
            }

            bool is_new;
            struct te_flow *flow = te_flow_get_or_create(
                &key, pkt_len, now, &is_new);
            if (unlikely(flow == NULL)) {
                rte_pktmbuf_free(m);
                continue;
            }

            if (is_new)
                st->new_flows++;
            else
                st->update_flows++;

            /* ---- ML feature accumulation (always) ---- */
            if (flow->features != NULL && g_te.ml_enabled) {
                te_ml_feature_accumulate(flow->features, m, &key,
                                          l4_payload, payload_len, now);
            }

            /* Classification is cached on first packet to save cost */
            enum te_category cat;
            if (is_new || flow->category == TE_CAT_UNKNOWN) {
                /* DPI classification */
                enum te_category dpi_cat = te_classify(
                    &key, l4_payload, payload_len);
                flow->dpi_category = (uint8_t)dpi_cat;

                /* DPI confidence heuristic:
                 *   - port-based high confidence (0.9)
                 *   - DPI regex match high confidence (0.85)
                 *   - unknown low confidence (0.1) */
                float dpi_conf = (dpi_cat != TE_CAT_UNKNOWN) ? 0.9f : 0.1f;
                flow->dpi_confidence = dpi_conf;

                /* ML inference: run on every new flow that has at least
                 * 5 packets accumulated, or every 10 packets thereafter.
                 * For the first packet we run inference if features are
                 * ready (though limited data) to meet latency target. */
                enum te_category ml_cat = TE_CAT_UNKNOWN;
                float ml_conf = 0.0f;
                float ml_proba[TE_NB_CATEGORIES] = {0};

                if (g_te.ml_enabled && g_ml_ctx && g_ml_ctx->ort_session &&
                    flow->features != NULL) {

                    /* Extract feature vector */
                    te_ml_feature_extract(flow->features, &key,
                                           flow->features->feature_vec);

                    /* Run ONNX inference (target <100us) */
                    uint64_t lat_us = 0;
                    if (te_ml_inference(flow->features->feature_vec,
                                         &ml_cat, &ml_conf,
                                         ml_proba, &lat_us) == 0) {
                        flow->ml_category = ml_cat;
                        flow->ml_confidence = ml_conf;
                        memcpy(flow->ml_proba, ml_proba,
                               sizeof(ml_proba));
                    }
                }

                /* Weighted fusion of DPI + ML */
                if (g_te.ml_enabled && flow->ml_confidence > 0.0f) {
                    cat = te_ml_fuse(dpi_cat, dpi_conf,
                                      ml_cat, ml_conf, ml_proba);
                } else {
                    cat = dpi_cat;
                }

                flow->category = cat;
            } else {
                cat = (enum te_category)flow->category;
            }
            st->cat_counts[cat]++;

            /* Periodic ML re-inference every 10 packets per flow
             * to refine classification as more data arrives */
            if (g_te.ml_enabled && flow->features &&
                (flow->pkts % 10 == 0) && flow->pkts >= 10) {

                te_ml_feature_extract(flow->features, &key,
                                       flow->features->feature_vec);

                uint8_t ml_cat;
                float ml_conf;
                float ml_proba[TE_NB_CATEGORIES];
                if (te_ml_inference(flow->features->feature_vec,
                                     &ml_cat, &ml_conf,
                                     ml_proba, NULL) == 0) {
                    flow->ml_category = ml_cat;
                    flow->ml_confidence = ml_conf;
                    memcpy(flow->ml_proba, ml_proba, sizeof(ml_proba));

                    /* Re-fuse with DPI */
                    cat = te_ml_fuse(
                        (enum te_category)flow->dpi_category,
                        flow->dpi_confidence,
                        ml_cat, ml_conf, ml_proba);
                    flow->category = cat;
                }
            }

            /* Send per-packet classification result via ring */
            te_result_enqueue(&key, cat, pkt_len, now);

            rte_pktmbuf_free(m);
        }

        /* Periodic drain */
        if (unlikely(now - drain_tsc > g_te.tsc_hz))
            drain_tsc = now;
    }

    RTE_LOG(INFO, USER1, "lcore %u exit (rx_pkts=%" PRIu64 ")\n",
            rte_lcore_id(), st->rx_pkts);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Stats lcore: periodic dump + flow expiry                          */
/* ------------------------------------------------------------------ */
static int stats_lcore_main(void *arg)
{
    (void)arg;

    while (!g_te.force_quit) {
        uint64_t now = te_rdtsc();

        if (g_te.show_stats)
            te_stats_periodic(now);

        /* Expire idle flows every 5 seconds */
        static uint64_t last_expire = 0;
        if (last_expire == 0)
            last_expire = now;
        if ((now - last_expire) > 5ULL * g_te.tsc_hz) {
            te_flow_expire(now,
                (uint64_t)TE_FLOW_TIMEOUT_SEC * g_te.tsc_hz);
            /* Also drain IP fragment death row */
            te_frag_drain_death_row(now);
            last_expire = now;
        }

        rte_delay_us_sleep(200 * 1000);
    }

    /* Final dump before exit */
    if (g_te.show_stats)
        te_stats_dump();

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    int ret;
    uint16_t port_id = 0;
    int show_stats  = 0;

    /* 1. DPDK EAL init */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "rte_eal_init failed: %s\n",
                 rte_strerror(-ret));
    argc -= ret;
    argv += ret;

    /* 2. App args */
    uint32_t frag_table_size = TE_FRAG_TABLE_SIZE;
    uint32_t frag_timeout_sec = TE_FRAG_TIMEOUT_SEC;
    char ml_model_path[TE_ML_MODEL_PATH_MAX] = {0};
    int  ml_disable = 0;
    int  rpc_disable = 0;
    uint16_t rpc_port = TE_ML_RPC_PORT;

    if (parse_app_args(argc, argv, &port_id, &show_stats,
                       &frag_table_size, &frag_timeout_sec,
                       ml_model_path, &ml_disable,
                       &rpc_disable, &rpc_port) != 0)
        return -1;

    memset(&g_te, 0, sizeof(g_te));
    g_te.port_id     = port_id;
    g_te.show_stats  = show_stats;
    g_te.tsc_hz      = rte_get_tsc_hz();
    g_te.start_tsc   = te_rdtsc();
    g_te.frag_table_size = frag_table_size;
    g_te.frag_timeout_sec = frag_timeout_sec;
    g_te.ml_enabled  = (ml_disable == 0 && ml_model_path[0] != '\0') ? 1 : 0;
    g_te.ml_rpc_enabled = (rpc_disable == 0) ? 1 : 0;
    g_te.ml_rpc_port = rpc_port;
    if (ml_model_path[0] != '\0')
        strncpy(g_te.ml_model_path, ml_model_path, TE_ML_MODEL_PATH_MAX - 1);
    rte_atomic64_init(&g_te.seq_counter);

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    /* 3. mbuf pool */
    char pool_name[64];
    snprintf(pool_name, sizeof(pool_name), "mbuf_pool");
    g_te.mbuf_pool = rte_pktmbuf_pool_create(
        pool_name, TE_MBUF_POOL_SIZE, TE_MBUF_CACHE_SIZE,
        0, TE_MBUF_SIZE, rte_socket_id());
    if (g_te.mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "mempool create failed\n");

    /* 3b. ML feature object pool (pre-allocated, no runtime malloc) */
    if (g_te.ml_enabled) {
        snprintf(pool_name, sizeof(pool_name), "feature_pool");
        g_te.feature_pool = rte_mempool_create(
            pool_name, TE_FLOW_TABLE_SIZE,
            sizeof(struct te_flow_features),
            512, 0, NULL, NULL, NULL, NULL,
            rte_socket_id(), 0);
        if (g_te.feature_pool == NULL) {
            RTE_LOG(WARNING, USER1,
                    "feature pool create failed, will use rte_malloc fallback\n");
        }
    }

    /* 4. Initialize port */
    if (port_init(port_id, g_te.mbuf_pool) != 0)
        rte_exit(EXIT_FAILURE, "port init failed\n");

    /* 4b. IP fragment reassembly table */
    if (te_frag_init() != 0)
        rte_exit(EXIT_FAILURE, "frag init failed\n");

    /* 5. Flow hash table */
    if (te_flow_table_init() != 0)
        rte_exit(EXIT_FAILURE, "flow table init failed\n");

    /* 6. Classifier (PCRE compile) */
    if (te_classifier_init() != 0)
        rte_exit(EXIT_FAILURE, "classifier init failed\n");

    /* 7. ML classifier (ONNX Random Forest) */
    if (g_te.ml_enabled) {
        if (te_ml_init(g_te.ml_model_path) != 0) {
            RTE_LOG(WARNING, USER1,
                    "ML classifier init failed - continuing without ML\n");
            g_te.ml_enabled = 0;
        }
    }

    /* 8. Training ring buffer for online learning samples */
    if (g_te.ml_enabled) {
        g_te.training_ring = rte_ring_create(
            "te_training_ring", TE_ML_TRAINING_RING_SIZE,
            rte_socket_id(),
            RTE_RING_F_SP_ENQ | RTE_RING_F_SC_DEQ);
        if (g_te.training_ring == NULL) {
            RTE_LOG(WARNING, USER1,
                    "training ring create failed, continuing\n");
        }
    }

    /* 9. RPC server for model hot update and label feedback */
    if (g_te.ml_rpc_enabled) {
        if (te_rpc_init(g_te.ml_rpc_port) != 0) {
            RTE_LOG(WARNING, USER1,
                    "RPC server init failed on port %u, continuing\n",
                    g_te.ml_rpc_port);
            g_te.ml_rpc_enabled = 0;
        }
    }

    /* 10. Result ring buffer (SP/MC ring, message size = sizeof msg) */
    g_te.result_ring = rte_ring_create(
        "te_result_ring", TE_RESULT_RING_SIZE,
        rte_socket_id(),
        RTE_RING_F_SP_ENQ | RTE_RING_F_SC_DEQ);
    if (g_te.result_ring == NULL)
        rte_exit(EXIT_FAILURE, "result ring create failed\n");

    /* 11. Stats subsystem */
    te_stats_init();

    /* 12. Assign lcores: one per RX queue + one for stats */
    uint16_t nb_rx_lcores = TE_RX_QUEUES_PER_PORT;
    if (nb_rx_lcores + 1 > (uint16_t)rte_lcore_count())
        rte_exit(EXIT_FAILURE, "not enough lcores (need >= %u)\n",
                 nb_rx_lcores + 1);

    g_te.nb_lcores = nb_rx_lcores;
    g_te.lcore_stats = (struct te_lcore_stats *)rte_zmalloc(
        "lcore_stats",
        sizeof(struct te_lcore_stats) * nb_rx_lcores,
        RTE_CACHE_LINE_SIZE);
    if (g_te.lcore_stats == NULL)
        rte_exit(EXIT_FAILURE, "lcore_stats alloc failed\n");

    /* Assign: first nb_rx_lcores slave lcores to RX, next to stats */
    uint32_t lcore_list[RTE_MAX_LCORE];
    uint32_t lc = 0;
    unsigned int lid;
    RTE_LCORE_FOREACH_WORKER(lid) {
        if (lc < nb_rx_lcores + 1)
            lcore_list[lc++] = lid;
    }
    if (lc < nb_rx_lcores + 1)
        rte_exit(EXIT_FAILURE, "need at least %u worker lcores\n",
                 nb_rx_lcores + 1);

    /* 13. Launch RX lcores */
    struct rx_lcore_arg rx_args[RTE_MAX_LCORE];
    for (uint32_t i = 0; i < nb_rx_lcores; i++) {
        rx_args[i].port_id   = port_id;
        rx_args[i].queue_id  = i;
        rx_args[i].lcore_idx = i;
        g_te.lcore_id[i]     = lcore_list[i];
        RTE_LOG(INFO, USER1, "launch lcore %u for RX q=%u\n",
                lcore_list[i], i);
        rte_eal_remote_launch(rx_lcore_main, &rx_args[i],
                              lcore_list[i]);
    }

    /* 14. Launch stats lcore (on next worker lcore) */
    uint32_t stats_lcore = lcore_list[nb_rx_lcores];
    RTE_LOG(INFO, USER1, "launch lcore %u for stats\n", stats_lcore);
    rte_eal_remote_launch(stats_lcore_main, NULL, stats_lcore);

    /* 15. Main lcore: wait for quit, drain rings, process RPC */
    RTE_LOG(INFO, USER1,
            "DPDK Traffic Engine running. Press Ctrl-C to stop.\n");
    if (g_te.ml_enabled)
        RTE_LOG(INFO, USER1,
                "  ML classifier: %s ONNX model\n",
                g_te.ml_model_path);
    if (g_te.ml_rpc_enabled)
        RTE_LOG(INFO, USER1,
                "  RPC server: port %u\n", g_te.ml_rpc_port);

    while (!g_te.force_quit) {
        /* Drain results if external service isn't connected */
        struct te_result_msg msg;
        while (rte_ring_dequeue(g_te.result_ring, &msg) == 0) {
            (void)msg;
        }

        /* Drain training samples (offline trainer can consume these) */
        if (g_te.training_ring) {
            struct te_ml_training_sample sample;
            while (rte_ring_dequeue(g_te.training_ring, &sample) == 0) {
                /* Consumed silently - write to file/SHM in production */
                (void)sample;
            }
        }

        /* Process RPC requests (model hot update, label feedback) */
        if (g_te.ml_rpc_enabled)
            te_rpc_process();

        rte_delay_us_sleep(1000);
    }

    /* 16. Wait lcores, cleanup */
    RTE_LOG(INFO, USER1, "shutting down...\n");
    rte_eal_mp_wait_lcore();

    if (show_stats)
        te_stats_dump();

    port_fini(port_id);
    te_frag_fini();
    te_classifier_fini();
    te_flow_table_fini();

    if (g_te.ml_enabled)
        te_ml_fini();
    if (g_te.ml_rpc_enabled)
        te_rpc_fini();

    rte_ring_free(g_te.result_ring);
    if (g_te.training_ring)
        rte_ring_free(g_te.training_ring);

    rte_free(g_te.lcore_stats);
    if (g_te.feature_pool)
        rte_mempool_free(g_te.feature_pool);
    rte_mempool_free(g_te.mbuf_pool);

    RTE_LOG(INFO, USER1, "bye.\n");
    return 0;
}
