#ifndef PROXY_H
#define PROXY_H

#include <rfb/rfb.h>
#include <rfb/rfbclient.h>
#include <pthread.h>
#include <stdbool.h>
#include "watermark.h"
#include "logger.h"
#include "recorder.h"
#include "tls.h"
#include "sensitive_words.h"
#include "ocr_engine.h"
#include "email_alert.h"

typedef struct {
    rfbScreenInfoPtr server;
    rfbClient *client;
    uint8_t *framebuffer;
    WatermarkState watermark;
    SessionRecorder recorder;
    TlsContext tls;
    SensitiveWords sensitive_words;
    OcrEngine ocr;
    EmailAlertConfig email;
    char client_ip[64];
    char username[256];
    char server_host[256];
    int server_port;
    char server_password[256];
    int width;
    int height;
    int depth;
    int proxy_port;
    int running;
    int use_tls;
    int tls_validity_days;
    int tls_auto_renew;
    int last_cert_check;
    int use_ocr;
    int ocr_interval;
    int ocr_block_on_hit;
    int ocr_last_scan;
    int blocked;
    pthread_t client_thread;
    pthread_t ocr_monitor_thread;
    pthread_mutex_t fb_mutex;
    pthread_mutex_t block_mutex;
} ProxyContext;

bool proxy_init(ProxyContext *ctx,
                const char *server_host, int server_port, const char *server_password,
                int proxy_port, const char *username,
                const char *db_path, const char *recordings_dir,
                int use_tls, int tls_validity_days, int tls_auto_renew,
                int use_ocr, int ocr_interval, int ocr_block_on_hit,
                OcrRegion ocr_region, const char *ocr_lang,
                const char *sensitive_words_file,
                const char *smtp_host, int smtp_port,
                const char *smtp_user, const char *smtp_password,
                const char *email_sender, const char *email_recipients[],
                int email_recipient_count, int email_use_tls);

void proxy_cleanup(ProxyContext *ctx);
bool proxy_start(ProxyContext *ctx);
void proxy_hot_reload_cert(ProxyContext *ctx);
void proxy_hot_reload_sensitive_words(ProxyContext *ctx);
void proxy_block_session(ProxyContext *ctx, const char *reason);

#endif
