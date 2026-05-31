#ifndef VSHAPER_DRY_RUN_H
#define VSHAPER_DRY_RUN_H

#include "common.h"

typedef struct {
    unsigned long   total_packets;
    unsigned long   total_bytes;
    unsigned long   dropped;
    unsigned long   delayed;
    unsigned long   reordered;
    unsigned long   duplicated;
    unsigned long   passed;
    double          total_delay_ms;
    double          max_delay_ms;
    double          loss_rate;
    double          delay_avg_ms;
    double          dup_rate;
    double          reorder_rate;
} dry_run_stats_t;

typedef struct {
    unsigned long   seq;
    int             tun_fd;
    char            ifname[MAX_IFNAME];
    rule_config_t   rule;
    dry_run_stats_t stats;
    int             verbose;
    unsigned long   delay_us;
    double          loss_prob;
    double          dup_prob;
    double          reorder_prob;
    unsigned long   rate_bytes_per_sec;
    unsigned long   bucket_tokens;
    unsigned long   bucket_capacity;
    struct timespec last_token_refill;
} dry_run_ctx_t;

int  dry_run_init(dry_run_ctx_t *ctx, const char *ifname,
                  const rule_config_t *rule, int tun_fd);
void dry_run_process_packet(dry_run_ctx_t *ctx, const void *data,
                            size_t len, unsigned long seq);
void dry_run_print_stats(const dry_run_ctx_t *ctx);
void dry_run_destroy(dry_run_ctx_t *ctx);

#endif
