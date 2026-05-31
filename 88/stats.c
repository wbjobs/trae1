/* SPDX-License-Identifier: BSD-3-Clause
 * Statistics - periodic dump of packet rate and classification stats
 */
#include "te_header.h"
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>

/* Cached per-interval snapshot for rate computation */
struct te_stats_snapshot {
    uint64_t tsc;
    uint64_t rx_pkts;
    uint64_t rx_bytes;
    uint64_t cat_counts[TE_NB_CATEGORIES + 1];
    uint64_t missed;
    uint64_t enq_fail;
    /* frag counters */
    uint64_t frag_pkts;
    uint64_t frag_reassembled;
    uint64_t frag_dropped;
    uint64_t frag_timeouts;
    uint64_t frag_attack_logs;
    /* ML counters */
    uint64_t ml_inference_count;
    uint64_t ml_inference_us;
    uint64_t ml_latency_max_us;
    uint64_t ml_fusion_count;
    uint64_t ml_training_count;
    uint64_t ml_model_reloads;
};

static struct te_stats_snapshot g_prev;
static int g_stats_initialized = 0;

void te_stats_init(void)
{
    memset(&g_prev, 0, sizeof(g_prev));
    g_stats_initialized = 1;
}

/*
 * Aggregate per-lcore counters into a single summary snapshot
 */
static void te_stats_aggregate(struct te_stats_snapshot *s)
{
    memset(s, 0, sizeof(*s));
    s->tsc = te_rdtsc();

    for (uint32_t i = 0; i < g_te.nb_lcores; i++) {
        struct te_lcore_stats *ls = &g_te.lcore_stats[i];
        s->rx_pkts  += ls->rx_pkts;
        s->rx_bytes += ls->rx_bytes;
        s->missed   += ls->rx_missed;
        s->enq_fail += ls->ring_enq_fail;
        s->frag_pkts        += ls->frag_pkts;
        s->frag_reassembled  += ls->frag_reassembled;
        s->frag_dropped      += ls->frag_dropped;
        s->frag_timeouts     += ls->frag_timeouts;
        s->frag_attack_logs  += ls->frag_attack_logs;
        s->ml_inference_count += ls->ml_inference_count;
        s->ml_inference_us    += ls->ml_inference_us;
        if (ls->ml_latency_max_us > s->ml_latency_max_us)
            s->ml_latency_max_us = ls->ml_latency_max_us;
        s->ml_fusion_count   += ls->ml_fusion_count;
        s->ml_training_count += ls->ml_training_count;
        s->ml_model_reloads  += ls->ml_model_reloads;
        for (int c = 0; c < TE_NB_CATEGORIES + 1; c++)
            s->cat_counts[c] += ls->cat_counts[c];
    }
}

void te_stats_dump(void)
{
    struct te_stats_snapshot cur;
    te_stats_aggregate(&cur);

    uint64_t dt_cycles = cur.tsc - g_prev.tsc;
    double dt_sec = (g_prev.tsc == 0) ? 0.0 :
                    (double)dt_cycles / (double)g_te.tsc_hz;

    printf("\n========== DPDK Traffic Engine Stats ==========\n");
    printf(" uptime       : %.2f sec\n",
           (double)(cur.tsc - g_te.start_tsc) / (double)g_te.tsc_hz);
    printf(" flows        : %u\n", te_flow_count());
    printf(" rx_pkts      : %-20" PRIu64 "\n", cur.rx_pkts);
    printf(" rx_bytes     : %-20" PRIu64 "\n", cur.rx_bytes);
    printf(" missed       : %-20" PRIu64 "\n", cur.missed);
    printf(" ring_enq_fail: %-20" PRIu64 "\n", cur.enq_fail);

    printf(" ---- IP fragment reassembly ----\n");
    printf("   frag_pkts       : %-20" PRIu64 "\n", cur.frag_pkts);
    printf("   frag_reassembled: %-20" PRIu64 "\n", cur.frag_reassembled);
    printf("   frag_dropped    : %-20" PRIu64 "\n", cur.frag_dropped);
    printf("   frag_timeouts   : %-20" PRIu64 "\n", cur.frag_timeouts);
    printf("   frag_attacks    : %-20" PRIu64 "\n", cur.frag_attack_logs);

    if (g_te.ml_enabled) {
        printf(" ---- ML Random Forest ----\n");
        printf("   ml_inferences   : %-20" PRIu64 "\n", cur.ml_inference_count);
        double avg_lat = cur.ml_inference_count > 0 ?
            (double)cur.ml_inference_us / (double)cur.ml_inference_count : 0.0;
        printf("   ml_avg_lat_us   : %-20.2f\n", avg_lat);
        printf("   ml_max_lat_us   : %-20" PRIu64 "\n", cur.ml_latency_max_us);
        printf("   ml_fusions      : %-20" PRIu64 "\n", cur.ml_fusion_count);
        printf("   ml_train_samples: %-20" PRIu64 "\n", cur.ml_training_count);
        printf("   ml_model_reloads: %-20" PRIu64 "\n", cur.ml_model_reloads);
    }

    if (dt_sec > 0.0) {
        uint64_t dp = cur.rx_pkts  - g_prev.rx_pkts;
        uint64_t db = cur.rx_bytes - g_prev.rx_bytes;
        double pps = dp / dt_sec;
        double bps = (db * 8.0) / dt_sec;
        double mbps = bps / 1e6;
        printf(" rate         : %.2f Mpps,  %.2f Mbps (%.2f Gbps)\n",
               pps / 1e6, mbps, mbps / 1000.0);
    }

    printf(" ---- classification ----\n");
    for (int c = 0; c < TE_NB_CATEGORIES; c++) {
        double pct = 0.0;
        if (cur.rx_pkts > 0)
            pct = 100.0 * (double)cur.cat_counts[c] / (double)cur.rx_pkts;
        printf("   %-8s : %-12" PRIu64 " pkts  (%5.1f%%)\n",
               te_category_name[c], cur.cat_counts[c], pct);
    }
    printf("   %-8s : %-12" PRIu64 " pkts\n",
           te_category_name[TE_CAT_UNKNOWN],
           cur.cat_counts[TE_CAT_UNKNOWN]);

    printf("==============================================\n\n");
    fflush(stdout);

    g_prev = cur;
    if (!g_stats_initialized)
        g_stats_initialized = 1;
}

void te_stats_periodic(uint64_t now_tsc)
{
    static uint64_t last_tsc = 0;

    if (last_tsc == 0)
        last_tsc = now_tsc;

    if ((now_tsc - last_tsc) < g_te.tsc_hz)
        return;

    last_tsc = now_tsc;
    te_stats_dump();
}
