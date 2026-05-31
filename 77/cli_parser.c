#include "cli_parser.h"
#include <getopt.h>

void cli_print_help(const char *prog) {
    printf("vshaper - 虚拟网卡流量整形工具 v%s\n\n", VSHAPER_VERSION);
    printf("用法: %s [选项]\n\n", prog);
    printf("基础选项:\n");
    printf("  -h, --help              显示帮助信息\n");
    printf("  -v, --version           显示版本信息\n");
    printf("  -i, --ifname NAME       虚拟网卡名称 (默认: tun0)\n");
    printf("  --ip ADDR               IP地址 (默认: 10.0.0.1)\n");
    printf("  --netmask MASK          子网掩码 (默认: 255.255.255.0)\n");
    printf("\n流量整形规则:\n");
    printf("  --rate-limit RATE       带宽限制 (如: 1mbit, 500kbit)\n");
    printf("  --delay TIME            延迟 (如: 100ms, 1s)\n");
    printf("  --loss PCT              丢包率 (如: 5%%)\n");
    printf("  --dup PCT               重复包概率 (如: 1%%)\n");
    printf("  --reorder PCT           乱序概率 (如: 2%%)\n");
    printf("  --burst KB              令牌桶突发大小KB (默认: 16)\n");
    printf("  --latency MS            TBF延迟ms (默认: 50)\n");
    printf("\n配置与模式:\n");
    printf("  -c, --config FILE       加载配置文件\n");
    printf("  -s, --show-stats        显示统计信息\n");
    printf("  --show-history          显示规则历史\n");
    printf("  --rollback N            回滚N步规则\n");
    printf("  -d, --daemon            后台运行\n");
    printf("  --dry-run               模拟模式: 打印每个包的处理决策\n");
    printf("\n调度与过渡:\n");
    printf("  --scheduler             启用Cron定时调度\n");
    printf("  --transition-ms MS      规则切换过渡时间ms (默认: 5000)\n");
    printf("\nHTTP API:\n");
    printf("  --http-port PORT        HTTP API端口 (默认: 8080)\n");
    printf("  --no-http               禁用HTTP API服务\n");
    printf("\nPCAP回放:\n");
    printf("  --replay-pcap FILE      从pcap文件回放流量\n");
    printf("  --pcap-loop N           回放循环次数 (默认: 1)\n");
    printf("  --pcap-speed FACTOR     回放速度倍数 (默认: 1)\n");
    printf("\n日志:\n");
    printf("  --syslog                启用syslog日志记录\n");
    printf("  --no-syslog             禁用syslog日志\n");
    printf("\n示例:\n");
    printf("  %s --rate-limit 1mbit --delay 100ms --loss 5%%\n", prog);
    printf("  %s --replay-pcap traffic.pcap --dry-run\n", prog);
    printf("  %s --http-port 9090 --syslog --scheduler\n", prog);
    printf("  %s --rollback 1\n", prog);
    printf("  curl -X POST http://localhost:8080/api/update \\\n");
    printf("       -H 'Content-Type: application/json' \\\n");
    printf("       -d '{\"delay_add\":\"50ms\"}'\n");
}

int cli_parse_args(int argc, char *argv[], cli_options_t *opts,
                    app_config_t *config) {
    if (!opts || !config) return -1;

    memset(opts, 0, sizeof(*opts));
    strncpy(opts->ifname, "tun0", MAX_IFNAME - 1);
    strncpy(opts->ip, "10.0.0.1", sizeof(opts->ip) - 1);
    strncpy(opts->netmask, "255.255.255.0", sizeof(opts->netmask) - 1);
    opts->burst_kbytes = 16;
    opts->latency_ms = 50;
    opts->http_port = DEFAULT_HTTP_PORT;
    opts->http_enabled = 1;
    opts->transition_ms = DEFAULT_TRANSITION_MS;
    opts->pcap_loop_count = 1;
    opts->pcap_speed_factor = 1;
    opts->syslog_enabled = 0;
    opts->history_enabled = 1;
    opts->scheduler_enabled = 0;
    opts->rollback_steps = 0;

    static struct option long_opts[] = {
        {"help",          no_argument,       0, 'h'},
        {"version",       no_argument,       0, 'v'},
        {"ifname",        required_argument, 0, 'i'},
        {"ip",            required_argument, 0,  0 },
        {"netmask",       required_argument, 0,  0 },
        {"rate-limit",    required_argument, 0,  0 },
        {"delay",         required_argument, 0,  0 },
        {"loss",          required_argument, 0,  0 },
        {"dup",           required_argument, 0,  0 },
        {"reorder",       required_argument, 0,  0 },
        {"burst",         required_argument, 0,  0 },
        {"latency",       required_argument, 0,  0 },
        {"config",        required_argument, 0, 'c'},
        {"show-stats",    no_argument,       0, 's'},
        {"show-history",  no_argument,       0,  0 },
        {"rollback",      required_argument, 0,  0 },
        {"daemon",        no_argument,       0, 'd'},
        {"dry-run",       no_argument,       0,  0 },
        {"scheduler",     no_argument,       0,  0 },
        {"transition-ms", required_argument, 0,  0 },
        {"http-port",     required_argument, 0,  0 },
        {"no-http",       no_argument,       0,  0 },
        {"replay-pcap",   required_argument, 0,  0 },
        {"pcap-loop",     required_argument, 0,  0 },
        {"pcap-speed",    required_argument, 0,  0 },
        {"syslog",        no_argument,       0,  0 },
        {"no-syslog",     no_argument,       0,  0 },
        {0, 0, 0, 0}
    };

    int opt, idx;
    while ((opt = getopt_long(argc, argv, "hvi:c:sd", long_opts, &idx)) != -1) {
        switch (opt) {
        case 'h':
            opts->show_help = 1;
            return 0;
        case 'v':
            opts->show_version = 1;
            return 0;
        case 'i':
            strncpy(opts->ifname, optarg, MAX_IFNAME - 1);
            break;
        case 'c':
            strncpy(opts->config_file, optarg, MAX_PATH - 1);
            break;
        case 's':
            opts->show_stats = 1;
            break;
        case 'd':
            opts->daemon_mode = 1;
            break;
        case 0:
            if (strcmp(long_opts[idx].name, "ip") == 0) {
                strncpy(opts->ip, optarg, sizeof(opts->ip) - 1);
            } else if (strcmp(long_opts[idx].name, "netmask") == 0) {
                strncpy(opts->netmask, optarg, sizeof(opts->netmask) - 1);
            } else if (strcmp(long_opts[idx].name, "rate-limit") == 0) {
                strncpy(opts->rate_limit, optarg,
                        sizeof(opts->rate_limit) - 1);
            } else if (strcmp(long_opts[idx].name, "delay") == 0) {
                strncpy(opts->delay, optarg,
                        sizeof(opts->delay) - 1);
            } else if (strcmp(long_opts[idx].name, "loss") == 0) {
                strncpy(opts->loss, optarg, sizeof(opts->loss) - 1);
            } else if (strcmp(long_opts[idx].name, "dup") == 0) {
                strncpy(opts->dup, optarg, sizeof(opts->dup) - 1);
            } else if (strcmp(long_opts[idx].name, "reorder") == 0) {
                strncpy(opts->reorder, optarg,
                        sizeof(opts->reorder) - 1);
            } else if (strcmp(long_opts[idx].name, "burst") == 0) {
                opts->burst_kbytes = atoi(optarg);
            } else if (strcmp(long_opts[idx].name, "latency") == 0) {
                opts->latency_ms = atoi(optarg);
            } else if (strcmp(long_opts[idx].name, "show-history") == 0) {
                opts->show_history = 1;
            } else if (strcmp(long_opts[idx].name, "rollback") == 0) {
                opts->rollback_steps = atoi(optarg);
            } else if (strcmp(long_opts[idx].name, "dry-run") == 0) {
                opts->dry_run_mode = 1;
            } else if (strcmp(long_opts[idx].name, "scheduler") == 0) {
                opts->scheduler_enabled = 1;
            } else if (strcmp(long_opts[idx].name, "transition-ms") == 0) {
                opts->transition_ms = atoi(optarg);
            } else if (strcmp(long_opts[idx].name, "http-port") == 0) {
                opts->http_port = atoi(optarg);
                opts->http_enabled = 1;
            } else if (strcmp(long_opts[idx].name, "no-http") == 0) {
                opts->http_enabled = 0;
            } else if (strcmp(long_opts[idx].name, "replay-pcap") == 0) {
                strncpy(opts->pcap_file, optarg, MAX_PATH - 1);
                opts->pcap_replay_mode = 1;
            } else if (strcmp(long_opts[idx].name, "pcap-loop") == 0) {
                opts->pcap_loop_count = atoi(optarg);
            } else if (strcmp(long_opts[idx].name, "pcap-speed") == 0) {
                opts->pcap_speed_factor = atoi(optarg);
            } else if (strcmp(long_opts[idx].name, "syslog") == 0) {
                opts->syslog_enabled = 1;
            } else if (strcmp(long_opts[idx].name, "no-syslog") == 0) {
                opts->syslog_enabled = 0;
            }
            break;
        default:
            return -1;
        }
    }

    memset(config, 0, sizeof(*config));
    strncpy(config->ifname, opts->ifname, MAX_IFNAME - 1);
    strncpy(config->ip, opts->ip, sizeof(config->ip) - 1);
    strncpy(config->netmask, opts->netmask, sizeof(config->netmask) - 1);
    config->daemon_mode = opts->daemon_mode;
    config->dry_run_mode = opts->dry_run_mode;
    config->http_port = opts->http_port;
    config->http_enabled = opts->http_enabled;
    config->transition_ms = opts->transition_ms;
    config->pcap_replay_mode = opts->pcap_replay_mode;
    config->scheduler_enabled = opts->scheduler_enabled;
    config->history_enabled = opts->history_enabled;
    config->syslog_enabled = opts->syslog_enabled;
    if (opts->config_file[0] != '\0') {
        strncpy(config->config_file, opts->config_file, MAX_PATH - 1);
    }
    if (opts->pcap_file[0] != '\0') {
        strncpy(config->pcap_file, opts->pcap_file, MAX_PATH - 1);
    }

    rule_config_t rule;
    memset(&rule, 0, sizeof(rule));
    strncpy(rule.name, "default", sizeof(rule.name) - 1);
    strncpy(rule.rate_limit, opts->rate_limit,
            sizeof(rule.rate_limit) - 1);
    strncpy(rule.delay, opts->delay, sizeof(rule.delay) - 1);
    strncpy(rule.loss, opts->loss, sizeof(rule.loss) - 1);
    strncpy(rule.dup, opts->dup, sizeof(rule.dup) - 1);
    strncpy(rule.reorder, opts->reorder, sizeof(rule.reorder) - 1);
    rule.burst_kbytes = opts->burst_kbytes;
    rule.latency_ms = opts->latency_ms;

    if (rule.rate_limit[0] || rule.delay[0] || rule.loss[0] ||
        rule.dup[0] || rule.reorder[0]) {
        memcpy(&config->rules[0], &rule, sizeof(rule));
        config->num_rules = 1;
    }

    return 0;
}
