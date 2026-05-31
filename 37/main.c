#include "config.h"
#include "capture.h"
#include "tcp_reasm.h"
#include "mysql_parser.h"
#include "filter.h"
#include "output.h"
#include "alert.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/types.h>

int g_verbose = 0;

typedef struct {
    mysql_parser_t  *parser;
    mysql_filter_t  *filter;
    output_ctx_t    *output;
    alert_ctx_t     *alert;
    capture_t       *cap;
    tcp_reasm_t     *reasm;
    volatile sig_atomic_t stop;
} app_ctx_t;

static app_ctx_t g_app;

static void sig_handler(int sig) {
    (void)sig;
    g_app.stop = 1;
    if (g_app.cap && g_app.cap->handle) {
        pcap_breakloop(g_app.cap->handle);
    }
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -i, --interface IFACE   Network interface to sniff (default: auto)\n"
        "  -p, --port PORT         MySQL port (default: 3306)\n"
        "  -f, --format FMT        Output format: text|json|syslog (default: text)\n"
        "  -o, --output PATH       Output file path (default: stdout)\n"
        "      --syslog            Send events to syslog\n"
        "      --syslog-facility F  Syslog facility (default: local0)\n"
        "      --syslog-ident S     Syslog ident (default: mysql-sniffer)\n"
        "      --filter-sql TYPES   Comma-separated SQL types to capture (e.g. SELECT,INSERT)\n"
        "      --filter-user USER   Only capture queries for this user (substring match)\n"
        "      --filter-ip IP       Only capture queries from this client IP prefix\n"
        "      --case-sensitive     Case-sensitive user filter\n"
        "  -s, --snaplen N          Capture snap length (default: 65535)\n"
        "  -B, --buffer-size N      Kernel capture buffer size in bytes\n"
        "  -t, --timeout MS         pcap timeout in milliseconds\n"
        "  -P, --no-promisc         Disable promiscuous mode\n"
        "  -v, --verbose            Verbose debug output\n"
        "      --slow-ms MS          Slow query threshold in milliseconds (default: 100)\n"
        "      --slack-webhook URL    Slack incoming webhook URL for slow query alerts\n"
        "      --dingtalk-webhook URL DingTalk robot webhook URL for slow query alerts\n"
        "      --ring-size N          Ring buffer size for recent slow queries (default: 100)\n"
        "  -h, --help               Show this help\n"
        "\n"
        "SQL types: SELECT, INSERT, UPDATE, DELETE, REPLACE, CREATE, ALTER, DROP,\n"
        "           TRUNCATE, GRANT, SET, SHOW, USE, BEGIN, COMMIT, ROLLBACK, CALL,\n"
        "           PREPARE, EXECUTE, OTHER\n",
        prog);
}

static void on_mysql_event(const mysql_event_t *ev, void *user) {
    app_ctx_t *app = (app_ctx_t *)user;
    if (app->filter && !filter_match(app->filter, ev)) return;
    if (app->output) output_write(app->output, ev);
    if (app->alert) alert_check(app->alert, ev);
}

static void on_stream(tcp_dir_t dir, const uint8_t *data, size_t len,
                      uint64_t ts_sec, uint32_t ts_usec,
                      void *session_key, void *user) {
    (void)user;
    app_ctx_t *app = &g_app;

    char client_ip[64] = {0};
    uint16_t client_port = 0;
    tcp_reasm_get_client(session_key, dir, client_ip, sizeof(client_ip), &client_port);

    if (dir == DIR_C2S) {
        mysql_parser_set_client(app->parser, session_key, client_ip, client_port);
    }

    mysql_parser_feed(app->parser, session_key, (int)dir,
                      data, len, ts_sec, ts_usec, NULL, 0);
}

static void pkt_cb(unsigned char *user, const struct pcap_pkthdr *hdr,
                   const unsigned char *pkt) {
    (void)user;
    app_ctx_t *app = &g_app;
    int dl = app->cap->datalink;
    uint32_t ip_offset = 0;

    switch (dl) {
        case DLT_EN10MB:
            ip_offset = 14;
            if (hdr->caplen < ip_offset + 20) return;
            if (pkt[12] != 0x08 || pkt[13] != 0x00) return;
            break;
        case DLT_LINUX_SLL:
            ip_offset = 16;
            if (hdr->caplen < ip_offset + 20) return;
            break;
        case DLT_RAW:
            ip_offset = 0;
            if (hdr->caplen < 20) return;
            break;
        default:
            return;
    }

    const uint8_t *ip = pkt + ip_offset;
    if ((ip[0] >> 4) != 4) return;
    uint32_t ip_hdr_len = (ip[0] & 0x0f) * 4;
    if (ip_hdr_len < 20) return;
    if (hdr->caplen < ip_offset + ip_hdr_len) return;
    if (ip[9] != 6) return;

    uint32_t total_len = ((uint32_t)ip[2] << 8) | ip[3];
    if (total_len == 0) total_len = hdr->caplen - ip_offset;
    if (hdr->caplen < ip_offset + total_len) return;

    const uint8_t *tcp = ip + ip_hdr_len;
    if (hdr->caplen < ip_offset + ip_hdr_len + 20) return;
    uint32_t tcp_hdr_len = ((tcp[12] >> 4) & 0x0f) * 4;
    if (tcp_hdr_len < 20) return;
    if (hdr->caplen < ip_offset + ip_hdr_len + tcp_hdr_len) return;

    uint32_t payload_offset = ip_offset + ip_hdr_len + tcp_hdr_len;
    uint32_t payload_len = total_len - ip_hdr_len - tcp_hdr_len;
    if (hdr->caplen < payload_offset + payload_len) {
        payload_len = hdr->caplen - payload_offset;
    }

    uint16_t src_port = ((uint16_t)tcp[0] << 8) | tcp[1];
    uint16_t dst_port = ((uint16_t)tcp[2] << 8) | tcp[3];

    uint32_t src_ip = (uint32_t)ip[12] | ((uint32_t)ip[13] << 8) |
                      ((uint32_t)ip[14] << 16) | ((uint32_t)ip[15] << 24);
    uint32_t dst_ip = (uint32_t)ip[16] | ((uint32_t)ip[17] << 8) |
                      ((uint32_t)ip[18] << 16) | ((uint32_t)ip[19] << 24);

    uint32_t seq = (uint32_t)tcp[4] | ((uint32_t)tcp[5] << 8) |
                   ((uint32_t)tcp[6] << 16) | ((uint32_t)tcp[7] << 24);
    uint32_t ack = (uint32_t)tcp[8] | ((uint32_t)tcp[9] << 8) |
                   ((uint32_t)tcp[10] << 16) | ((uint32_t)tcp[11] << 24);
    uint8_t flags = tcp[13];
    uint16_t window = ((uint16_t)tcp[14] << 8) | tcp[15];

    tcp_reasm_input(app->reasm,
                    src_ip, src_port, dst_ip, dst_port,
                    seq, ack, flags, window,
                    payload_len ? pkt + payload_offset : NULL, payload_len,
                    (uint64_t)hdr->ts.tv_sec, (uint32_t)hdr->ts.tv_usec);
    (void)src_ip; (void)dst_ip;
}

static int parse_syslog_facility(const char *s) {
#ifndef _WIN32
    struct { const char *n; int v; } f[] = {
        {"kern", LOG_KERN}, {"user", LOG_USER}, {"mail", LOG_MAIL},
        {"daemon", LOG_DAEMON}, {"auth", LOG_AUTH}, {"syslog", LOG_SYSLOG},
        {"lpr", LOG_LPR}, {"news", LOG_NEWS}, {"uucp", LOG_UUCP},
        {"cron", LOG_CRON}, {"local0", LOG_LOCAL0}, {"local1", LOG_LOCAL1},
        {"local2", LOG_LOCAL2}, {"local3", LOG_LOCAL3}, {"local4", LOG_LOCAL4},
        {"local5", LOG_LOCAL5}, {"local6", LOG_LOCAL6}, {"local7", LOG_LOCAL7},
    };
    for (size_t i = 0; i < sizeof(f) / sizeof(f[0]); i++) {
        if (!strcasecmp(s, f[i].n)) return f[i].v;
    }
#endif
    return 0;
}

int main(int argc, char **argv) {
    static struct option long_opts[] = {
        {"interface",       required_argument, 0, 'i'},
        {"port",            required_argument, 0, 'p'},
        {"format",          required_argument, 0, 'f'},
        {"output",          required_argument, 0, 'o'},
        {"syslog",          no_argument,       0, 0x101},
        {"syslog-facility", required_argument, 0, 0x102},
        {"syslog-ident",    required_argument, 0, 0x103},
        {"filter-sql",      required_argument, 0, 0x104},
        {"filter-user",     required_argument, 0, 0x105},
        {"filter-ip",       required_argument, 0, 0x106},
        {"case-sensitive",  no_argument,       0, 0x107},
        {"snaplen",         required_argument, 0, 's'},
        {"buffer-size",     required_argument, 0, 'B'},
        {"timeout",         required_argument, 0, 't'},
        {"no-promisc",      no_argument,       0, 'P'},
        {"verbose",         no_argument,       0, 'v'},
        {"help",            no_argument,       0, 'h'},
        {"slow-ms",         required_argument, 0, 0x110},
        {"slack-webhook",   required_argument, 0, 0x111},
        {"dingtalk-webhook",required_argument, 0, 0x112},
        {"ring-size",       required_argument, 0, 0x113},
        {0, 0, 0, 0}
    };

    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.format = FMT_TEXT;
    cfg.snaplen = SNAP_LEN;
    cfg.promisc = 1;
    cfg.timeout_ms = 100;
    cfg.buffer_size = 32 * 1024 * 1024;
    cfg.slow_threshold_ms = 100.0;
    cfg.ring_buffer_size = 100;
    int mysql_port = MYSQL_PORT;
    char syslog_facility_str[32] = "local0";
    char syslog_ident[64] = "mysql-sniffer";

    int c;
    while ((c = getopt_long(argc, argv, "i:p:f:o:s:B:t:Pvh", long_opts, NULL)) != -1) {
        switch (c) {
            case 'i': snprintf(cfg.interface, sizeof(cfg.interface), "%s", optarg); break;
            case 'p': mysql_port = atoi(optarg); break;
            case 'f':
                if (!strcasecmp(optarg, "json")) cfg.format = FMT_JSON;
                else if (!strcasecmp(optarg, "syslog")) cfg.format = FMT_SYSLOG;
                else cfg.format = FMT_TEXT;
                break;
            case 'o': snprintf(cfg.output_path, sizeof(cfg.output_path), "%s", optarg); break;
            case 0x101: cfg.use_syslog = 1; break;
            case 0x102: snprintf(syslog_facility_str, sizeof(syslog_facility_str), "%s", optarg); break;
            case 0x103: snprintf(syslog_ident, sizeof(syslog_ident), "%s", optarg); break;
            case 0x104: snprintf(cfg.filter_sql_types, sizeof(cfg.filter_sql_types), "%s", optarg); break;
            case 0x105: snprintf(cfg.filter_user, sizeof(cfg.filter_user), "%s", optarg); break;
            case 0x106: snprintf(cfg.filter_client_ip, sizeof(cfg.filter_client_ip), "%s", optarg); break;
            case 0x107: cfg.case_sensitive = 1; break;
            case 's': cfg.snaplen = atoi(optarg); break;
            case 'B': cfg.buffer_size = (size_t)atoll(optarg); break;
            case 't': cfg.timeout_ms = atoi(optarg); break;
            case 'P': cfg.promisc = 0; break;
            case 'v': g_verbose = 1; cfg.verbose = 1; break;
            case 0x110: cfg.slow_threshold_ms = atof(optarg); break;
            case 0x111: snprintf(cfg.slack_webhook_url, sizeof(cfg.slack_webhook_url), "%s", optarg); cfg.alert_enabled = 1; break;
            case 0x112: snprintf(cfg.dingtalk_webhook_url, sizeof(cfg.dingtalk_webhook_url), "%s", optarg); cfg.alert_enabled = 1; break;
            case 0x113: cfg.ring_buffer_size = (size_t)atoll(optarg); break;
            case 'h': usage(argv[0]); return 0;
            default: usage(argv[0]); return 1;
        }
    }

    if (cfg.format == FMT_SYSLOG) cfg.use_syslog = 1;

    memset(&g_app, 0, sizeof(g_app));

    g_app.parser = mysql_parser_new(on_mysql_event, &g_app);
    if (!g_app.parser) die("cannot create parser");

    g_app.filter = filter_new(
        cfg.filter_sql_types[0] ? cfg.filter_sql_types : NULL,
        cfg.filter_user[0] ? cfg.filter_user : NULL,
        cfg.filter_client_ip[0] ? cfg.filter_client_ip : NULL,
        cfg.case_sensitive);

    g_app.output = output_new(cfg.format,
                              cfg.output_path[0] ? cfg.output_path : NULL,
                              cfg.use_syslog,
                              syslog_ident,
                              parse_syslog_facility(syslog_facility_str));
    if (!g_app.output) die("cannot create output");

    g_app.reasm = tcp_reasm_new(1024, 65536, 300, on_stream, &g_app);
    if (!g_app.reasm) die("cannot create tcp reassembler");

    if (cfg.alert_enabled || cfg.slow_threshold_ms > 0) {
        g_app.alert = alert_init(cfg.slow_threshold_ms,
                                  cfg.slack_webhook_url[0] ? cfg.slack_webhook_url : NULL,
                                  cfg.dingtalk_webhook_url[0] ? cfg.dingtalk_webhook_url : NULL,
                                  cfg.ring_buffer_size);
        if (g_app.alert) {
            info("slow query alert enabled: threshold=%.1fms, slack=%s, dingtalk=%s, ring=%zu",
                 cfg.slow_threshold_ms,
                 cfg.slack_webhook_url[0] ? "yes" : "no",
                 cfg.dingtalk_webhook_url[0] ? "yes" : "no",
                 cfg.ring_buffer_size);
        }
    }

    const char *iface = cfg.interface[0] ? cfg.interface : NULL;
    g_app.cap = capture_open(iface, cfg.snaplen, cfg.promisc, cfg.timeout_ms, cfg.buffer_size);
    if (!g_app.cap) die("cannot open capture interface");

    char bpf[128];
    snprintf(bpf, sizeof(bpf), "tcp port %d", mysql_port);
    if (capture_apply_filter(g_app.cap, bpf) != 0) {
        warn("BPF filter set failed, continuing");
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    info("mysql-sniffer started: iface=%s port=%d format=%d",
         iface ? iface : "default", mysql_port, cfg.format);

    capture_loop(g_app.cap, pkt_cb, NULL);

    info("shutting down...");
    capture_stats_print(g_app.cap);
    output_flush(g_app.output);

    tcp_reasm_free(g_app.reasm);
    capture_close(g_app.cap);
    alert_free(g_app.alert);
    output_free(g_app.output);
    filter_free(g_app.filter);
    mysql_parser_free(g_app.parser);

    return 0;
}
