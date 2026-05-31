#include "tc_shaper.h"
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>

int tc_run_cmd(const char *fmt, ...) {
    char cmd[MAX_CMD];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);

    printf("[tc] 执行: %s\n", cmd);
    int ret = system(cmd);
    if (WIFEXITED(ret)) {
        int code = WEXITSTATUS(ret);
        if (code != 0) {
            fprintf(stderr, "[tc] 命令返回非零: %d\n", code);
        }
        return code;
    }
    return -1;
}

static double parse_loss_probability(const char *loss_str) {
    if (!loss_str || loss_str[0] == '\0') return 0.0;
    double pct = 0.0;
    char suffix[16] = "";
    if (sscanf(loss_str, "%lf%15s", &pct, suffix) < 1) return 0.0;
    if (strchr(suffix, '%')) {
        return pct / 100.0;
    }
    return pct;
}

static int parse_loss_percentage(const char *loss_str, double *probability) {
    if (!loss_str || loss_str[0] == '\0') return -1;
    *probability = parse_loss_probability(loss_str);
    if (*probability <= 0.0 || *probability > 1.0) return -1;
    return 0;
}

int iptables_apply_loss(const char *ifname, const char *loss_pct,
                         char *chain_name, char *rule_spec) {
    if (!ifname || !loss_pct || loss_pct[0] == '\0') return 0;

    double prob;
    if (parse_loss_percentage(loss_pct, &prob) != 0) {
        fprintf(stderr, "[iptables] 无效丢包率: %s\n", loss_pct);
        return -1;
    }

    char chain[IPTABLES_CHAIN_MAX];
    snprintf(chain, sizeof(chain), "VSHAPER_LOSS_%s", ifname);
    if (chain_name) strncpy(chain_name, chain, IPTABLES_CHAIN_MAX - 1);

    char spec[256];
    snprintf(spec, sizeof(spec),
             "-o %s -m statistic --mode random --probability %.6f -j DROP",
             ifname, prob);
    if (rule_spec) strncpy(rule_spec, spec, sizeof(spec) - 1);

    tc_run_cmd("iptables -N %s 2>/dev/null", chain);
    tc_run_cmd("iptables -F %s 2>/dev/null", chain);

    int ret = tc_run_cmd("iptables -I OUTPUT %s", spec);
    if (ret != 0) {
        fprintf(stderr, "[iptables] 丢包规则添加失败\n");
        tc_run_cmd("iptables -X %s 2>/dev/null", chain);
        return -1;
    }

    printf("[iptables] 丢包规则已应用: 接口=%s, 概率=%.4f (%.1f%%)\n",
           ifname, prob, prob * 100.0);
    return 0;
}

int iptables_remove_loss(const char *chain_name, const char *rule_spec) {
    if (!chain_name || chain_name[0] == '\0') return 0;

    if (rule_spec && rule_spec[0] != '\0') {
        tc_run_cmd("iptables -D OUTPUT %s 2>/dev/null", rule_spec);
    }

    tc_run_cmd("iptables -F %s 2>/dev/null", chain_name);
    tc_run_cmd("iptables -X %s 2>/dev/null", chain_name);

    printf("[iptables] 丢包规则已移除\n");
    return 0;
}

int iptables_collect_stats(const char *chain_name,
                             const char *ifname,
                             unsigned long *lost_packets) {
    if (!chain_name || !lost_packets) return -1;
    *lost_packets = 0;

    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd),
             "iptables -L OUTPUT -n -v -x 2>/dev/null | "
             "grep -E 'DROP.*%s' | awk '{print $2}'",
             ifname);

    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[64];
        if (fgets(line, sizeof(line), fp)) {
            *lost_packets = strtoul(line, NULL, 10);
        }
        pclose(fp);
    }

    return 0;
}

int tc_apply_token_bucket(const char *ifname, const char *rate,
                           int burst_kbytes, int latency_ms,
                           unsigned int parent_handle) {
    if (!ifname || !rate || rate[0] == '\0') return 0;

    return tc_run_cmd(
        "tc qdisc add dev %s root handle %u: tbf rate %s "
        "burst %dkb latency %dms",
        ifname, parent_handle, rate, burst_kbytes, latency_ms);
}

int tc_apply_netem(const char *ifname, const char *delay,
                    const char *dup, const char *reorder,
                    unsigned int parent_handle) {
    if (!ifname) return -1;

    int has_delay   = (delay   && delay[0]   != '\0');
    int has_dup     = (dup     && dup[0]     != '\0');
    int has_reorder = (reorder && reorder[0] != '\0');

    if (!has_delay && !has_dup && !has_reorder) return 0;

    char params[MAX_CMD];
    params[0] = '\0';

    if (has_delay) {
        strncat(params, " delay ", sizeof(params) - strlen(params) - 1);
        strncat(params, delay, sizeof(params) - strlen(params) - 1);
    }
    if (has_reorder) {
        strncat(params, " reorder ", sizeof(params) - strlen(params) - 1);
        strncat(params, reorder, sizeof(params) - strlen(params) - 1);
    }
    if (has_dup) {
        strncat(params, " duplicate ", sizeof(params) - strlen(params) - 1);
        strncat(params, dup, sizeof(params) - strlen(params) - 1);
    }

    unsigned int netem_handle = parent_handle + 1;

    if (parent_handle == 1) {
        return tc_run_cmd(
            "tc qdisc add dev %s root handle %u: netem%s",
            ifname, netem_handle, params);
    } else {
        return tc_run_cmd(
            "tc qdisc add dev %s parent %u: handle %u: netem%s",
            ifname, parent_handle, netem_handle, params);
    }
}

int tc_shaper_init(tc_shaper_t *shaper, const char *ifname,
                    const rule_config_t *rule) {
    if (!shaper || !ifname || !rule) return -1;
    memset(shaper, 0, sizeof(*shaper));
    strncpy(shaper->ifname, ifname, MAX_IFNAME - 1);
    strncpy(shaper->rate_limit, rule->rate_limit,
            sizeof(shaper->rate_limit) - 1);
    strncpy(shaper->delay, rule->delay, sizeof(shaper->delay) - 1);
    strncpy(shaper->loss, rule->loss, sizeof(shaper->loss) - 1);
    strncpy(shaper->dup, rule->dup, sizeof(shaper->dup) - 1);
    strncpy(shaper->reorder, rule->reorder,
            sizeof(shaper->reorder) - 1);
    shaper->burst_kbytes = rule->burst_kbytes > 0 ? rule->burst_kbytes : 16;
    shaper->latency_ms = rule->latency_ms > 0 ? rule->latency_ms : 50;
    shaper->tbf_handle = 1;
    shaper->netem_handle = 10;
    return 0;
}

int tc_shaper_apply(tc_shaper_t *shaper) {
    if (!shaper) return -1;
    if (shaper->is_applied) {
        printf("[tc] 整形规则已应用，跳过\n");
        return 0;
    }

    printf("[pipeline] 管道顺序: iptables(丢包) → TC(速率限制) → TC(延迟/乱序/重复)\n");

    if (shaper->loss[0] != '\0') {
        if (iptables_apply_loss(shaper->ifname, shaper->loss,
                                shaper->iptables_chain,
                                shaper->iptables_rule_spec) != 0) {
            fprintf(stderr, "[tc] iptables 丢包规则应用失败\n");
            return -1;
        }
        shaper->iptables_rule_applied = 1;
    }

    tc_run_cmd("tc qdisc del dev %s root 2>/dev/null", shaper->ifname);

    int has_tbf = (shaper->rate_limit[0] != '\0');
    int has_netem = (shaper->delay[0] != '\0' ||
                     shaper->dup[0] != '\0' ||
                     shaper->reorder[0] != '\0');

    if (has_tbf && has_netem) {
        if (tc_apply_token_bucket(shaper->ifname, shaper->rate_limit,
                                   shaper->burst_kbytes, shaper->latency_ms,
                                   shaper->tbf_handle) != 0) {
            fprintf(stderr, "[tc] TBF 令牌桶应用失败\n");
            return -1;
        }

        if (tc_apply_netem(shaper->ifname, shaper->delay,
                           shaper->dup, shaper->reorder,
                           shaper->tbf_handle) != 0) {
            fprintf(stderr, "[tc] netem 规则应用失败\n");
            return -1;
        }
    } else if (has_tbf) {
        if (tc_apply_token_bucket(shaper->ifname, shaper->rate_limit,
                                   shaper->burst_kbytes, shaper->latency_ms,
                                   shaper->tbf_handle) != 0) {
            fprintf(stderr, "[tc] TBF 令牌桶应用失败\n");
            return -1;
        }
    } else if (has_netem) {
        if (tc_apply_netem(shaper->ifname, shaper->delay,
                           shaper->dup, shaper->reorder, 1) != 0) {
            fprintf(stderr, "[tc] netem 规则应用失败\n");
            return -1;
        }
    }

    shaper->is_applied = 1;
    printf("[tc] 流量整形规则已应用到 %s\n", shaper->ifname);
    return 0;
}

int tc_shaper_remove(tc_shaper_t *shaper) {
    if (!shaper) return -1;
    if (!shaper->is_applied && !shaper->iptables_rule_applied) return 0;

    if (shaper->iptables_rule_applied) {
        iptables_remove_loss(shaper->iptables_chain,
                             shaper->iptables_rule_spec);
        shaper->iptables_rule_applied = 0;
    }

    if (shaper->is_applied) {
        tc_run_cmd("tc qdisc del dev %s root 2>/dev/null", shaper->ifname);
        shaper->is_applied = 0;
    }

    printf("[tc] %s 上的整形规则已移除\n", shaper->ifname);
    return 0;
}

int tc_shaper_show_stats(const tc_shaper_t *shaper, stats_info_t *stats) {
    if (!shaper || !stats) return -1;

    memset(stats, 0, sizeof(*stats));

    char path[MAX_PATH];

    sprintf(path, "%s/%s/statistics/rx_packets", SYSFS_NET_PATH, shaper->ifname);
    FILE *fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->rx_packets); fclose(fp); }

    sprintf(path, "%s/%s/statistics/tx_packets", SYSFS_NET_PATH, shaper->ifname);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->tx_packets); fclose(fp); }

    sprintf(path, "%s/%s/statistics/rx_bytes", SYSFS_NET_PATH, shaper->ifname);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->rx_bytes); fclose(fp); }

    sprintf(path, "%s/%s/statistics/tx_bytes", SYSFS_NET_PATH, shaper->ifname);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->tx_bytes); fclose(fp); }

    sprintf(path, "%s/%s/statistics/rx_dropped", SYSFS_NET_PATH, shaper->ifname);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->rx_dropped); fclose(fp); }

    sprintf(path, "%s/%s/statistics/tx_dropped", SYSFS_NET_PATH, shaper->ifname);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->tx_dropped); fclose(fp); }

    sprintf(path, "%s/%s/statistics/rx_errors", SYSFS_NET_PATH, shaper->ifname);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->rx_errors); fclose(fp); }

    sprintf(path, "%s/%s/statistics/tx_errors", SYSFS_NET_PATH, shaper->ifname);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->tx_errors); fclose(fp); }

    unsigned long iptables_lost = 0;
    iptables_collect_stats(shaper->iptables_chain, shaper->ifname,
                           &iptables_lost);
    stats->lost_packets = iptables_lost;

    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd),
             "tc -s qdisc show dev %s 2>/dev/null", shaper->ifname);
    fp = popen(cmd, "r");
    if (fp) {
        char buf[4096] = "";
        while (fgets(cmd, sizeof(cmd), fp)) {
            strncat(buf, cmd, sizeof(buf) - strlen(buf) - 1);
        }
        pclose(fp);

        char *p = strstr(buf, "delay");
        if (p) sscanf(p, "delay %lfms", &stats->avg_delay_ms);

        p = strstr(buf, "dropped");
        if (p) {
            char *num_start = p;
            while (*num_start && !isdigit((unsigned char)*num_start))
                num_start++;
            if (*num_start) stats->rx_dropped += strtoul(num_start, NULL, 10);
        }

        p = strstr(buf, "requeues");
        if (p) {
            char *num_start = p;
            while (*num_start && !isdigit((unsigned char)*num_start))
                num_start++;
            if (*num_start) stats->reordered_packets = strtoul(num_start, NULL, 10);
        }
    }

    return 0;
}

void tc_shaper_destroy(tc_shaper_t *shaper) {
    if (!shaper) return;
    tc_shaper_remove(shaper);
    memset(shaper, 0, sizeof(*shaper));
}
