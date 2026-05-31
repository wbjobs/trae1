#include "dry_run.h"
#include <time.h>
#include <arpa/inet.h>

static unsigned long xorshift64(unsigned long *state) {
    unsigned long x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static double random_prob(unsigned long *state) {
    return (double)xorshift64(state) / (double)0xFFFFFFFFFFFFFFFFUL;
}

static unsigned long parse_rate_to_bps(const char *rate_str) {
    if (!rate_str || rate_str[0] == '\0') return 0;
    double val;
    char unit[16] = "";
    if (sscanf(rate_str, "%lf%15s", &val, unit) < 1) return 0;

    if (strcmp(unit, "mbit") == 0 || strcmp(unit, "Mbit") == 0)
        return (unsigned long)(val * 1024 * 1024 / 8);
    if (strcmp(unit, "kbit") == 0 || strcmp(unit, "Kbit") == 0)
        return (unsigned long)(val * 1024 / 8);
    if (strcmp(unit, "mbytes") == 0 || strcmp(unit, "MB") == 0)
        return (unsigned long)(val * 1024 * 1024);
    if (strcmp(unit, "kbytes") == 0 || strcmp(unit, "KB") == 0)
        return (unsigned long)(val * 1024);
    return (unsigned long)val;
}

static unsigned long parse_delay_to_us(const char *delay_str) {
    if (!delay_str || delay_str[0] == '\0') return 0;
    double val;
    char unit[16] = "";
    if (sscanf(delay_str, "%lf%15s", &val, unit) < 1) return 0;

    if (strcmp(unit, "ms") == 0) return (unsigned long)(val * 1000);
    if (strcmp(unit, "s") == 0 || strcmp(unit, "sec") == 0)
        return (unsigned long)(val * 1000000);
    if (strcmp(unit, "us") == 0) return (unsigned long)val;
    return (unsigned long)(val * 1000);
}

static double parse_pct_to_prob(const char *pct_str) {
    if (!pct_str || pct_str[0] == '\0') return 0.0;
    double val;
    char suffix[16] = "";
    if (sscanf(pct_str, "%lf%15s", &val, suffix) < 1) return 0.0;
    if (strchr(suffix, '%')) return val / 100.0;
    return val;
}

static void token_bucket_init(dry_run_ctx_t *ctx, unsigned long rate_bps,
                               int burst_kbytes) {
    ctx->rate_bytes_per_sec = rate_bps;
    ctx->bucket_capacity = (unsigned long)burst_kbytes * 1024;
    ctx->bucket_tokens = ctx->bucket_capacity;
    clock_gettime(CLOCK_MONOTONIC, &ctx->last_token_refill);
}

static void token_bucket_refill(dry_run_ctx_t *ctx) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    double elapsed = (now.tv_sec - ctx->last_token_refill.tv_sec) +
                     (now.tv_nsec - ctx->last_token_refill.tv_nsec) / 1e9;
    if (elapsed > 0) {
        unsigned long add = (unsigned long)(elapsed * ctx->rate_bytes_per_sec);
        ctx->bucket_tokens += add;
        if (ctx->bucket_tokens > ctx->bucket_capacity)
            ctx->bucket_tokens = ctx->bucket_capacity;
        ctx->last_token_refill = now;
    }
}

static int token_bucket_check(dry_run_ctx_t *ctx, size_t packet_len) {
    if (ctx->rate_bytes_per_sec == 0) return 1;
    token_bucket_refill(ctx);
    if (ctx->bucket_tokens >= packet_len) {
        ctx->bucket_tokens -= packet_len;
        return 1;
    }
    return 0;
}

int dry_run_init(dry_run_ctx_t *ctx, const char *ifname,
                  const rule_config_t *rule, int tun_fd) {
    if (!ctx || !ifname || !rule) return -1;

    memset(ctx, 0, sizeof(*ctx));
    strncpy(ctx->ifname, ifname, MAX_IFNAME - 1);
    memcpy(&ctx->rule, rule, sizeof(*rule));
    ctx->tun_fd = tun_fd;
    ctx->verbose = 1;
    ctx->seq = 0;

    ctx->loss_prob = parse_pct_to_prob(rule->loss);
    ctx->dup_prob = parse_pct_to_prob(rule->dup);
    ctx->reorder_prob = parse_pct_to_prob(rule->reorder);
    ctx->delay_us = parse_delay_to_us(rule->delay);
    unsigned long rate_bps = parse_rate_to_bps(rule->rate_limit);

    if (rate_bps > 0) {
        int burst = rule->burst_kbytes > 0 ? rule->burst_kbytes : 16;
        token_bucket_init(ctx, rate_bps, burst);
    }

    memset(&ctx->stats, 0, sizeof(ctx->stats));
    ctx->stats.max_delay_ms = 0.0;

    printf("[dry-run] 管道模拟已初始化\n");
    printf("[dry-run] 管道顺序: 丢包(%.2f%%) → 速率限制 → 延迟(%luμs) → 乱序(%.2f%%) → 重复(%.2f%%)\n",
           ctx->loss_prob * 100.0, ctx->delay_us,
           ctx->reorder_prob * 100.0, ctx->dup_prob * 100.0);
    if (rate_bps > 0) {
        printf("[dry-run] 速率限制: %s, 突发: %dkb\n",
               rule->rate_limit,
               rule->burst_kbytes > 0 ? rule->burst_kbytes : 16);
    }

    return 0;
}

static void print_ip_header(const unsigned char *data, size_t len) {
    if (len < 20) return;
    if (data[0] >> 4 != 4) return;

    unsigned int src_ip = (data[12] << 24) | (data[13] << 16) |
                          (data[14] << 8) | data[15];
    unsigned int dst_ip = (data[16] << 24) | (data[17] << 16) |
                          (data[18] << 8) | data[19];
    unsigned char proto = data[9];
    unsigned short total_len = (data[2] << 8) | data[3];

    char src_str[16], dst_str[16];
    snprintf(src_str, sizeof(src_str), "%u.%u.%u.%u",
             (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
             (src_ip >> 8) & 0xFF, src_ip & 0xFF);
    snprintf(dst_str, sizeof(dst_str), "%u.%u.%u.%u",
             (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
             (dst_ip >> 8) & 0xFF, dst_ip & 0xFF);

    const char *proto_str = "OTHER";
    switch (proto) {
    case 1:  proto_str = "ICMP"; break;
    case 6:  proto_str = "TCP";  break;
    case 17: proto_str = "UDP";  break;
    }

    printf(" [IPv4 %s %s→%s len=%u]", proto_str, src_str, dst_str, total_len);
}

void dry_run_process_packet(dry_run_ctx_t *ctx, const void *data,
                            size_t len, unsigned long seq) {
    if (!ctx || !data || len == 0) return;

    ctx->stats.total_packets++;
    ctx->stats.total_bytes += len;

    static unsigned long rng_state = 0x123456789ABCDEF0UL;
    rng_state += seq;

    char actions[512];
    actions[0] = '\0';
    int dropped = 0;
    int delayed = 0;
    int reordered = 0;
    int duplicated = 0;

    printf("[dry-run] #%-6lu %zu bytes", seq, len);
    print_ip_header((const unsigned char *)data, len);
    printf("\n");

    /* Stage 1: Random Loss */
    if (ctx->loss_prob > 0.0) {
        double r = random_prob(&rng_state);
        if (r < ctx->loss_prob) {
            printf("  ├─ [STAGE 1: LOSS]    ✗ DROP  (random=%.4f < threshold=%.4f)\n",
                   r, ctx->loss_prob);
            strncat(actions, "DROP ", sizeof(actions) - strlen(actions) - 1);
            ctx->stats.dropped++;
            dropped = 1;
            printf("  └─ 结果: DROPPED (后续阶段跳过)\n");
            return;
        } else {
            printf("  ├─ [STAGE 1: LOSS]    ✓ PASS  (random=%.4f >= threshold=%.4f)\n",
                   r, ctx->loss_prob);
        }
    } else {
        printf("  ├─ [STAGE 1: LOSS]    ○ SKIP (未配置)\n");
    }

    /* Stage 2: Rate Limit (Token Bucket) */
    if (ctx->rate_bytes_per_sec > 0) {
        token_bucket_refill(ctx);
        if (ctx->bucket_tokens >= len) {
            printf("  ├─ [STAGE 2: RATE]    ✓ PASS  (tokens=%lu need=%zu)\n",
                   ctx->bucket_tokens, len);
            ctx->bucket_tokens -= len;
        } else {
            printf("  ├─ [STAGE 2: RATE]    ✗ QUEUE (tokens=%lu need=%zu) - 会在TBF队列等待\n",
                   ctx->bucket_tokens, len);
            strncat(actions, "QUEUE ", sizeof(actions) - strlen(actions) - 1);
        }
    } else {
        printf("  ├─ [STAGE 2: RATE]    ○ SKIP (未配置)\n");
    }

    /* Stage 3: Delay */
    if (ctx->delay_us > 0) {
        double delay_ms = ctx->delay_us / 1000.0;
        printf("  ├─ [STAGE 3: DELAY]   ◎ DELAY  (%lu μs = %.2f ms)\n",
               ctx->delay_us, delay_ms);
        strncat(actions, "DELAY(", sizeof(actions) - strlen(actions) - 1);
        char dbuf[64];
        snprintf(dbuf, sizeof(dbuf), "%.0fms) ", delay_ms);
        strncat(actions, dbuf, sizeof(actions) - strlen(actions) - 1);
        ctx->stats.delayed++;
        ctx->stats.total_delay_ms += delay_ms;
        if (delay_ms > ctx->stats.max_delay_ms)
            ctx->stats.max_delay_ms = delay_ms;
        delayed = 1;
    } else {
        printf("  ├─ [STAGE 3: DELAY]   ○ SKIP (未配置)\n");
    }

    /* Stage 4: Reorder */
    if (ctx->reorder_prob > 0.0) {
        double r = random_prob(&rng_state);
        if (r < ctx->reorder_prob) {
            printf("  ├─ [STAGE 4: REORDER] ⇆ REORDER (random=%.4f < threshold=%.4f)\n",
                   r, ctx->reorder_prob);
            strncat(actions, "REORDER ", sizeof(actions) - strlen(actions) - 1);
            ctx->stats.reordered++;
            reordered = 1;
        } else {
            printf("  ├─ [STAGE 4: REORDER] ✓ PASS  (random=%.4f >= threshold=%.4f)\n",
                   r, ctx->reorder_prob);
        }
    } else {
        printf("  ├─ [STAGE 4: REORDER] ○ SKIP (未配置)\n");
    }

    /* Stage 5: Duplicate */
    if (ctx->dup_prob > 0.0) {
        double r = random_prob(&rng_state);
        if (r < ctx->dup_prob) {
            printf("  ├─ [STAGE 5: DUP]     ⧉ DUPLICATE (random=%.4f < threshold=%.4f)\n",
                   r, ctx->dup_prob);
            strncat(actions, "DUPLICATE ", sizeof(actions) - strlen(actions) - 1);
            ctx->stats.duplicated++;
            duplicated = 1;
        } else {
            printf("  ├─ [STAGE 5: DUP]     ✓ PASS  (random=%.4f >= threshold=%.4f)\n",
                   r, ctx->dup_prob);
        }
    } else {
        printf("  ├─ [STAGE 5: DUP]     ○ SKIP (未配置)\n");
    }

    if (!dropped)
        ctx->stats.passed++;

    printf("  └─ 结果: ");
    if (dropped) {
        printf("DROPPED");
    } else {
        printf("FORWARD");
        if (delayed) printf(" + DELAY");
        if (reordered) printf(" + REORDER");
        if (duplicated) printf(" + DUPLICATE");
    }
    printf("\n\n");
}

void dry_run_print_stats(const dry_run_ctx_t *ctx) {
    if (!ctx) return;

    unsigned long total = ctx->stats.total_packets;
    if (total > 0) {
        ctx->stats.loss_rate = (double)ctx->stats.dropped / total * 100.0;
        ctx->stats.delay_avg_ms = ctx->stats.total_delay_ms / total;
        ctx->stats.dup_rate = (double)ctx->stats.duplicated / total * 100.0;
        ctx->stats.reorder_rate = (double)ctx->stats.reordered / total * 100.0;
    }

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║            DRY-RUN 模拟统计                           ║\n");
    printf("╠═══════════════════════════════════════════════════════╣\n");
    printf("║ 总包数:              %-31lu ║\n", total);
    printf("║ 总字节数:            %-31lu ║\n", ctx->stats.total_bytes);
    printf("╠═══════════════════════════════════════════════════════╣\n");
    printf("║ 丢包数:              %-31lu ║\n", ctx->stats.dropped);
    printf("║ 实际丢包率:          %-18.2f %%              ║\n",
           ctx->stats.loss_rate);
    printf("║ 配置丢包率:          %-18.2f %%              ║\n",
           ctx->loss_prob * 100.0);
    printf("╠═══════════════════════════════════════════════════════╣\n");
    printf("║ 延迟包数:            %-31lu ║\n", ctx->stats.delayed);
    printf("║ 平均延迟:            %-18.2f ms              ║\n",
           ctx->stats.delay_avg_ms);
    printf("║ 最大延迟:            %-18.2f ms              ║\n",
           ctx->stats.max_delay_ms);
    printf("║ 配置延迟:            %-18.2f ms              ║\n",
           ctx->delay_us / 1000.0);
    printf("╠═══════════════════════════════════════════════════════╣\n");
    printf("║ 乱序包数:            %-31lu ║\n", ctx->stats.reordered);
    printf("║ 实际乱序率:          %-18.2f %%              ║\n",
           ctx->stats.reorder_rate);
    printf("║ 配置乱序率:          %-18.2f %%              ║\n",
           ctx->reorder_prob * 100.0);
    printf("╠═══════════════════════════════════════════════════════╣\n");
    printf("║ 重复包数:            %-31lu ║\n", ctx->stats.duplicated);
    printf("║ 实际重复率:          %-18.2f %%              ║\n",
           ctx->stats.dup_rate);
    printf("║ 配置重复率:          %-18.2f %%              ║\n",
           ctx->dup_prob * 100.0);
    printf("╠═══════════════════════════════════════════════════════╣\n");
    printf("║ 转发包数:            %-31lu ║\n", ctx->stats.passed);
    printf("╚═══════════════════════════════════════════════════════╝\n");
}

void dry_run_destroy(dry_run_ctx_t *ctx) {
    if (!ctx) return;
    dry_run_print_stats(ctx);
    memset(ctx, 0, sizeof(*ctx));
}
