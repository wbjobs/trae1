#include "mysql_firewall.h"
#include "tcp_reassembly.h"
#include "mysql_parser.h"
#include "injection_detect.h"
#include "whitelist.h"
#include "stmt_cache.h"
#include "config.h"

global_ctx_t g_ctx = {0};
stats_t g_stats = {0};
static volatile int g_reload = 0;

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options]\n"
            "Options:\n"
            "  --stats              Show statistics and exit\n"
            "  --debug              Enable debug logging\n"
            "  --learn              Enable active learning mode\n"
            "  --sandbox-host HOST  Sandbox MySQL host (default: 127.0.0.1)\n"
            "  --sandbox-port PORT  Sandbox MySQL port (default: 3307)\n"
            "  --sandbox-user USER  Sandbox MySQL user (default: sandbox)\n"
            "  --sandbox-pass PASS  Sandbox MySQL password\n"
            "  --sandbox-db DB      Sandbox MySQL database (default: sandbox_db)\n"
            "  --export FILE        Export learned rules to file after learning\n"
            "  --import FILE        Import previously learned rules\n"
            "  --help               Show this help message\n", prog);
}

static void print_stats(void) {
    pthread_mutex_lock(&g_stats.mutex);
    printf("=== MySQL Firewall Statistics ===\n");
    printf("Total Packets:        %lu\n", g_stats.total_packets);
    printf("Total Inspections:    %lu\n", g_stats.total_inspections);
    printf("Total Suspicious:     %lu\n", g_stats.total_suspicious);
    printf("Total Blocks:         %lu\n", g_stats.total_blocks);
    printf("Total Allows:         %lu\n", g_stats.total_allows);
    printf("Regex Matches:        %lu\n", g_stats.regex_matches);
    printf("Syntax Matches:       %lu\n", g_stats.syntax_matches);
    printf("Learning Matches:     %lu\n", g_stats.learning_matches);
    printf("Sandbox Verified:     %lu\n", g_stats.sandbox_verified);
    printf("================================\n");
    if (g_ctx.learning) {
        printf("\n=== Learning Status ===\n");
        printf("Learning Mode:        %s\n", g_ctx.learning->enabled ? "ON" : "OFF");
        printf("Total Learned:        %lu\n", g_ctx.learning->total_learned);
        printf("Total Verified:       %lu\n", g_ctx.learning->total_verified);
        printf("Rules Count:          %zu\n", g_ctx.learning->rule_count);
        printf("Learning Period:      %d days\n", LEARNING_PERIOD_DAYS);
        printf("========================\n");
    }
    pthread_mutex_unlock(&g_stats.mutex);
}

void signal_handler(int sig) {
    if (sig == SIGHUP) {
        g_reload = 1;
        log_message(0, "Received SIGHUP, reloading rules...");
    } else if (sig == SIGINT || sig == SIGTERM) {
        g_ctx.running = 0;
        log_message(0, "Received signal %d, shutting down...", sig);
    }
}

static int nfqueue_callback(struct nfq_q_handle *qh, struct nfgenmsg *nfmsg,
                            struct nfq_data *nfa, void *data) {
    (void)nfmsg;
    (void)data;

    struct nfqnl_msg_packet_hdr *ph = nfq_get_msg_packet_hdr(nfa);
    if (!ph) return NF_ACCEPT;

    uint32_t id = ntohl(ph->packet_id);
    uint32_t mark = nfq_get_nfmark(nfa);

    int payload_len = 0;
    char *payload = extract_tcp_payload(qh, nfa, &payload_len);
    if (!payload || payload_len <= 0) {
        return NF_ACCEPT;
    }

    struct iphdr *ip = (struct iphdr *)(payload - sizeof(struct iphdr));
    struct tcphdr *tcp = (struct tcphdr *)((char *)ip + sizeof(struct iphdr));

    char src_ip[INET_ADDRSTRLEN];
    char dst_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ip->saddr, src_ip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &ip->daddr, dst_ip, INET_ADDRSTRLEN);
    uint16_t src_port = ntohs(tcp->source);
    uint16_t dst_port = ntohs(tcp->dest);

    pthread_mutex_lock(&g_stats.mutex);
    g_stats.total_packets++;
    pthread_mutex_unlock(&g_stats.mutex);

    if (dst_port != MYSQL_PORT && src_port != MYSQL_PORT) {
        free(payload);
        return NF_ACCEPT;
    }

    if (is_whitelisted(src_ip)) {
        free(payload);
        return NF_ACCEPT;
    }

    tcp_stream_t *stream = tcp_reassembly_get_stream(src_ip, dst_ip, src_port, dst_port, ip->saddr, ip->daddr);

    uint8_t *tcp_data = (uint8_t *)tcp + (tcp->doff * 4);
    int tcp_data_len = payload_len - ((uint8_t *)tcp_data - (uint8_t *)ip);

    if (tcp_data_len > 0) {
        tcp_reassembly_add_data(stream, tcp_data, tcp_data_len, tcp->th_flags & TH_SYN, tcp->th_flags & TH_FIN);
    }

    char *完整sql = extract_mysql_sql(stream);
    if (完整sql) {
        pthread_mutex_lock(&g_stats.mutex);
        g_stats.total_inspections++;
        pthread_mutex_unlock(&g_stats.mutex);

        injection_result_t result = detect_injection(完整sql);

        if (!result.detected && result.suspicious && g_ctx.learn_mode && g_ctx.learning) {
            pthread_mutex_lock(&g_stats.mutex);
            g_stats.total_suspicious++;
            pthread_mutex_unlock(&g_stats.mutex);

            sandbox_report_t report = {0};
            if (g_ctx.use_sandbox && g_ctx.sandbox) {
                report = sandbox_execute(g_ctx.sandbox, 完整sql);
                pthread_mutex_lock(&g_stats.mutex);
                g_stats.sandbox_verified++;
                pthread_mutex_unlock(&g_stats.mutex);
            }

            int is_injection = learning_process_suspicious(g_ctx.learning, 完整sql, result.score,
                                                          g_ctx.use_sandbox ? &report : NULL);

            if (is_injection) {
                result.detected = 1;
                result.match_type = MATCH_LEARNING;
                strncpy(result.pattern, "LEARNING_SANDBOX_CONFIRMED", sizeof(result.pattern) - 1);
                snprintf(result.details, sizeof(result.details),
                        "Sandbox confirmed injection, score: %d, risk: %d",
                        result.score, report.risk_score);
            }

            log_message(2, "SUSPICIOUS [score:%d] %s:%d -> %s:%d | SQL: %s | sandbox: %s",
                       result.score, src_ip, src_port, dst_ip, dst_port,
                       完整sql,
                       g_ctx.use_sandbox ? (report.result == SANDBOX_RESULT_SAFE ? "SAFE" :
                                  report.result == SANDBOX_RESULT_SUSPICIOUS ? "SUSPICIOUS" :
                                  report.result == SANDBOX_RESULT_DANGEROUS ? "DANGEROUS" : "UNKNOWN") : "disabled");
        }

        if (result.detected) {
            pthread_mutex_lock(&g_stats.mutex);
            g_stats.total_blocks++;
            if (result.match_type == MATCH_REGEX) {
                g_stats.regex_matches++;
            } else if (result.match_type == MATCH_LEARNING) {
                g_stats.learning_matches++;
            } else {
                g_stats.syntax_matches++;
            }
            pthread_mutex_unlock(&g_stats.mutex);

            log_message(1, "BLOCKED [%s] %s:%d -> %s:%d | SQL: %s | Reason: %s | score: %d",
                       result.match_type == MATCH_REGEX ? "REGEX" :
                       result.match_type == MATCH_LEARNING ? "LEARNING" : "SYNTAX",
                       src_ip, src_port, dst_ip, dst_port,
                       完整sql, result.pattern, result.score);

            free(完整sql);
            tcp_reassembly_remove_stream(stream);
            free(payload);
            return NF_DROP;
        } else {
            pthread_mutex_lock(&g_stats.mutex);
            g_stats.total_allows++;
            pthread_mutex_unlock(&g_stats.mutex);

            if (g_ctx.learn_mode && g_ctx.learning) {
                learning_update_model(g_ctx.learning, 完整sql, 0);
            }
        }
        free(完整sql);
    }

    if (tcp->th_flags & TH_FIN || tcp->th_flags & TH_RST) {
        tcp_reassembly_remove_stream(stream);
    }

    free(payload);
    return NF_ACCEPT;
}

int setup_nfqueue(void) {
    struct nfq_handle *h = nfq_open();
    if (!h) {
        log_message(3, "Failed to open nfq handle");
        return -1;
    }

    if (nfq_bind_pf(h, AF_INET) < 0) {
        log_message(3, "Failed to bind pf");
        nfq_close(h);
        return -1;
    }

    struct nfq_q_handle *qh = nfq_create_queue(h, NFQUEUE_NUM, nfqueue_callback, NULL);
    if (!qh) {
        log_message(3, "Failed to create queue");
        nfq_close(h);
        return -1;
    }

    if (nfq_set_mode(qh, NFQNL_COPY_PACKET, 0xFFFF) < 0) {
        log_message(3, "Failed to set queue mode");
        nfq_destroy_queue(qh);
        nfq_close(h);
        return -1;
    }

    int fd = nfq_fd(h);
    log_message(0, "NFQUEUE %d initialized successfully", NFQUEUE_NUM);

    while (g_ctx.running) {
        char buf[4096] __attribute__ ((aligned));
        int rv = recv(fd, buf, sizeof(buf), 0);

        if (rv > 0) {
            nfq_handle_packet(h, buf, rv);
        } else if (rv < 0 && errno != EINTR) {
            break;
        }

        if (g_reload) {
            reload_whitelist();
            reload_rules();
            g_reload = 0;
        }
    }

    nfq_destroy_queue(qh);
    nfq_close(h);
    return 0;
}

void *stats_thread(void *arg) {
    (void)arg;
    while (g_ctx.running) {
        sleep(STATS_INTERVAL);
        if (!g_ctx.running) break;
        print_stats();
    }
    return NULL;
}

void log_message(int level, const char *fmt, ...) {
    if (level > 0 && !g_ctx.debug) return;

    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    pthread_mutex_lock(&g_ctx.log_mutex);
    FILE *fp = g_ctx.logfile ? g_ctx.logfile : stderr;

    if (level == 0) {
        fprintf(fp, "[%s] ", timestamp);
    } else if (level == 1) {
        fprintf(fp, "[%s] BLOCK: ", timestamp);
    } else if (level == 2) {
        fprintf(fp, "[%s] WARN: ", timestamp);
    } else {
        fprintf(fp, "[%s] ERROR: ", timestamp);
    }

    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fprintf(fp, "\n");
    fflush(fp);
    pthread_mutex_unlock(&g_ctx.log_mutex);
}

char *extract_tcp_payload(struct nfq_q_handle *qh, struct nfq_data *nfa, int *payload_len) {
    *payload_len = nfq_get_payload(nfa, (char **)payload_len);
    if (*payload_len < 0) return NULL;

    unsigned char *data = NULL;
    *payload_len = nfq_get_payload(nfa, (char **)&data);
    if (*payload_len < 0 || !data) return NULL;

    char *buf = malloc(*payload_len + 1);
    if (!buf) return NULL;
    memcpy(buf, data, *payload_len);
    buf[*payload_len] = 0;

    return buf;
}

int main(int argc, char *argv[]) {
    int show_stats_only = 0;

    g_ctx.sandbox_port = 3307;
    strcpy(g_ctx.sandbox_host, "127.0.0.1");
    strcpy(g_ctx.sandbox_user, "sandbox");
    strcpy(g_ctx.sandbox_pass, "sandbox_pass");
    strcpy(g_ctx.sandbox_db, "sandbox_db");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--stats") == 0) {
            show_stats_only = 1;
        } else if (strcmp(argv[i], "--debug") == 0) {
            g_ctx.debug = 1;
        } else if (strcmp(argv[i], "--learn") == 0) {
            g_ctx.learn_mode = 1;
        } else if (strcmp(argv[i], "--sandbox-host") == 0 && i + 1 < argc) {
            strncpy(g_ctx.sandbox_host, argv[++i], sizeof(g_ctx.sandbox_host) - 1);
            g_ctx.use_sandbox = 1;
        } else if (strcmp(argv[i], "--sandbox-port") == 0 && i + 1 < argc) {
            g_ctx.sandbox_port = atoi(argv[++i]);
            g_ctx.use_sandbox = 1;
        } else if (strcmp(argv[i], "--sandbox-user") == 0 && i + 1 < argc) {
            strncpy(g_ctx.sandbox_user, argv[++i], sizeof(g_ctx.sandbox_user) - 1);
            g_ctx.use_sandbox = 1;
        } else if (strcmp(argv[i], "--sandbox-pass") == 0 && i + 1 < argc) {
            strncpy(g_ctx.sandbox_pass, argv[++i], sizeof(g_ctx.sandbox_pass) - 1);
            g_ctx.use_sandbox = 1;
        } else if (strcmp(argv[i], "--sandbox-db") == 0 && i + 1 < argc) {
            strncpy(g_ctx.sandbox_db, argv[++i], sizeof(g_ctx.sandbox_db) - 1);
            g_ctx.use_sandbox = 1;
        } else if (strcmp(argv[i], "--export") == 0 && i + 1 < argc) {
            strncpy(g_ctx.export_path, argv[++i], sizeof(g_ctx.export_path) - 1);
        } else if (strcmp(argv[i], "--import") == 0 && i + 1 < argc) {
            strncpy(g_ctx.import_path, argv[++i], sizeof(g_ctx.import_path) - 1);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    g_ctx.logfile = fopen(LOG_FILE, "a");
    if (!g_ctx.logfile) {
        g_ctx.logfile = stderr;
        fprintf(stderr, "Warning: Could not open log file %s, using stderr\n", LOG_FILE);
    }

    pthread_mutex_init(&g_ctx.log_mutex, NULL);
    pthread_mutex_init(&g_stats.mutex, NULL);

    tcp_reassembly_init();
    injection_detector_init();
    whitelist_init();
    get_stmt_cache();

    g_ctx.sandbox = sandbox_create();
    g_ctx.learning = learning_create();

    if (g_ctx.use_sandbox) {
        sandbox_init(g_ctx.sandbox, g_ctx.sandbox_host, g_ctx.sandbox_port,
                     g_ctx.sandbox_user, g_ctx.sandbox_pass, g_ctx.sandbox_db);
    }

    if (g_ctx.learning) {
        learning_init(g_ctx.learning, g_ctx.sandbox);
        set_learning_context(g_ctx.learning);

        if (g_ctx.import_path[0]) {
            int imported = learning_import_rules(g_ctx.learning, g_ctx.import_path);
            log_message(0, "Imported %d learned rules from %s", imported, g_ctx.import_path);
        }
    }

    if (g_ctx.learn_mode && g_ctx.learning) {
        learning_start(g_ctx.learning);
        log_message(0, "Active learning mode enabled, period: %d days", LEARNING_PERIOD_DAYS);
        if (g_ctx.use_sandbox) {
            log_message(0, "Sandbox verification enabled: %s:%d/%s",
                       g_ctx.sandbox_host, g_ctx.sandbox_port, g_ctx.sandbox_db);
        }
    }

    if (show_stats_only) {
        print_stats();
        if (g_ctx.learning && g_ctx.export_path[0]) {
            learning_export_rules(g_ctx.learning, g_ctx.export_path);
            log_message(0, "Exported learned rules to %s", g_ctx.export_path);
        }
        whitelist_destroy();
        injection_detector_destroy();
        tcp_reassembly_destroy();
        stmt_cache_destroy(get_stmt_cache());
        if (g_ctx.learning) {
            learning_stop(g_ctx.learning);
            learning_destroy(g_ctx.learning);
        }
        if (g_ctx.sandbox) {
            sandbox_destroy(g_ctx.sandbox);
        }
        if (g_ctx.logfile && g_ctx.logfile != stderr) fclose(g_ctx.logfile);
        return 0;
    }

    g_ctx.running = 1;

    signal(SIGHUP, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    pthread_t stats_tid;
    pthread_create(&stats_tid, NULL, stats_thread, NULL);

    log_message(0, "MySQL Firewall starting...");
    log_message(0, "MySQL Port: %d, NFQueue: %d", MYSQL_PORT, NFQUEUE_NUM);
    log_message(0, "Log file: %s", LOG_FILE);
    log_message(0, "Config file: %s", CONFIG_FILE);
    if (g_ctx.learn_mode) {
        log_message(0, "Active Learning: ENABLED");
        log_message(0, "  Threshold Block: %d, Threshold Suspicious: %d",
                   DETECTION_THRESHOLD_BLOCK, DETECTION_THRESHOLD_SUSPICIOUS);
    }

    int ret = setup_nfqueue();

    pthread_join(stats_tid, NULL);

    if (g_ctx.learning && g_ctx.export_path[0]) {
        learning_export_rules(g_ctx.learning, g_ctx.export_path);
        log_message(0, "Exported learned rules to %s", g_ctx.export_path);
    }

    whitelist_destroy();
    injection_detector_destroy();
    tcp_reassembly_destroy();
    stmt_cache_destroy(get_stmt_cache());

    if (g_ctx.learning) {
        learning_stop(g_ctx.learning);
        learning_destroy(g_ctx.learning);
    }

    if (g_ctx.sandbox) {
        sandbox_destroy(g_ctx.sandbox);
    }

    pthread_mutex_destroy(&g_ctx.log_mutex);
    pthread_mutex_destroy(&g_stats.mutex);

    if (g_ctx.logfile && g_ctx.logfile != stderr) {
        fclose(g_ctx.logfile);
    }

    log_message(0, "MySQL Firewall stopped. Final stats:");
    print_stats();

    return ret;
}
