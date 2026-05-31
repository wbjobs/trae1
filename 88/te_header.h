/* SPDX-License-Identifier: BSD-3-Clause
 * DPDK Traffic Parsing Engine - Shared Header
 */
#ifndef __TE_HEADER_H__
#define __TE_HEADER_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>

#include <rte_config.h>
#include <rte_common.h>
#include <rte_errno.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_ip_frag.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_ring.h>
#include <rte_hash.h>
#include <rte_jhash.h>
#include <rte_lcore.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_atomic.h>
#include <rte_launch.h>
#include <rte_malloc.h>
#include <rte_timer.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                         */
/* ------------------------------------------------------------------ */
#define TE_MAX_PORTS             1
#define TE_RX_QUEUES_PER_PORT    1
#define TE_TX_QUEUES_PER_PORT    0
#define TE_RX_RING_SIZE          4096
#define TE_MBUF_SIZE             (2048 + RTE_PKTMBUF_HEADROOM)
#define TE_MBUF_CACHE_SIZE       512
#define TE_MBUF_POOL_SIZE        (8192 * 8)
#define TE_BURST_SIZE            64

#define TE_FLOW_TABLE_SIZE       (1U << 20)    /* 1M flows */
#define TE_RESULT_RING_SIZE      (1U << 16)    /* 64K slots */

#define TE_NB_CATEGORIES         5
#define TE_IPV4_ADDR_LEN         4
#define TE_FLOW_TIMEOUT_SEC      30

/* -------- IP fragment reassembly constants -------- */
#define TE_FRAG_TABLE_SIZE       4096       /* concurrent flows being reassembled */
#define TE_FRAG_TIMEOUT_SEC      1          /* reassembly timeout */
#define TE_FRAG_MAX_PKT_SIZE     65535
#define TE_FRAG_POOL_SIZE        (1U << 12) /* 4096 direct mbufs for reassembled pkts */
#define TE_ATTACK_LOG_RATE_LIMIT 1          /* min seconds between attack logs for same flow */

/* -------- Machine Learning classifier constants -------- */
#define TE_ML_NB_FEATURES        50
#define TE_ML_MODEL_PATH_MAX     512
#define TE_ML_INFERENCE_LATENCY_US 100      /* target max latency per flow */
#define TE_ML_RPC_PORT           9876
#define TE_ML_TRAINING_RING_SIZE (1U << 14) /* 16K training samples */

/* TLS fingerprint related */
#define TE_TLS_HELLO_MAX_LEN     512
#define TE_TLS_CIPHER_MAX        32

/* Weighted fusion weights (DPI vs ML) */
#define TE_FUSION_WEIGHT_DPI     0.4
#define TE_FUSION_WEIGHT_ML      0.6

/* Feature normalization bounds */
#define TE_PKT_LEN_MAX           1500
#define TE_IAT_MAX_US            1000000.0  /* 1 second max for normalization */

/* ------------------------------------------------------------------ */
/*  Traffic categories                                                */
/* ------------------------------------------------------------------ */
enum te_category {
    TE_CAT_WEB   = 0,
    TE_CAT_VIDEO = 1,
    TE_CAT_P2P   = 2,
    TE_CAT_GAME  = 3,
    TE_CAT_VOIP  = 4,
    TE_CAT_UNKNOWN,
};

static const char * const te_category_name[] = {
    [TE_CAT_WEB]   = "Web",
    [TE_CAT_VIDEO] = "Video",
    [TE_CAT_P2P]   = "P2P",
    [TE_CAT_GAME]  = "Game",
    [TE_CAT_VOIP]  = "VoIP",
    [TE_CAT_UNKNOWN] = "Unknown",
};

/* ------------------------------------------------------------------ */
/*  5-Tuple Key                                                       */
/* ------------------------------------------------------------------ */
struct te_5tuple {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  protocol;
    uint8_t  _pad[3];
} __rte_packed;

/* ------------------------------------------------------------------ */
/*  Flow record with ML feature collector                             */
/* ------------------------------------------------------------------ */

/* Running statistics accumulator for packet lengths */
struct te_pktlen_stats {
    uint64_t sum;
    uint64_t sum_sq;
    uint32_t min;
    uint32_t max;
    uint32_t count;
    /* histogram bins: 0-64, 65-128, 129-256, 257-512, 513-1024, 1025-1500, 1500+ */
    uint32_t histo[8];
};

/* Running statistics accumulator for inter-arrival times (us) */
struct te_iat_stats {
    uint64_t sum;
    uint64_t sum_sq;
    uint32_t min;
    uint32_t max;
    uint32_t count;
    /* histogram bins: 0-100us, 101-500us, 501-1000us, 1-10ms, 10-100ms, 100ms+ */
    uint32_t histo[6];
    uint64_t last_tsc;
};

/* TLS Client Hello fingerprint extractor */
struct te_tls_fp {
    uint8_t  has_client_hello;
    uint16_t version;
    uint16_t ciphers[TE_TLS_CIPHER_MAX];
    uint8_t  nb_ciphers;
    uint16_t extensions[16];
    uint8_t  nb_extensions;
    uint8_t  sni[256];
    uint16_t sni_len;
    uint32_t ja3_hash; /* simplified JA3 hash */
};

/* Per-flow feature collector (accumulated over packet sequence) */
struct te_flow_features {
    /* Basic */
    uint64_t start_tsc;
    uint64_t last_tsc;
    uint32_t duration_us;

    /* Downstream (src -> dst) */
    struct te_pktlen_stats ds_pktlen;
    struct te_iat_stats    ds_iat;
    uint64_t ds_bytes;
    uint32_t ds_pkts;

    /* Upstream (dst -> src) */
    struct te_pktlen_stats us_pktlen;
    struct te_iat_stats    us_iat;
    uint64_t us_bytes;
    uint32_t us_pkts;

    /* Protocol flags */
    uint8_t  has_syn;
    uint8_t  has_fin;
    uint8_t  has_rst;
    uint8_t  has_ack;
    uint8_t  has_psh;
    uint8_t  has_urg;
    uint8_t  tcp_window_size_avg;
    uint16_t tcp_flags_count;

    /* TLS fingerprint */
    struct te_tls_fp tls_fp;

    /* Payload stats */
    uint32_t payload_bytes_total;
    uint16_t payload_len_avg;
    uint8_t  has_http;
    uint8_t  has_dns;
    uint8_t  has_ssl;

    /* Extracted 50-dim feature vector (normalized float) */
    float feature_vec[TE_ML_NB_FEATURES];
    bool  feature_vec_ready;
} __rte_cache_aligned;

struct te_flow {
    struct te_5tuple key;
    uint64_t pkts;
    uint64_t bytes;
    uint64_t first_tsc;   /* tsc of first packet */
    uint64_t last_tsc;    /* tsc of last packet  */
    uint32_t category;    /* enum te_category, after fusion */
    uint32_t flags;       /* reserved */

    /* DPI and ML intermediate results for fusion */
    uint8_t  dpi_category;
    float    dpi_confidence;
    uint8_t  ml_category;
    float    ml_confidence;
    float    ml_proba[TE_NB_CATEGORIES];

    /* ML feature collector */
    struct te_flow_features *features;

    /* True label (for online training, provided via RPC) */
    uint8_t  true_label;
    bool     has_true_label;
};

/* ------------------------------------------------------------------ */
/*  ML inference result and training sample                           */
/* ------------------------------------------------------------------ */

/* Training sample for online continuous learning (sent via ring) */
struct te_ml_training_sample {
    struct te_5tuple key;
    float feature_vec[TE_ML_NB_FEATURES];
    uint8_t true_label;
    uint8_t pred_label_dpi;
    uint8_t pred_label_ml;
    uint64_t timestamp;
} __rte_packed;

/* ------------------------------------------------------------------ */
/*  RPC command/response structures for model hot update              */
/* ------------------------------------------------------------------ */

enum te_rpc_cmd {
    TE_RPC_CMD_LOAD_MODEL   = 1,
    TE_RPC_CMD_GET_STATS    = 2,
    TE_RPC_CMD_SET_LABEL    = 3,
    TE_RPC_CMD_PING         = 4,
};

struct te_rpc_request {
    uint32_t cmd;
    uint32_t seq;
    union {
        char model_path[TE_ML_MODEL_PATH_MAX];
        struct {
            struct te_5tuple key;
            uint8_t true_label;
        } label;
    } data;
} __rte_packed;

struct te_rpc_response {
    uint32_t cmd;
    uint32_t seq;
    int32_t  status;
    char message[256];
} __rte_packed;

/* ------------------------------------------------------------------ */
/*  ML classifier context (opaque forward decl)                       */
/* ------------------------------------------------------------------ */
struct te_ml_ctx;
extern struct te_ml_ctx *g_ml_ctx;

/* ------------------------------------------------------------------ */
/*  Global statistics (per lcore)                                     */
/* ------------------------------------------------------------------ */
struct te_lcore_stats {
    uint64_t rx_pkts;
    uint64_t rx_bytes;
    uint64_t rx_missed;
    uint64_t parse_err;
    uint64_t cat_counts[TE_NB_CATEGORIES + 1];
    uint64_t ring_enq_ok;
    uint64_t ring_enq_fail;
    uint64_t new_flows;
    uint64_t update_flows;
    uint64_t expired_flows;
    /* IP fragment reassembly counters */
    uint64_t frag_pkts;          /* total fragment packets received */
    uint64_t frag_reassembled;   /* packets successfully reassembled */
    uint64_t frag_dropped;       /* fragments dropped (queue full, etc.) */
    uint64_t frag_timeouts;      /* flows expired before full reassembly */
    uint64_t frag_attack_logs;   /* potential teardrop attack events */
    /* ML inference counters */
    uint64_t ml_inference_count; /* total ML inference calls */
    uint64_t ml_inference_us;    /* total inference latency (us) */
    uint64_t ml_latency_max_us;  /* max inference latency observed */
    uint64_t ml_fusion_count;    /* times DPI+ML fusion applied */
    uint64_t ml_training_count;  /* training samples enqueued */
    uint64_t ml_model_reloads;   /* number of hot model reloads */
} __rte_cache_aligned;

/* ------------------------------------------------------------------ */
/*  Result message sent via ring buffer to external service           */
/* ------------------------------------------------------------------ */
struct te_result_msg {
    uint64_t seq;
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  protocol;
    uint8_t  category;
    uint64_t pkts;
    uint64_t bytes;
    uint64_t first_tsc;
    uint64_t last_tsc;
    /* ML augmented fields */
    uint8_t  dpi_category;
    uint8_t  ml_category;
    float    ml_confidence;
} __rte_packed;

/* ------------------------------------------------------------------ */
/*  Global configuration / context                                    */
/* ------------------------------------------------------------------ */
struct te_config {
    uint16_t port_id;
    uint16_t nb_lcores;
    uint32_t lcore_id[RTE_MAX_LCORE];
    struct rte_mempool *mbuf_pool;
    struct rte_mempool *frag_pool;          /* direct mbuf pool for reassembly */
    struct rte_mempool *feature_pool;       /* prealloc flow features */
    struct rte_ip_frag_tbl *frag_tbl;       /* IP fragment reassembly table */
    struct rte_hash     *flow_ht;            /* 5tuple -> te_flow */
    struct rte_ring     *result_ring;
    struct rte_ring     *training_ring;      /* training samples for ML */
    volatile int         force_quit;
    volatile int         show_stats;         /* trigger by --stats */
    uint64_t             start_tsc;
    uint64_t             tsc_hz;
    struct te_lcore_stats *lcore_stats;      /* array indexed by lcore_idx */
    rte_atomic64_t       seq_counter;
    uint32_t             frag_table_size;    /* configurable, default TE_FRAG_TABLE_SIZE */
    uint32_t             frag_timeout_sec;   /* configurable, default TE_FRAG_TIMEOUT_SEC */
    /* ML config */
    char                 ml_model_path[TE_ML_MODEL_PATH_MAX];
    int                  ml_enabled;
    int                  ml_rpc_enabled;
    uint16_t             ml_rpc_port;
};

extern struct te_config g_te;

/* ------------------------------------------------------------------ */
/*  Common helpers                                                    */
/* ------------------------------------------------------------------ */
static inline uint64_t te_rdtsc(void)
{
    return rte_rdtsc();
}

static inline uint64_t te_tsc_to_us(uint64_t tsc, uint64_t tsc_hz)
{
    return (tsc * 1000000ULL) / tsc_hz;
}

/* ------------------------------------------------------------------ */
/*  Module APIs                                                       */
/* ------------------------------------------------------------------ */

/* parser.c */
int  te_parse_packet(struct rte_mbuf *mbuf, struct te_5tuple *key,
                     uint16_t *l4_len, uint8_t **l4_payload,
                     uint16_t *payload_len);

/* classifier.c */
int  te_classifier_init(void);
void te_classifier_fini(void);
enum te_category te_classify(const struct te_5tuple *key,
                              const uint8_t *l4_payload,
                              uint16_t payload_len);

/* flow_table.c */
int  te_flow_table_init(void);
void te_flow_table_fini(void);
struct te_flow * te_flow_get_or_create(const struct te_5tuple *key,
                                        uint32_t pkt_len, uint64_t tsc,
                                        bool *is_new);
void te_flow_expire(uint64_t now_tsc, uint64_t timeout_cycles);
uint32_t te_flow_count(void);

/* stats.c */
void te_stats_init(void);
void te_stats_dump(void);
void te_stats_periodic(uint64_t now_tsc);

/* main.c */
void te_result_enqueue(const struct te_5tuple *key, enum te_category cat,
                       uint32_t pkt_len, uint64_t tsc);
void te_result_enqueue_flow_aggregate(const struct te_flow *flow);

/* frag.c (implemented in main.c for simplicity) */
int  te_frag_init(void);
void te_frag_fini(void);
int  te_frag_reassemble(struct rte_mbuf *m, uint32_t lcore_idx,
                         struct rte_mbuf **out_mbuf);
void te_frag_drain_death_row(uint64_t now_tsc);

/* attack log */
struct te_frag_attack_log {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t frag_id;      /* IP identification field */
    uint8_t  protocol;
    uint64_t tsc;
};
void te_frag_log_attack(const struct te_frag_attack_log *log);

/* -------- Machine Learning Classifier API -------- */

/* ml_classifier.c */
int  te_ml_init(const char *model_path);
void te_ml_fini(void);
int  te_ml_load_model(const char *model_path, char *err_msg, size_t err_len);

/* Feature engineering: accumulate per-packet data into flow features */
void te_ml_feature_accumulate(struct te_flow_features *feat,
                               const struct rte_mbuf *mbuf,
                               const struct te_5tuple *key,
                               const uint8_t *l4_payload,
                               uint16_t payload_len,
                               uint64_t tsc);

/* Extract 50-dim normalized feature vector from accumulated stats */
void te_ml_feature_extract(const struct te_flow_features *feat,
                           const struct te_5tuple *key,
                           float *feature_vec);

/* Run ONNX Random Forest inference on feature vector */
int  te_ml_inference(const float *feature_vec,
                     uint8_t *out_category,
                     float *out_confidence,
                     float *out_proba,
                     uint64_t *latency_us);

/* Weighted voting fusion of DPI and ML results */
enum te_category te_ml_fuse(uint8_t dpi_cat, float dpi_conf,
                             uint8_t ml_cat, float ml_conf,
                             const float *ml_proba);

/* Enqueue training sample to ring buffer for online learning */
int  te_ml_enqueue_training_sample(const struct te_flow *flow);

/* Set true label for a flow (called from RPC) */
int  te_ml_set_true_label(const struct te_5tuple *key, uint8_t true_label);

/* -------- RPC Server API -------- */

/* rpc_server.c */
int  te_rpc_init(uint16_t port);
void te_rpc_fini(void);
void te_rpc_process(void);

#endif /* __TE_HEADER_H__ */
