/* SPDX-License-Identifier: BSD-3-Clause
 * ML Classifier - Random Forest via ONNX Runtime
 *
 * 50 Feature vector layout:
 * [0-6]    Downstream packet length histogram (8 bins)
 * [7-13]   Upstream packet length histogram (8 bins)
 * [14-19]  Downstream IAT histogram (6 bins)
 * [20-25]  Upstream IAT histogram (6 bins)
 * [26]     Downstream packet count (normalized)
 * [27]     Upstream packet count (normalized)
 * [28]     Downstream total bytes (normalized)
 * [29]     Upstream total bytes (normalized)
 * [30]     Downstream avg packet length
 * [31]     Upstream avg packet length
 * [32]     Downstream packet length std dev
 * [33]     Upstream packet length std dev
 * [34]     Downstream avg IAT (us)
 * [35]     Upstream avg IAT (us)
 * [36]     Downstream IAT std dev
 * [37]     Upstream IAT std dev
 * [38]     Up/Down packet ratio
 * [39]     Up/Down byte ratio
 * [40]     Flow duration (us, normalized)
 * [41]     Protocol (TCP=1, UDP=0)
 * [42]     Has SYN flag
 * [43]     Has FIN flag
 * [44]     Has RST flag
 * [45]     Has PSH flag
 * [46]     Has ACK flag
 * [47]     TLS Client Hello present
 * [48]     HTTP header present
 * [49]     Payload ratio (payload / total bytes)
 */
#include "te_header.h"
#include <math.h>
#include <pthread.h>
#include <onnxruntime_c_api.h>

/* ------------------------------------------------------------------ */
/*  ONNX Runtime context                                              */
/* ------------------------------------------------------------------ */
struct te_ml_ctx {
    const OrtApi *ort_api;
    OrtEnv     *ort_env;
    OrtSession *ort_session;
    OrtAllocator *ort_allocator;
    OrtMemoryInfo *ort_meminfo;
    char     *input_name;
    char     *output_name;
    size_t    input_size;   /* TE_ML_NB_FEATURES */
    size_t    output_size;  /* TE_NB_CATEGORIES */
    int       nb_trees;     /* random forest trees */
    pthread_rwlock_t model_lock; /* for hot swap */
};

struct te_ml_ctx *g_ml_ctx = NULL;

/* ------------------------------------------------------------------ */
/*  Helper: running stats                                             */
/* ------------------------------------------------------------------ */
static inline float stats_mean(uint64_t sum, uint32_t count)
{
    return count > 0 ? (float)sum / (float)count : 0.0f;
}

static inline float stats_stddev(uint64_t sum, uint64_t sum_sq, uint32_t count)
{
    if (count < 2) return 0.0f;
    float mean = (float)sum / (float)count;
    float var  = ((float)sum_sq / (float)count) - mean * mean;
    return var > 0.0f ? sqrtf(var) : 0.0f;
}

static inline float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/* ------------------------------------------------------------------ */
/*  TLS Client Hello parser & JA3 fingerprint                         */
/* ------------------------------------------------------------------ */
static void parse_tls_client_hello(const uint8_t *payload,
                                   uint16_t payload_len,
                                   struct te_tls_fp *fp)
{
    if (payload_len < 43)
        return;

    /* TLS Record Layer:
     *   0x16 = Handshake
     *   0x0301 = TLS 1.0, 0x0302 = TLS 1.1, 0x0303 = TLS 1.2+ */
    if (payload[0] != 0x16)
        return;
    if (payload[1] != 0x03)
        return;

    /* Handshake header:
     *   0x01 = Client Hello */
    if (payload[5] != 0x01)
        return;

    fp->has_client_hello = 1;
    fp->version = (payload[1] << 8) | payload[2];

    /* Parse Client Hello (simplified) */
    uint32_t offset = 43; /* skip to session ID */
    if (offset >= payload_len) return;

    uint8_t session_id_len = payload[offset++];
    offset += session_id_len;
    if (offset + 2 >= payload_len) return;

    uint16_t cipher_len = (payload[offset] << 8) | payload[offset + 1];
    offset += 2;
    if (offset + cipher_len > payload_len) return;

    fp->nb_ciphers = 0;
    for (uint16_t i = 0; i < cipher_len && fp->nb_ciphers < TE_TLS_CIPHER_MAX; i += 2) {
        fp->ciphers[fp->nb_ciphers++] =
            (payload[offset + i] << 8) | payload[offset + i + 1];
    }
    offset += cipher_len;
    if (offset >= payload_len) return;

    uint8_t comp_len = payload[offset++];
    offset += comp_len;
    if (offset + 2 >= payload_len) return;

    uint16_t ext_len = (payload[offset] << 8) | payload[offset + 1];
    offset += 2;
    if (offset + ext_len > payload_len) return;

    fp->nb_extensions = 0;
    uint32_t ext_end = offset + ext_len;
    while (offset + 4 < ext_end && fp->nb_extensions < 16) {
        uint16_t ext_type = (payload[offset] << 8) | payload[offset + 1];
        uint16_t ext_sz   = (payload[offset + 2] << 8) | payload[offset + 3];
        fp->extensions[fp->nb_extensions++] = ext_type;
        offset += 4;

        /* SNI extension (type 0) */
        if (ext_type == 0 && offset + ext_sz <= ext_end && ext_sz >= 5) {
            uint16_t list_len = (payload[offset] << 8) | payload[offset + 1];
            (void)list_len;
            if (ext_sz >= 7) {
                uint8_t name_type = payload[offset + 2];
                if (name_type == 0) {
                    uint16_t name_len = (payload[offset + 3] << 8) | payload[offset + 4];
                    if (name_len < 256 && offset + 5 + name_len <= ext_end) {
                        memcpy(fp->sni, &payload[offset + 5], name_len);
                        fp->sni_len = name_len;
                    }
                }
            }
        }
        offset += ext_sz;
    }

    /* Simplified JA3 hash: XOR all relevant fields */
    uint32_t ja3 = 0;
    for (uint8_t i = 0; i < fp->nb_ciphers; i++)
        ja3 ^= fp->ciphers[i];
    for (uint8_t i = 0; i < fp->nb_extensions; i++)
        ja3 ^= fp->extensions[i];
    ja3 ^= fp->version;
    fp->ja3_hash = ja3;
}

/* ------------------------------------------------------------------ */
/*  Feature accumulation (per packet)                                 */
/* ------------------------------------------------------------------ */
static void pktlen_histogram_add(struct te_pktlen_stats *s, uint32_t len)
{
    uint32_t bin;
    if (len <= 64)        bin = 0;
    else if (len <= 128)  bin = 1;
    else if (len <= 256)  bin = 2;
    else if (len <= 512)  bin = 3;
    else if (len <= 1024) bin = 4;
    else if (len <= 1500) bin = 5;
    else                  bin = 6;
    s->histo[bin]++;
    s->sum    += len;
    s->sum_sq += (uint64_t)len * len;
    if (len < s->min || s->count == 0) s->min = len;
    if (len > s->max) s->max = len;
    s->count++;
}

static void iat_histogram_add(struct te_iat_stats *s, uint64_t tsc,
                              uint64_t tsc_hz)
{
    if (s->last_tsc == 0) {
        s->last_tsc = tsc;
        return;
    }
    uint64_t delta_cycles = tsc - s->last_tsc;
    uint32_t delta_us = (uint32_t)((delta_cycles * 1000000ULL) / tsc_hz);
    s->last_tsc = tsc;

    uint32_t bin;
    if (delta_us <= 100)        bin = 0;
    else if (delta_us <= 500)   bin = 1;
    else if (delta_us <= 1000)  bin = 2;
    else if (delta_us <= 10000) bin = 3;
    else if (delta_us <= 100000) bin = 4;
    else                        bin = 5;
    s->histo[bin]++;
    s->sum    += delta_us;
    s->sum_sq += (uint64_t)delta_us * delta_us;
    if (delta_us < s->min || s->count == 0) s->min = delta_us;
    if (delta_us > s->max) s->max = delta_us;
    s->count++;
}

void te_ml_feature_accumulate(struct te_flow_features *feat,
                               const struct rte_mbuf *mbuf,
                               const struct te_5tuple *key,
                               const uint8_t *l4_payload,
                               uint16_t payload_len,
                               uint64_t tsc)
{
    if (feat->start_tsc == 0) {
        feat->start_tsc = tsc;
        feat->ds_iat.last_tsc = 0;
        feat->us_iat.last_tsc = 0;
    }
    feat->last_tsc = tsc;

    /* Direction: src->dst = downstream, dst->src = upstream */
    bool downstream = true;

    if (key->protocol == IPPROTO_TCP) {
        struct rte_tcp_hdr *tcp = (struct rte_tcp_hdr *)
            ((uint8_t *)l4_payload - sizeof(struct rte_tcp_hdr));
        if (tcp) {
            if (tcp->tcp_flags & RTE_TCP_SYN_FLAG) feat->has_syn = 1;
            if (tcp->tcp_flags & RTE_TCP_FIN_FLAG) feat->has_fin = 1;
            if (tcp->tcp_flags & RTE_TCP_RST_FLAG) feat->has_rst = 1;
            if (tcp->tcp_flags & RTE_TCP_ACK_FLAG) feat->has_ack = 1;
            if (tcp->tcp_flags & RTE_TCP_PSH_FLAG) feat->has_psh = 1;
            if (tcp->tcp_flags & RTE_TCP_URG_FLAG) feat->has_urg = 1;
            feat->tcp_window_size_avg =
                (feat->tcp_window_size_avg + rte_be_to_cpu_16(tcp->rx_win)) / 2;
            feat->tcp_flags_count++;
        }

        if (payload_len > 0) {
            /* Check for HTTP request/response */
            if (payload_len >= 4 &&
                (memcmp(l4_payload, "GET ", 4) == 0 ||
                 memcmp(l4_payload, "POST", 4) == 0 ||
                 memcmp(l4_payload, "HTTP", 4) == 0)) {
                feat->has_http = 1;
            }
            /* Check for TLS Client Hello */
            if (!feat->tls_fp.has_client_hello) {
                parse_tls_client_hello(l4_payload, payload_len, &feat->tls_fp);
            }
        }
    } else if (key->protocol == IPPROTO_UDP) {
        if (payload_len >= 12) {
            /* Check for DNS (port 53) */
            if ((key->src_port == 53 || key->dst_port == 53) &&
                payload_len >= 12) {
                feat->has_dns = 1;
            }
        }
    }

    /* SSL/TLS detection (port 443 or handshake present) */
    if (key->src_port == 443 || key->dst_port == 443 ||
        feat->tls_fp.has_client_hello) {
        feat->has_ssl = 1;
    }

    /* Accumulate stats by direction */
    uint32_t pkt_len = mbuf->pkt_len;
    if (downstream) {
        pktlen_histogram_add(&feat->ds_pktlen, pkt_len);
        iat_histogram_add(&feat->ds_iat, tsc, g_te.tsc_hz);
        feat->ds_bytes += pkt_len;
        feat->ds_pkts++;
    } else {
        pktlen_histogram_add(&feat->us_pktlen, pkt_len);
        iat_histogram_add(&feat->us_iat, tsc, g_te.tsc_hz);
        feat->us_bytes += pkt_len;
        feat->us_pkts++;
    }

    /* Payload stats */
    feat->payload_bytes_total += payload_len;
    if (feat->ds_pkts + feat->us_pkts > 0) {
        feat->payload_len_avg = (uint16_t)(
            feat->payload_bytes_total / (feat->ds_pkts + feat->us_pkts));
    }
}

/* ------------------------------------------------------------------ */
/*  Extract 50-dim normalized feature vector                          */
/* ------------------------------------------------------------------ */
void te_ml_feature_extract(const struct te_flow_features *feat,
                           const struct te_5tuple *key,
                           float *fv)
{
    memset(fv, 0, sizeof(float) * TE_ML_NB_FEATURES);

    uint32_t total_pkts = feat->ds_pkts + feat->us_pkts;
    uint64_t total_bytes = feat->ds_bytes + feat->us_bytes;

    /* [0-6] Downstream pkt len histogram (8 bins) */
    for (int i = 0; i < 7; i++)
        fv[i] = total_pkts > 0 ? (float)feat->ds_pktlen.histo[i] /
                               (float)total_pkts : 0.0f;

    /* [7-13] Upstream pkt len histogram (8 bins) */
    for (int i = 0; i < 7; i++)
        fv[7 + i] = total_pkts > 0 ? (float)feat->us_pktlen.histo[i] /
                                    (float)total_pkts : 0.0f;

    /* [14-19] Downstream IAT histogram (6 bins) */
    uint32_t ds_iat_total = feat->ds_iat.count > 0 ? feat->ds_iat.count : 1;
    for (int i = 0; i < 6; i++)
        fv[14 + i] = (float)feat->ds_iat.histo[i] / (float)ds_iat_total;

    /* [20-25] Upstream IAT histogram (6 bins) */
    uint32_t us_iat_total = feat->us_iat.count > 0 ? feat->us_iat.count : 1;
    for (int i = 0; i < 6; i++)
        fv[20 + i] = (float)feat->us_iat.histo[i] / (float)us_iat_total;

    /* [26-27] Normalized packet counts */
    fv[26] = clamp01((float)feat->ds_pkts / 1000.0f);
    fv[27] = clamp01((float)feat->us_pkts / 1000.0f);

    /* [28-29] Normalized bytes */
    fv[28] = clamp01((float)feat->ds_bytes / 1000000.0f);
    fv[29] = clamp01((float)feat->us_bytes / 1000000.0f);

    /* [30-31] Avg packet length (normalized) */
    fv[30] = clamp01(
        stats_mean(feat->ds_pktlen.sum, feat->ds_pktlen.count) / TE_PKT_LEN_MAX);
    fv[31] = clamp01(
        stats_mean(feat->us_pktlen.sum, feat->us_pktlen.count) / TE_PKT_LEN_MAX);

    /* [32-33] Std dev packet length (normalized) */
    fv[32] = clamp01(
        stats_stddev(feat->ds_pktlen.sum, feat->ds_pktlen.sum_sq,
                      feat->ds_pktlen.count) / TE_PKT_LEN_MAX);
    fv[33] = clamp01(
        stats_stddev(feat->us_pktlen.sum, feat->us_pktlen.sum_sq,
                      feat->us_pktlen.count) / TE_PKT_LEN_MAX);

    /* [34-35] Avg IAT (normalized) */
    fv[34] = clamp01(
        stats_mean(feat->ds_iat.sum, feat->ds_iat.count) / TE_IAT_MAX_US);
    fv[35] = clamp01(
        stats_mean(feat->us_iat.sum, feat->us_iat.count) / TE_IAT_MAX_US);

    /* [36-37] Std dev IAT (normalized) */
    fv[36] = clamp01(
        stats_stddev(feat->ds_iat.sum, feat->ds_iat.sum_sq,
                      feat->ds_iat.count) / TE_IAT_MAX_US);
    fv[37] = clamp01(
        stats_stddev(feat->us_iat.sum, feat->us_iat.sum_sq,
                      feat->us_iat.count) / TE_IAT_MAX_US);

    /* [38-39] Up/Down ratios */
    if (feat->ds_pkts > 0)
        fv[38] = clamp01((float)feat->us_pkts / (float)feat->ds_pkts);
    if (feat->ds_bytes > 0)
        fv[39] = clamp01((float)feat->us_bytes / (float)feat->ds_bytes);

    /* [40] Flow duration (normalized to 100s max) */
    uint64_t dur_cycles = feat->last_tsc - feat->start_tsc;
    uint32_t dur_us = (uint32_t)((dur_cycles * 1000000ULL) / g_te.tsc_hz);
    fv[40] = clamp01((float)dur_us / 100000000.0f);

    /* [41] Protocol */
    fv[41] = (key->protocol == IPPROTO_TCP) ? 1.0f : 0.0f;

    /* [42-46] TCP flags */
    fv[42] = feat->has_syn ? 1.0f : 0.0f;
    fv[43] = feat->has_fin ? 1.0f : 0.0f;
    fv[44] = feat->has_rst ? 1.0f : 0.0f;
    fv[45] = feat->has_psh ? 1.0f : 0.0f;
    fv[46] = feat->has_ack ? 1.0f : 0.0f;

    /* [47] TLS present */
    fv[47] = feat->tls_fp.has_client_hello ? 1.0f : 0.0f;

    /* [48] HTTP present */
    fv[48] = feat->has_http ? 1.0f : 0.0f;

    /* [49] Payload ratio */
    if (total_bytes > 0)
        fv[49] = clamp01((float)feat->payload_bytes_total / (float)total_bytes);

    /* Mark feature vector ready */
    ((struct te_flow_features *)feat)->feature_vec_ready = true;
}

/* ------------------------------------------------------------------ */
/*  ONNX Runtime model loading                                        */
/* ------------------------------------------------------------------ */
int te_ml_load_model(const char *model_path, char *err_msg, size_t err_len)
{
    if (g_ml_ctx == NULL || model_path == NULL) {
        if (err_msg) snprintf(err_msg, err_len, "null context or path");
        return -1;
    }

    pthread_rwlock_wrlock(&g_ml_ctx->model_lock);

    /* Free old session if exists */
    if (g_ml_ctx->ort_session) {
        g_ml_ctx->ort_api->ReleaseSession(g_ml_ctx->ort_session);
        g_ml_ctx->ort_session = NULL;
    }
    if (g_ml_ctx->input_name) {
        g_ml_ctx->ort_api->AllocatorFree(g_ml_ctx->ort_allocator,
                                          g_ml_ctx->input_name);
        g_ml_ctx->input_name = NULL;
    }
    if (g_ml_ctx->output_name) {
        g_ml_ctx->ort_api->AllocatorFree(g_ml_ctx->ort_allocator,
                                          g_ml_ctx->output_name);
        g_ml_ctx->output_name = NULL;
    }

    /* Create new session */
    OrtSessionOptions *opts = g_ml_ctx->ort_api->CreateSessionOptions();
    if (!opts) {
        pthread_rwlock_unlock(&g_ml_ctx->model_lock);
        if (err_msg) snprintf(err_msg, err_len, "CreateSessionOptions failed");
        return -1;
    }

    /* Optimize for latency (single thread, graph opt level 1) */
    g_ml_ctx->ort_api->SetIntraOpNumThreads(opts, 1);
    g_ml_ctx->ort_api->SetSessionGraphOptimizationLevel(
        opts, ORT_ENABLE_BASIC);

    /* Load model */
    OrtStatus *status = g_ml_ctx->ort_api->CreateSession(
        g_ml_ctx->ort_env, model_path, opts, &g_ml_ctx->ort_session);
    g_ml_ctx->ort_api->ReleaseSessionOptions(opts);

    if (status) {
        const char *msg = g_ml_ctx->ort_api->GetErrorMessage(status);
        if (err_msg) snprintf(err_msg, err_len, "CreateSession: %s", msg);
        g_ml_ctx->ort_api->ReleaseStatus(status);
        pthread_rwlock_unlock(&g_ml_ctx->model_lock);
        return -1;
    }

    /* Query input/output names */
    OrtTensorTypeAndShapeInfo *in_info = NULL;
    size_t in_idx = 0;
    g_ml_ctx->ort_api->SessionGetInputName(
        g_ml_ctx->ort_session, in_idx, g_ml_ctx->ort_allocator,
        &g_ml_ctx->input_name);
    g_ml_ctx->ort_api->SessionGetInputTypeInfo(
        g_ml_ctx->ort_session, in_idx, &in_info);

    int64_t *in_dims;
    size_t in_ndim;
    g_ml_ctx->ort_api->GetDimensions(in_info, NULL, &in_ndim);
    in_dims = alloca(sizeof(int64_t) * in_ndim);
    g_ml_ctx->ort_api->GetDimensions(in_info, in_dims, &in_ndim);
    g_ml_ctx->input_size = (size_t)in_dims[in_ndim - 1];
    g_ml_ctx->ort_api->ReleaseTensorTypeAndShapeInfo(in_info);

    size_t out_idx = 0;
    g_ml_ctx->ort_api->SessionGetOutputName(
        g_ml_ctx->ort_session, out_idx, g_ml_ctx->ort_allocator,
        &g_ml_ctx->output_name);

    OrtTensorTypeAndShapeInfo *out_info = NULL;
    g_ml_ctx->ort_api->SessionGetOutputTypeInfo(
        g_ml_ctx->ort_session, out_idx, &out_info);
    int64_t out_dims[4];
    size_t out_ndim;
    g_ml_ctx->ort_api->GetDimensions(out_info, NULL, &out_ndim);
    g_ml_ctx->ort_api->GetDimensions(out_info, out_dims, &out_ndim);
    g_ml_ctx->output_size = (size_t)out_dims[out_ndim - 1];
    g_ml_ctx->ort_api->ReleaseTensorTypeAndShapeInfo(out_info);

    if (g_te.lcore_stats)
        g_te.lcore_stats[0].ml_model_reloads++;

    pthread_rwlock_unlock(&g_ml_ctx->model_lock);

    if (err_msg)
        snprintf(err_msg, err_len, "OK: in=%zu out=%zu",
                 g_ml_ctx->input_size, g_ml_ctx->output_size);

    RTE_LOG(INFO, USER1,
            "ML model loaded: %s (input=%zu, output=%zu)\n",
            model_path, g_ml_ctx->input_size, g_ml_ctx->output_size);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Init / Fini                                                       */
/* ------------------------------------------------------------------ */
int te_ml_init(const char *model_path)
{
    g_ml_ctx = (struct te_ml_ctx *)rte_zmalloc(
        "ml_ctx", sizeof(*g_ml_ctx), RTE_CACHE_LINE_SIZE);
    if (!g_ml_ctx) return -1;

    pthread_rwlock_init(&g_ml_ctx->model_lock, NULL);

    g_ml_ctx->ort_api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!g_ml_ctx->ort_api) {
        rte_free(g_ml_ctx);
        g_ml_ctx = NULL;
        return -1;
    }

    g_ml_ctx->ort_api->CreateEnv(ORT_LOGGING_LEVEL_WARNING,
                                 "dpdk-traffic-engine",
                                 &g_ml_ctx->ort_env);

    g_ml_ctx->ort_api->CreateCpuMemoryInfo(
        OrtDeviceAllocator, OrtMemTypeDefault,
        &g_ml_ctx->ort_meminfo);

    g_ml_ctx->ort_api->GetAllocatorWithDefaultOptions(
        &g_ml_ctx->ort_allocator);

    /* Load the model */
    if (model_path && strlen(model_path) > 0) {
        char err[256];
        if (te_ml_load_model(model_path, err, sizeof(err)) != 0) {
            RTE_LOG(WARNING, USER1,
                    "ML model load failed: %s (ML disabled)\n", err);
        }
    }

    g_ml_ctx->input_size  = TE_ML_NB_FEATURES;
    g_ml_ctx->output_size = TE_NB_CATEGORIES;

    return 0;
}

void te_ml_fini(void)
{
    if (!g_ml_ctx) return;

    pthread_rwlock_wrlock(&g_ml_ctx->model_lock);
    if (g_ml_ctx->ort_session)
        g_ml_ctx->ort_api->ReleaseSession(g_ml_ctx->ort_session);
    if (g_ml_ctx->input_name)
        g_ml_ctx->ort_api->AllocatorFree(g_ml_ctx->ort_allocator,
                                          g_ml_ctx->input_name);
    if (g_ml_ctx->output_name)
        g_ml_ctx->ort_api->AllocatorFree(g_ml_ctx->ort_allocator,
                                          g_ml_ctx->output_name);
    if (g_ml_ctx->ort_meminfo)
        g_ml_ctx->ort_api->ReleaseMemoryInfo(g_ml_ctx->ort_meminfo);
    if (g_ml_ctx->ort_env)
        g_ml_ctx->ort_api->ReleaseEnv(g_ml_ctx->ort_env);
    pthread_rwlock_unlock(&g_ml_ctx->model_lock);

    pthread_rwlock_destroy(&g_ml_ctx->model_lock);
    rte_free(g_ml_ctx);
    g_ml_ctx = NULL;
}

/* ------------------------------------------------------------------ */
/*  Inference                                                         */
/* ------------------------------------------------------------------ */
int te_ml_inference(const float *feature_vec,
                     uint8_t *out_category,
                     float *out_confidence,
                     float *out_proba,
                     uint64_t *latency_us)
{
    if (!g_ml_ctx || !g_ml_ctx->ort_session || !feature_vec)
        return -1;

    uint64_t t0 = te_rdtsc();

    pthread_rwlock_rdlock(&g_ml_ctx->model_lock);

    if (g_ml_ctx->ort_session == NULL) {
        pthread_rwlock_unlock(&g_ml_ctx->model_lock);
        return -1;
    }

    /* Setup input tensor */
    int64_t input_shape[] = {1, (int64_t)g_ml_ctx->input_size};
    OrtValue *input_tensor = NULL;
    OrtStatus *status = g_ml_ctx->ort_api->CreateTensorWithDataAsOrtValue(
        g_ml_ctx->ort_meminfo,
        (void *)feature_vec,
        sizeof(float) * g_ml_ctx->input_size,
        input_shape,
        2,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &input_tensor);

    if (status) {
        g_ml_ctx->ort_api->ReleaseStatus(status);
        pthread_rwlock_unlock(&g_ml_ctx->model_lock);
        return -1;
    }

    const char *input_names[]  = { g_ml_ctx->input_name };
    const char *output_names[] = { g_ml_ctx->output_name };
    OrtValue *output_tensor = NULL;

    status = g_ml_ctx->ort_api->Run(
        g_ml_ctx->ort_session,
        NULL,
        input_names,
        (const OrtValue *const *)&input_tensor,
        1,
        output_names,
        1,
        &output_tensor);

    g_ml_ctx->ort_api->ReleaseOrtValue(input_tensor);

    if (status) {
        g_ml_ctx->ort_api->ReleaseStatus(status);
        pthread_rwlock_unlock(&g_ml_ctx->model_lock);
        return -1;
    }

    /* Get output probabilities */
    float *out_data = NULL;
    status = g_ml_ctx->ort_api->GetTensorMutableData(output_tensor,
                                                     (void **)&out_data);
    if (status) {
        g_ml_ctx->ort_api->ReleaseStatus(status);
        g_ml_ctx->ort_api->ReleaseOrtValue(output_tensor);
        pthread_rwlock_unlock(&g_ml_ctx->model_lock);
        return -1;
    }

    /* Find argmax and confidence */
    uint8_t best_class = 0;
    float best_prob = out_data[0];
    for (size_t i = 1; i < g_ml_ctx->output_size; i++) {
        if (out_data[i] > best_prob) {
            best_prob = out_data[i];
            best_class = (uint8_t)i;
        }
    }

    if (out_proba)
        memcpy(out_proba, out_data, sizeof(float) * g_ml_ctx->output_size);
    if (out_category)
        *out_category = best_class;
    if (out_confidence)
        *out_confidence = best_prob;

    g_ml_ctx->ort_api->ReleaseOrtValue(output_tensor);
    pthread_rwlock_unlock(&g_ml_ctx->model_lock);

    /* Compute latency */
    uint64_t t1 = te_rdtsc();
    uint64_t lat = (uint64_t)((t1 - t0) * 1000000ULL / g_te.tsc_hz);
    if (latency_us) *latency_us = lat;

    /* Update stats */
    if (g_te.lcore_stats) {
        g_te.lcore_stats[0].ml_inference_count++;
        g_te.lcore_stats[0].ml_inference_us += lat;
        if (lat > g_te.lcore_stats[0].ml_latency_max_us)
            g_te.lcore_stats[0].ml_latency_max_us = lat;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Weighted voting fusion                                            */
/* ------------------------------------------------------------------ */
enum te_category te_ml_fuse(uint8_t dpi_cat, float dpi_conf,
                             uint8_t ml_cat, float ml_conf,
                             const float *ml_proba)
{
    if (g_te.lcore_stats)
        g_te.lcore_stats[0].ml_fusion_count++;

    /* If ML model not loaded, trust DPI */
    if (ml_cat >= TE_NB_CATEGORIES || ml_conf < 0.01f) {
        return (enum te_category)dpi_cat;
    }

    /* If DPI confidence is low, trust ML */
    if (dpi_cat == TE_CAT_UNKNOWN || dpi_conf < 0.3f) {
        return (enum te_category)ml_cat;
    }

    /* Weighted voting on probabilities */
    float fused_proba[TE_NB_CATEGORIES];
    float dpi_proba[TE_NB_CATEGORIES];

    /* Build DPI one-hot probability vector with confidence */
    memset(dpi_proba, 0, sizeof(dpi_proba));
    if (dpi_cat < TE_NB_CATEGORIES) {
        dpi_proba[dpi_cat] = dpi_conf * TE_FUSION_WEIGHT_DPI;
        for (int i = 0; i < TE_NB_CATEGORIES; i++)
            if (i != dpi_cat)
                dpi_proba[i] = (1.0f - dpi_conf) / (TE_NB_CATEGORIES - 1) *
                               TE_FUSION_WEIGHT_DPI;
    }

    /* Fuse with ML probabilities */
    for (int i = 0; i < TE_NB_CATEGORIES; i++) {
        fused_proba[i] =
            dpi_proba[i] +
            (ml_proba ? ml_proba[i] * TE_FUSION_WEIGHT_ML :
             (i == ml_cat ? ml_conf * TE_FUSION_WEIGHT_ML :
              (1.0f - ml_conf) / (TE_NB_CATEGORIES - 1) * TE_FUSION_WEIGHT_ML));
    }

    /* Pick argmax */
    uint8_t best = 0;
    float best_p = fused_proba[0];
    for (int i = 1; i < TE_NB_CATEGORIES; i++) {
        if (fused_proba[i] > best_p) {
            best_p = fused_proba[i];
            best = (uint8_t)i;
        }
    }

    return (enum te_category)best;
}

/* ------------------------------------------------------------------ */
/*  Enqueue training sample for online learning                        */
/* ------------------------------------------------------------------ */
int te_ml_enqueue_training_sample(const struct te_flow *flow)
{
    if (!g_te.training_ring || !flow || !flow->features ||
        !flow->has_true_label || !flow->features->feature_vec_ready)
        return -1;

    struct te_ml_training_sample sample;
    memset(&sample, 0, sizeof(sample));
    sample.key = flow->key;
    memcpy(sample.feature_vec, flow->features->feature_vec,
           sizeof(float) * TE_ML_NB_FEATURES);
    sample.true_label     = flow->true_label;
    sample.pred_label_dpi = flow->dpi_category;
    sample.pred_label_ml  = flow->ml_category;
    sample.timestamp      = te_rdtsc();

    if (rte_ring_enqueue(g_te.training_ring, &sample) == 0) {
        if (g_te.lcore_stats)
            g_te.lcore_stats[0].ml_training_count++;
        return 0;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  Set true label for a flow (called from RPC)                       */
/* ------------------------------------------------------------------ */
int te_ml_set_true_label(const struct te_5tuple *key, uint8_t true_label)
{
    if (!g_te.flow_ht || !key || true_label >= TE_NB_CATEGORIES + 1)
        return -1;

    int ret = -1;
    rte_spinlock_t *lock = NULL;
    /* Note: we need access to g_ht_lock from flow_table.c.
     * For production, export a helper function from flow_table.c.
     * Here we use a forward declaration for simplicity. */
    extern rte_spinlock_t g_ht_lock;
    rte_spinlock_lock(&g_ht_lock);

    struct te_flow *flow = NULL;
    int rc = rte_hash_lookup_data(g_te.flow_ht, key, (void **)&flow);
    if (rc >= 0 && flow != NULL) {
        flow->true_label    = true_label;
        flow->has_true_label = true;
        ret = 0;
    }

    rte_spinlock_unlock(&g_ht_lock);
    return ret;
}
