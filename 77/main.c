#include "common.h"
#include "tun_dev.h"
#include "tc_shaper.h"
#include "cli_parser.h"
#include "config_file.h"
#include "stats.h"
#include "dry_run.h"
#include "cron_scheduler.h"
#include "http_api.h"
#include "rule_history.h"
#include "pcap_replay.h"

volatile sig_atomic_t g_running = 1;
app_config_t g_config;
rule_history_t g_rule_history;
rule_config_t g_current_rule;
pthread_mutex_t g_rule_lock = PTHREAD_MUTEX_INITIALIZER;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM || sig == SIGHUP) {
        g_running = 0;
        printf("\n[main] 收到信号 %d，正在退出...\n", sig);
    }
}

int parse_rate_value(const char *str, unsigned long *bytes_per_sec) {
    if (!str || !bytes_per_sec) return -1;
    double val;
    char unit[16] = "";
    if (sscanf(str, "%lf%15s", &val, unit) < 1) return -1;

    if (strcmp(unit, "mbit") == 0 || strcmp(unit, "Mbit") == 0) {
        *bytes_per_sec = (unsigned long)(val * 1024 * 1024 / 8);
    } else if (strcmp(unit, "kbit") == 0 || strcmp(unit, "Kbit") == 0) {
        *bytes_per_sec = (unsigned long)(val * 1024 / 8);
    } else if (strcmp(unit, "mbytes") == 0 || strcmp(unit, "MB") == 0 ||
               strcmp(unit, "MB/s") == 0) {
        *bytes_per_sec = (unsigned long)(val * 1024 * 1024);
    } else if (strcmp(unit, "kbytes") == 0 || strcmp(unit, "KB") == 0 ||
               strcmp(unit, "KB/s") == 0) {
        *bytes_per_sec = (unsigned long)(val * 1024);
    } else if (strcmp(unit, "bps") == 0 || unit[0] == '\0') {
        *bytes_per_sec = (unsigned long)(val);
    } else {
        return -1;
    }
    return 0;
}

int parse_delay_value(const char *str, unsigned long *microseconds) {
    if (!str || !microseconds) return -1;
    double val;
    char unit[16] = "";
    if (sscanf(str, "%lf%15s", &val, unit) < 1) return -1;

    if (strcmp(unit, "ms") == 0) {
        *microseconds = (unsigned long)(val * 1000);
    } else if (strcmp(unit, "s") == 0 || strcmp(unit, "sec") == 0) {
        *microseconds = (unsigned long)(val * 1000000);
    } else if (strcmp(unit, "us") == 0) {
        *microseconds = (unsigned long)(val);
    } else {
        *microseconds = (unsigned long)(val * 1000);
    }
    return 0;
}

int parse_percentage(const char *str, double *probability) {
    if (!str || !probability) return -1;
    double val;
    char suffix[16] = "";
    if (sscanf(str, "%lf%15s", &val, suffix) < 1) return -1;
    if (strchr(suffix, '%')) {
        *probability = val / 100.0;
    } else {
        *probability = val;
    }
    return 0;
}

int rule_config_equal(const rule_config_t *a, const rule_config_t *b) {
    if (!a || !b) return 0;
    return (strcmp(a->name, b->name) == 0 &&
            strcmp(a->rate_limit, b->rate_limit) == 0 &&
            strcmp(a->delay, b->delay) == 0 &&
            strcmp(a->loss, b->loss) == 0 &&
            strcmp(a->dup, b->dup) == 0 &&
            strcmp(a->reorder, b->reorder) == 0 &&
            a->burst_kbytes == b->burst_kbytes &&
            a->latency_ms == b->latency_ms);
}

void rule_config_copy(rule_config_t *dst, const rule_config_t *src) {
    if (!dst || !src) return;
    memcpy(dst, src, sizeof(*dst));
}

static void daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    if (setsid() < 0) exit(EXIT_FAILURE);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);
    if (chdir("/") < 0) { /* ignore */ }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_RDWR);
}

static void print_banner(void) {
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║      vshaper v%s - 虚拟网卡流量整形 / 故障注入平台      ║\n",
           VSHAPER_VERSION);
    printf("╚═══════════════════════════════════════════════════════════╝\n");
}

static int run_dry_run_mode(tun_device_t *tun) {
    dry_run_ctx_t dry_ctx;
    memset(&dry_ctx, 0, sizeof(dry_ctx));

    rule_config_t *active_rule = NULL;
    if (g_config.num_rules > 0) {
        active_rule = &g_config.rules[0];
    } else {
        rule_config_t def;
        memset(&def, 0, sizeof(def));
        strncpy(def.name, "default", sizeof(def.name) - 1);
        strncpy(def.rate_limit, "1mbit", sizeof(def.rate_limit) - 1);
        strncpy(def.loss, "5%", sizeof(def.loss) - 1);
        strncpy(def.delay, "100ms", sizeof(def.delay) - 1);
        strncpy(def.dup, "1%", sizeof(def.dup) - 1);
        strncpy(def.reorder, "2%", sizeof(def.reorder) - 1);
        def.burst_kbytes = 16;
        def.latency_ms = 50;
        memcpy(&g_config.rules[0], &def, sizeof(def));
        g_config.num_rules = 1;
        active_rule = &g_config.rules[0];
    }

    if (dry_run_init(&dry_ctx, g_config.ifname, active_rule, tun->fd) != 0) {
        fprintf(stderr, "[dry-run] 初始化失败\n");
        return -1;
    }

    printf("\n[dry-run] ═══ 开始模拟 (Ctrl+C 停止) ═══\n\n");

    unsigned long seq = 0;
    while (g_running) {
        unsigned char buf[65536];
        int n = tun_device_read(tun, buf, sizeof(buf));
        if (n > 0) {
            seq++;
            dry_run_process_packet(&dry_ctx, buf, (size_t)n, seq);
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            if (g_running) {
                fprintf(stderr, "[dry-run] 读取错误: %s\n", strerror(errno));
            }
        }
    }

    printf("\n[dry-run] 正在停止...\n");
    dry_run_destroy(&dry_ctx);
    return 0;
}

static int run_pcap_replay(cli_options_t *opts) {
    pcap_replay_t replay;
    memset(&replay, 0, sizeof(replay));

    if (pcap_replay_init(&replay, g_config.pcap_file,
                         g_config.ifname,
                         opts->pcap_loop_count,
                         opts->pcap_speed_factor) != 0) {
        return -1;
    }

    if (g_config.num_rules > 0) {
        rule_config_copy(&g_current_rule, &g_config.rules[0]);
        tc_shaper_t shaper;
        memset(&shaper, 0, sizeof(shaper));
        tc_shaper_init(&shaper, g_config.ifname, &g_config.rules[0]);
        tc_shaper_apply(&shaper);

        pcap_replay_start(&replay);

        tc_shaper_destroy(&shaper);
    } else {
        pcap_replay_start(&replay);
    }

    pcap_replay_destroy(&replay);
    return 0;
}

static int run_rollback(int steps) {
    rule_config_t restored;
    if (rule_history_rollback(&g_rule_history, steps, &restored) != 0) {
        fprintf(stderr, "[rollback] 回滚失败\n");
        return -1;
    }

    tc_shaper_t shaper;
    memset(&shaper, 0, sizeof(shaper));
    tc_shaper_init(&shaper, g_config.ifname, &restored);
    tc_shaper_apply(&shaper);

    rule_config_copy(&g_current_rule, &restored);
    printf("[rollback] 已回滚 %d 步，当前规则: %s\n", steps, restored.name);

    if (g_config.syslog_enabled) {
        syslog(LOG_INFO, "vshaper: manual rollback %d steps -> rule '%s'",
               steps, restored.name);
    }

    tc_shaper_destroy(&shaper);
    return 0;
}

int main(int argc, char *argv[]) {
    cli_options_t opts;
    tun_device_t tun;
    tc_shaper_t shapers[MAX_RULES];
    stats_info_t stats;
    cron_scheduler_t scheduler;
    http_api_server_t http_server;
    int ret = EXIT_SUCCESS;
    int num_shapers = 0;

    memset(&opts, 0, sizeof(opts));
    memset(&tun, 0, sizeof(tun));
    memset(shapers, 0, sizeof(shapers));
    memset(&stats, 0, sizeof(stats));
    memset(&scheduler, 0, sizeof(scheduler));
    memset(&http_server, 0, sizeof(http_server));
    memset(&g_current_rule, 0, sizeof(g_current_rule));
    tun.fd = -1;

    rule_history_init(&g_rule_history);

    if (cli_parse_args(argc, argv, &opts, &g_config) != 0) {
        cli_print_help(argv[0]);
        return EXIT_FAILURE;
    }

    if (opts.show_help) {
        cli_print_help(argv[0]);
        return EXIT_SUCCESS;
    }

    if (opts.show_version) {
        printf("vshaper version %s\n", VSHAPER_VERSION);
        return EXIT_SUCCESS;
    }

    if (opts.config_file[0] != '\0') {
        if (config_file_load(opts.config_file, &g_config) != 0) {
            fprintf(stderr, "[main] 配置文件加载失败\n");
            return EXIT_FAILURE;
        }
    }

    if (opts.show_stats) {
        if (stats_collect(g_config.ifname, &stats) == 0) {
            stats_print(g_config.ifname, &stats);
        } else {
            fprintf(stderr, "[main] 无法收集统计信息\n");
        }
        return EXIT_SUCCESS;
    }

    if (opts.show_history) {
        rule_history_print(&g_rule_history);
        return EXIT_SUCCESS;
    }

    if (opts.rollback_steps > 0) {
        return run_rollback(opts.rollback_steps) == 0 ?
               EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (g_config.syslog_enabled) {
        openlog("vshaper", LOG_PID | LOG_NDELAY, LOG_LOCAL0);
        syslog(LOG_INFO, "vshaper started (v%s)", VSHAPER_VERSION);
    }

    if (g_config.num_rules <= 0) {
        fprintf(stderr, "[main] 未配置任何流量规则，使用默认规则\n");
        rule_config_t def;
        memset(&def, 0, sizeof(def));
        strncpy(def.name, "default", sizeof(def.name) - 1);
        if (opts.rate_limit[0]) strncpy(def.rate_limit, opts.rate_limit,
                                         sizeof(def.rate_limit) - 1);
        if (opts.delay[0]) strncpy(def.delay, opts.delay,
                                    sizeof(def.delay) - 1);
        if (opts.loss[0]) strncpy(def.loss, opts.loss,
                                   sizeof(def.loss) - 1);
        if (opts.dup[0]) strncpy(def.dup, opts.dup,
                                  sizeof(def.dup) - 1);
        if (opts.reorder[0]) strncpy(def.reorder, opts.reorder,
                                      sizeof(def.reorder) - 1);
        def.burst_kbytes = opts.burst_kbytes;
        def.latency_ms = opts.latency_ms;
        memcpy(&g_config.rules[0], &def, sizeof(def));
        g_config.num_rules = 1;
    }

    if (config_file_validate(&g_config) != 0) {
        return EXIT_FAILURE;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    print_banner();

    if (g_config.dry_run_mode)
        printf("[main] ═══ DRY-RUN 模拟模式 ═══\n");
    if (g_config.http_enabled)
        printf("[main] ═══ HTTP API 已启用 (端口 %d) ═══\n", g_config.http_port);
    if (g_config.scheduler_enabled)
        printf("[main] ═══ Cron 调度器已启用 ═══\n");
    if (g_config.syslog_enabled)
        printf("[main] ═══ Syslog 日志已启用 ═══\n");
    if (g_config.pcap_replay_mode)
        printf("[main] ═══ PCAP 回放模式 ═══\n");

    if (g_config.daemon_mode) {
        printf("[main] 切换到后台运行模式\n");
        daemonize();
    }

    printf("[main] 创建虚拟网卡: %s\n", g_config.ifname);
    if (tun_device_create(&tun, g_config.ifname, g_config.ip,
                          g_config.netmask, 1500) != 0) {
        fprintf(stderr, "[main] 虚拟网卡创建失败\n");
        return EXIT_FAILURE;
    }

    if (tun_device_set_mtu(&tun) != 0) {
        fprintf(stderr, "[main] 设置MTU失败\n");
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    if (tun_device_set_ip(&tun) != 0) {
        fprintf(stderr, "[main] 设置IP失败\n");
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    if (tun_device_up(&tun) != 0) {
        fprintf(stderr, "[main] 启用网卡失败\n");
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    if (g_config.pcap_replay_mode) {
        ret = run_pcap_replay(&opts);
        goto cleanup;
    }

    if (g_config.dry_run_mode) {
        ret = run_dry_run_mode(&tun);
        goto cleanup;
    }

    rule_config_copy(&g_current_rule, &g_config.rules[0]);

    for (int i = 0; i < g_config.num_rules && i < MAX_RULES; i++) {
        printf("[main] 应用规则 #%d: %s\n", i + 1,
               g_config.rules[i].name);

        if (tc_shaper_init(&shapers[num_shapers], g_config.ifname,
                           &g_config.rules[i]) != 0) {
            fprintf(stderr, "[main] 规则初始化失败\n");
            ret = EXIT_FAILURE;
            goto cleanup;
        }

        if (tc_shaper_apply(&shapers[num_shapers]) != 0) {
            fprintf(stderr, "[main] 规则应用失败\n");
            ret = EXIT_FAILURE;
            goto cleanup;
        }
        num_shapers++;
    }

    if (g_config.scheduler_enabled) {
        if (cron_scheduler_init(&scheduler) != 0) {
            fprintf(stderr, "[main] 调度器初始化失败\n");
        } else {
            if (g_config.num_rules > 0) {
                cron_scheduler_add_task(&scheduler,
                    "workday_business",
                    "0 9-18 * * 1-5",
                    &g_config.rules[0], 100);
            }
            rule_config_t free_rule;
            memset(&free_rule, 0, sizeof(free_rule));
            strncpy(free_rule.name, "unlimited", sizeof(free_rule.name) - 1);
            strncpy(free_rule.rate_limit, "100mbit",
                    sizeof(free_rule.rate_limit) - 1);
            free_rule.burst_kbytes = 64;
            free_rule.latency_ms = 50;
            cron_scheduler_add_task(&scheduler,
                "off_hours",
                "0 0-8,19-23 * * 1-5",
                &free_rule, 50);
            cron_scheduler_add_task(&scheduler,
                "weekend",
                "0 * * * 0,6",
                &free_rule, 50);

            cron_scheduler_start(&scheduler);
        }
    }

    if (g_config.http_enabled) {
        if (http_api_init(&http_server, g_config.http_port) == 0) {
            http_api_start(&http_server);
        }
    }

    printf("[main] vshaper 已启动运行 (Ctrl+C 退出)\n");
    printf("[main] 规则: %d 条 | 接口: %s | IP: %s\n",
           g_config.num_rules, g_config.ifname, g_config.ip);
    if (g_config.http_enabled)
        printf("[main] HTTP API: http://0.0.0.0:%d/api/status\n", g_config.http_port);

    while (g_running) {
        unsigned char buf[65536];
        int n = tun_device_read(&tun, buf, sizeof(buf));
        if (n > 0) {
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            if (g_running) {
                fprintf(stderr, "[main] 读取错误: %s\n", strerror(errno));
            }
        }

        usleep(100000);
    }

    printf("[main] 正在清理资源...\n");

    stats_collect(g_config.ifname, &stats);
    stats_print(g_config.ifname, &stats);

    if (g_config.syslog_enabled) {
        syslog(LOG_INFO, "vshaper: shutdown, rx=%lu tx=%lu dropped=%lu",
               stats.rx_packets, stats.tx_packets,
               stats.rx_dropped + stats.tx_dropped);
    }

cleanup:
    if (g_config.scheduler_enabled)
        cron_scheduler_destroy(&scheduler);
    if (g_config.http_enabled)
        http_api_destroy(&http_server);
    for (int i = 0; i < num_shapers; i++) {
        tc_shaper_destroy(&shapers[i]);
    }
    tun_device_close(&tun);
    rule_history_destroy(&g_rule_history);

    if (g_config.syslog_enabled) {
        syslog(LOG_INFO, "vshaper: exited");
        closelog();
    }

    printf("[main] vshaper 已退出\n");
    return ret;
}
