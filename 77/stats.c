#include "stats.h"

int stats_collect(const char *ifname, stats_info_t *stats) {
    if (!ifname || !stats) return -1;
    memset(stats, 0, sizeof(*stats));

    char path[MAX_PATH];

    sprintf(path, "%s/%s/statistics/rx_packets", SYSFS_NET_PATH, ifname);
    FILE *fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->rx_packets); fclose(fp); }

    sprintf(path, "%s/%s/statistics/tx_packets", SYSFS_NET_PATH, ifname);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->tx_packets); fclose(fp); }

    sprintf(path, "%s/%s/statistics/rx_bytes", SYSFS_NET_PATH, ifname);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->rx_bytes); fclose(fp); }

    sprintf(path, "%s/%s/statistics/tx_bytes", SYSFS_NET_PATH, ifname);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->tx_bytes); fclose(fp); }

    sprintf(path, "%s/%s/statistics/rx_dropped", SYSFS_NET_PATH, ifname);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->rx_dropped); fclose(fp); }

    sprintf(path, "%s/%s/statistics/tx_dropped", SYSFS_NET_PATH, ifname);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->tx_dropped); fclose(fp); }

    sprintf(path, "%s/%s/statistics/rx_errors", SYSFS_NET_PATH, ifname);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->rx_errors); fclose(fp); }

    sprintf(path, "%s/%s/statistics/tx_errors", SYSFS_NET_PATH, ifname);
    fp = fopen(path, "r");
    if (fp) { fscanf(fp, "%lu", &stats->tx_errors); fclose(fp); }

    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd),
             "tc -s qdisc show dev %s 2>/dev/null", ifname);
    fp = popen(cmd, "r");
    if (fp) {
        char buf[2048] = "";
        while (fgets(cmd, sizeof(cmd), fp)) {
            strncat(buf, cmd, sizeof(buf) - strlen(buf) - 1);
        }
        pclose(fp);

        char *p = strstr(buf, "delay");
        if (p) sscanf(p, "delay %lfms", &stats->avg_delay_ms);

        p = strstr(buf, "dropped");
        if (p) sscanf(p, "dropped %lu", &stats->rx_dropped);

        p = strstr(buf, "requeues");
        if (p) sscanf(p, "requeues %lu", &stats->reordered_packets);
    }

    return 0;
}

static void format_bytes(unsigned long bytes, char *buf, size_t len) {
    if (bytes >= 1024UL * 1024 * 1024) {
        snprintf(buf, len, "%.2f GB", bytes / (1024.0 * 1024 * 1024));
    } else if (bytes >= 1024UL * 1024) {
        snprintf(buf, len, "%.2f MB", bytes / (1024.0 * 1024));
    } else if (bytes >= 1024) {
        snprintf(buf, len, "%.2f KB", bytes / 1024.0);
    } else {
        snprintf(buf, len, "%lu B", bytes);
    }
}

void stats_print(const char *ifname, const stats_info_t *stats) {
    if (!ifname || !stats) return;

    char rx_fmt[64], tx_fmt[64];
    format_bytes(stats->rx_bytes, rx_fmt, sizeof(rx_fmt));
    format_bytes(stats->tx_bytes, tx_fmt, sizeof(tx_fmt));

    printf("\n");
    printf("┌───────────────────────────────────────────────┐\n");
    printf("│            %s 统计信息                    │\n", ifname);
    printf("├───────────────────────────────────────────────┤\n");
    printf("│ 接收包数 (RX):       %-24lu │\n", stats->rx_packets);
    printf("│ 发送包数 (TX):       %-24lu │\n", stats->tx_packets);
    printf("│ 接收字节数:          %-24s │\n", rx_fmt);
    printf("│ 发送字节数:          %-24s │\n", tx_fmt);
    printf("├───────────────────────────────────────────────┤\n");
    printf("│ 接收丢包数:          %-24lu │\n", stats->rx_dropped);
    printf("│ 发送丢包数:          %-24lu │\n", stats->tx_dropped);
    printf("│ 接收错误数:          %-24lu │\n", stats->rx_errors);
    printf("│ 发送错误数:          %-24lu │\n", stats->tx_errors);
    printf("├───────────────────────────────────────────────┤\n");
    printf("│ 平均延迟:            %-18.2f ms │\n", stats->avg_delay_ms);
    printf("│ 重排序包数:          %-24lu │\n", stats->reordered_packets);
    printf("└───────────────────────────────────────────────┘\n");
}

int stats_collect_all(const app_config_t *config, stats_info_t *stats) {
    if (!config || !stats) return -1;
    return stats_collect(config->ifname, stats);
}
