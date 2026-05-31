#include "proxy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>

static ProxyContext g_ctx;

static void signal_handler(int sig)
{
    switch (sig) {
    case SIGINT:
    case SIGTERM:
        g_ctx.running = 0;
        break;
    case SIGHUP:
        fprintf(stderr, "[Signal] SIGHUP received, hot-reloading TLS certificate and sensitive words...\n");
        proxy_hot_reload_cert(&g_ctx);
        proxy_hot_reload_sensitive_words(&g_ctx);
        break;
    case SIGUSR1:
        fprintf(stderr, "[Signal] SIGUSR1 received, hot-reloading sensitive words...\n");
        proxy_hot_reload_sensitive_words(&g_ctx);
        break;
    }
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "VNC Watermark Audit Proxy (with VeNCrypt/TLS + OCR)\n");
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "\nCore Options:\n");
    fprintf(stderr, "  -s, --server HOST       Real VNC server host (default: 127.0.0.1)\n");
    fprintf(stderr, "  -p, --port PORT         Real VNC server port (default: 5900)\n");
    fprintf(stderr, "  -w, --password PASS     Real VNC server password\n");
    fprintf(stderr, "  -l, --listen PORT       Proxy listen port (default: 5901)\n");
    fprintf(stderr, "  -u, --user NAME         Username for audit logging (default: anonymous)\n");
    fprintf(stderr, "  -d, --db PATH           SQLite database path (default: data/audit.db)\n");
    fprintf(stderr, "  -r, --record DIR        Screenshot recording directory (default: recordings)\n");
    fprintf(stderr, "\nTLS/VeNCrypt Options:\n");
    fprintf(stderr, "  -t, --tls               Enable TLS/VeNCrypt (default: off)\n");
    fprintf(stderr, "  --tls-validity DAYS     Self-signed cert validity in days (default: 365)\n");
    fprintf(stderr, "  --tls-no-auto-renew     Disable automatic certificate renewal\n");
    fprintf(stderr, "  --tls-cert PATH         Path to existing TLS certificate (PEM)\n");
    fprintf(stderr, "  --tls-key PATH          Path to existing TLS private key (PEM)\n");
    fprintf(stderr, "\nOCR Options:\n");
    fprintf(stderr, "  --ocr                   Enable OCR sensitive content detection\n");
    fprintf(stderr, "  --ocr-interval SEC      OCR scan interval in seconds (default: 10)\n");
    fprintf(stderr, "  --ocr-lang LANG         Tesseract language (default: chi_sim+eng)\n");
    fprintf(stderr, "  --ocr-no-block          Don't block session on hit (alert only)\n");
    fprintf(stderr, "  --ocr-region X,Y,W,H    Limit OCR to region (e.g., 0,0,800,600)\n");
    fprintf(stderr, "  --sensitive-words FILE  Path to sensitive words list\n");
    fprintf(stderr, "\nEmail Alert Options:\n");
    fprintf(stderr, "  --smtp-host HOST        SMTP server host\n");
    fprintf(stderr, "  --smtp-port PORT        SMTP server port (default: 587)\n");
    fprintf(stderr, "  --smtp-user USER        SMTP username\n");
    fprintf(stderr, "  --smtp-password PASS    SMTP password\n");
    fprintf(stderr, "  --email-sender ADDR     From email address\n");
    fprintf(stderr, "  --email-to ADDR         Recipient email address (repeatable)\n");
    fprintf(stderr, "  --email-no-tls          Disable TLS for SMTP\n");
    fprintf(stderr, "\nSignals:\n");
    fprintf(stderr, "  SIGHUP    Hot-reload TLS cert and sensitive words\n");
    fprintf(stderr, "  SIGUSR1   Hot-reload sensitive words only\n");
    fprintf(stderr, "\n  -h, --help              Show this help\n");
}

int main(int argc, char *argv[])
{
    char server_host[256] = "127.0.0.1";
    int server_port = 5900;
    char server_password[256] = "";
    int proxy_port = 5901;
    char username[256] = "anonymous";
    char db_path[512] = "data/audit.db";
    char recordings_dir[512] = "recordings";
    int use_tls = 0;
    int tls_validity_days = 365;
    int tls_auto_renew = 1;
    char tls_cert_path[512] = "";
    char tls_key_path[512] = "";

    int use_ocr = 0;
    int ocr_interval = 10;
    int ocr_block_on_hit = 1;
    OcrRegion ocr_region;
    memset(&ocr_region, 0, sizeof(ocr_region));
    char ocr_lang[32] = "chi_sim+eng";
    char sensitive_words_file[512] = "data/sensitive_words.txt";

    char smtp_host[256] = "";
    int smtp_port = 587;
    char smtp_user[128] = "";
    char smtp_password[128] = "";
    char email_sender[256] = "";
    const char *email_recipients[EMAIL_ALERT_MAX_RECIPIENTS];
    int email_recipient_count = 0;
    int email_use_tls = 1;

    memset(email_recipients, 0, sizeof(email_recipients));

    static struct option long_opts[] = {
        {"server",   required_argument, 0, 's'},
        {"port",     required_argument, 0, 'p'},
        {"password", required_argument, 0, 'w'},
        {"listen",   required_argument, 0, 'l'},
        {"user",     required_argument, 0, 'u'},
        {"db",       required_argument, 0, 'd'},
        {"record",   required_argument, 0, 'r'},
        {"tls",      no_argument,       0, 't'},
        {"tls-validity",    required_argument, 0,  1000},
        {"tls-no-auto-renew", no_argument,    0,  1001},
        {"tls-cert", required_argument, 0, 1002},
        {"tls-key",  required_argument, 0, 1003},
        {"ocr",      no_argument,       0, 1004},
        {"ocr-interval",    required_argument, 0, 1005},
        {"ocr-lang",        required_argument, 0, 1006},
        {"ocr-no-block",    no_argument,       0, 1007},
        {"ocr-region",      required_argument, 0, 1008},
        {"sensitive-words", required_argument, 0, 1009},
        {"smtp-host",       required_argument, 0, 1010},
        {"smtp-port",       required_argument, 0, 1011},
        {"smtp-user",       required_argument, 0, 1012},
        {"smtp-password",   required_argument, 0, 1013},
        {"email-sender",    required_argument, 0, 1014},
        {"email-to",        required_argument, 0, 1015},
        {"email-no-tls",    no_argument,       0, 1016},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "s:p:w:l:u:d:r:th", long_opts, NULL)) != -1) {
        switch (opt) {
        case 's': strncpy(server_host, optarg, sizeof(server_host) - 1); break;
        case 'p': server_port = atoi(optarg); break;
        case 'w': strncpy(server_password, optarg, sizeof(server_password) - 1); break;
        case 'l': proxy_port = atoi(optarg); break;
        case 'u': strncpy(username, optarg, sizeof(username) - 1); break;
        case 'd': strncpy(db_path, optarg, sizeof(db_path) - 1); break;
        case 'r': strncpy(recordings_dir, optarg, sizeof(recordings_dir) - 1); break;
        case 't': use_tls = 1; break;
        case 1000: tls_validity_days = atoi(optarg); break;
        case 1001: tls_auto_renew = 0; break;
        case 1002: strncpy(tls_cert_path, optarg, sizeof(tls_cert_path) - 1); break;
        case 1003: strncpy(tls_key_path, optarg, sizeof(tls_key_path) - 1); break;
        case 1004: use_ocr = 1; break;
        case 1005: ocr_interval = atoi(optarg); break;
        case 1006: strncpy(ocr_lang, optarg, sizeof(ocr_lang) - 1); break;
        case 1007: ocr_block_on_hit = 0; break;
        case 1008: {
            int x, y, w, h;
            if (sscanf(optarg, "%d,%d,%d,%d", &x, &y, &w, &h) == 4) {
                ocr_region.x = x;
                ocr_region.y = y;
                ocr_region.width = w;
                ocr_region.height = h;
                ocr_region.enabled = true;
            } else {
                fprintf(stderr, "Warning: Invalid --ocr-region format, use X,Y,W,H\n");
            }
            break;
        }
        case 1009: strncpy(sensitive_words_file, optarg, sizeof(sensitive_words_file) - 1); break;
        case 1010: strncpy(smtp_host, optarg, sizeof(smtp_host) - 1); break;
        case 1011: smtp_port = atoi(optarg); break;
        case 1012: strncpy(smtp_user, optarg, sizeof(smtp_user) - 1); break;
        case 1013: strncpy(smtp_password, optarg, sizeof(smtp_password) - 1); break;
        case 1014: strncpy(email_sender, optarg, sizeof(email_sender) - 1); break;
        case 1015:
            if (email_recipient_count < EMAIL_ALERT_MAX_RECIPIENTS) {
                email_recipients[email_recipient_count++] = optarg;
            } else {
                fprintf(stderr, "Warning: Max email recipients (%d) reached\n", EMAIL_ALERT_MAX_RECIPIENTS);
            }
            break;
        case 1016: email_use_tls = 0; break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    fprintf(stderr, "========================================\n");
    fprintf(stderr, " VNC Watermark Audit Proxy\n");
    fprintf(stderr, "========================================\n");
    fprintf(stderr, " Target Server: %s:%d\n", server_host, server_port);
    fprintf(stderr, " Proxy Port:    %d\n", proxy_port);
    fprintf(stderr, " Username:      %s\n", username);
    fprintf(stderr, " Database:      %s\n", db_path);
    fprintf(stderr, " Recordings:    %s\n", recordings_dir);
    fprintf(stderr, " TLS/VeNCrypt:  %s\n", use_tls ? "enabled" : "disabled");
    if (use_tls) {
        fprintf(stderr, " TLS Validity:  %d days\n", tls_validity_days);
        fprintf(stderr, " TLS Auto-Renew: %s\n", tls_auto_renew ? "yes" : "no");
    }
    fprintf(stderr, " OCR:           %s\n", use_ocr ? "enabled" : "disabled");
    if (use_ocr) {
        fprintf(stderr, " OCR Interval:  %ds\n", ocr_interval);
        fprintf(stderr, " OCR Language:  %s\n", ocr_lang);
        fprintf(stderr, " OCR Block:     %s\n", ocr_block_on_hit ? "yes" : "no");
        fprintf(stderr, " OCR Region:    %s\n",
                ocr_region.enabled ? "limited" : "full screen");
        if (ocr_region.enabled) {
            fprintf(stderr, "                x=%d,y=%d,w=%d,h=%d\n",
                    ocr_region.x, ocr_region.y, ocr_region.width, ocr_region.height);
        }
        fprintf(stderr, " Sensitive:     %s\n", sensitive_words_file);
    }
    if (smtp_host[0] && email_recipient_count > 0) {
        fprintf(stderr, " SMTP:          %s:%d (%s TLS)\n",
                smtp_host, smtp_port, email_use_tls ? "with" : "no");
        fprintf(stderr, " Email From:    %s\n", email_sender[0] ? email_sender : smtp_user);
        fprintf(stderr, " Email To:      %d recipient(s)\n", email_recipient_count);
    }
    fprintf(stderr, "========================================\n");
    fprintf(stderr, " Signals:\n");
    fprintf(stderr, "   kill -HUP <pid>   # Hot-reload TLS cert + sensitive words\n");
    fprintf(stderr, "   kill -USR1 <pid>  # Hot-reload sensitive words\n");
    fprintf(stderr, "========================================\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGUSR1, signal_handler);

    if (!proxy_init(&g_ctx, server_host, server_port, server_password,
                    proxy_port, username, db_path, recordings_dir,
                    use_tls, tls_validity_days, tls_auto_renew,
                    use_ocr, ocr_interval, ocr_block_on_hit,
                    ocr_region, ocr_lang, sensitive_words_file,
                    smtp_host[0] ? smtp_host : NULL, smtp_port,
                    smtp_user[0] ? smtp_user : NULL,
                    smtp_password[0] ? smtp_password : NULL,
                    email_sender[0] ? email_sender : NULL,
                    email_recipients, email_recipient_count, email_use_tls)) {
        fprintf(stderr, "Failed to initialize proxy\n");
        return 1;
    }

    if (tls_cert_path[0] && tls_key_path[0] && use_tls) {
        fprintf(stderr, "[TLS] Using provided certificate: %s\n", tls_cert_path);
        if (!tls_load_or_create_cert(&g_ctx.tls, tls_cert_path, tls_key_path)) {
            fprintf(stderr, "[TLS] Warning: Failed to load provided cert, using auto-generated\n");
        }
    }

    fprintf(stderr, "Proxy started. Press Ctrl+C to stop.\n");

    proxy_start(&g_ctx);
    proxy_cleanup(&g_ctx);

    fprintf(stderr, "Proxy stopped.\n");
    return 0;
}
