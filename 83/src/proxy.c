#include "proxy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

AuditLogger g_logger;

static void proxy_block_session_internal(ProxyContext *ctx, const char *reason);

static void proxy_kbd_callback(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
    ProxyContext *ctx = (ProxyContext *)cl->screen->screenData;
    if (!ctx || !ctx->client) return;

    pthread_mutex_lock(&ctx->block_mutex);
    if (ctx->blocked) {
        pthread_mutex_unlock(&ctx->block_mutex);
        return;
    }
    pthread_mutex_unlock(&ctx->block_mutex);

    logger_log_keyboard(&g_logger, (uint32_t)key, down ? true : false);
    SendKeyEvent(ctx->client, key, down ? TRUE : FALSE);
}

static void proxy_ptr_callback(int buttonMask, int x, int y, rfbClientPtr cl)
{
    ProxyContext *ctx = (ProxyContext *)cl->screen->screenData;
    if (!ctx || !ctx->client) return;

    pthread_mutex_lock(&ctx->block_mutex);
    if (ctx->blocked) {
        pthread_mutex_unlock(&ctx->block_mutex);
        return;
    }
    pthread_mutex_unlock(&ctx->block_mutex);

    logger_log_mouse(&g_logger, x, y, (uint8_t)buttonMask);
    SendPointerEvent(ctx->client, buttonMask, x, y);
}

static void proxy_client_connected(rfbClientPtr cl)
{
    ProxyContext *ctx = (ProxyContext *)cl->screen->screenData;
    if (!ctx) return;

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getpeername(cl->sock, (struct sockaddr *)&addr, &len) == 0) {
        inet_ntop(AF_INET, &addr.sin_addr, ctx->client_ip, sizeof(ctx->client_ip));
    }

    watermark_update_text(&ctx->watermark, ctx->client_ip, ctx->username);

    fprintf(stderr, "[Proxy] VNC client connected: %s\n", ctx->client_ip);
}

static void proxy_client_disconnected(rfbClientPtr cl)
{
    ProxyContext *ctx = (ProxyContext *)cl->screen->screenData;
    if (!ctx) return;

    fprintf(stderr, "[Proxy] VNC client disconnected: %s\n", ctx->client_ip);
}

static void proxy_ocr_hit_callback(OcrResult *result, void *user_data)
{
    ProxyContext *ctx = (ProxyContext *)user_data;
    if (!ctx || !ctx->ocr_block_on_hit) return;

    char reason[256];
    snprintf(reason, sizeof(reason),
             "检测到敏感内容: \"%s\"，会话已被阻断。请联系管理员。",
             result->matched_word);

    proxy_block_session_internal(ctx, reason);
}

static void proxy_block_session_internal(ProxyContext *ctx, const char *reason)
{
    pthread_mutex_lock(&ctx->block_mutex);
    if (ctx->blocked) {
        pthread_mutex_unlock(&ctx->block_mutex);
        return;
    }
    ctx->blocked = 1;
    pthread_mutex_unlock(&ctx->block_mutex);

    fprintf(stderr, "[Proxy] Session BLOCKED: %s\n", reason);

    if (ctx->server && ctx->server->clientHead) {
        rfbClientPtr cl = ctx->server->clientHead;
        if (cl && cl->sock >= 0) {
            size_t len = strlen(reason);
            uint8_t msg[8 + 1024];
            if (len > 1024) len = 1024;

            msg[0] = 3;
            msg[1] = 0;
            *((uint32_t *)(msg + 4)) = (uint32_t)len;
            memcpy(msg + 8, reason, len);

            rfbSendServerMessage(cl, msg, 8 + len);
            rfbClientSendString(cl, (char *)reason);

            for (int i = 0; i < 2; i++) {
                uint32_t text_len = (uint32_t)len;
                uint8_t cut_msg[4 + 4];
                cut_msg[0] = 6;
                cut_msg[1] = 0;
                cut_msg[2] = 0;
                cut_msg[3] = 0;
                cut_msg[4] = (text_len >> 24) & 0xFF;
                cut_msg[5] = (text_len >> 16) & 0xFF;
                cut_msg[6] = (text_len >> 8) & 0xFF;
                cut_msg[7] = text_len & 0xFF;

                if (rfbSendServerMessage(cl, cut_msg, 8) > 0) {
                    rfbSendServerMessage(cl, (const uint8_t *)reason, len);
                }
            }
        }
    }
}

void proxy_block_session(ProxyContext *ctx, const char *reason)
{
    if (!ctx) return;
    proxy_block_session_internal(ctx, reason);
}

static char *client_get_password(rfbClient *cl)
{
    ProxyContext *ctx = (ProxyContext *)rfbClientGetClientData(cl, NULL);
    if (!ctx) return NULL;
    return strdup(ctx->server_password);
}

static void client_got_framebuffer_update(rfbClient *cl, int x, int y, int w, int h)
{
    ProxyContext *ctx = (ProxyContext *)rfbClientGetClientData(cl, NULL);
    if (!ctx) return;

    pthread_mutex_lock(&ctx->fb_mutex);

    int bpp = ctx->depth / 8;
    if (bpp < 3) bpp = 3;

    uint8_t *src = cl->frameBuffer + (y * cl->width + x) * bpp;
    uint8_t *dst = ctx->framebuffer + (y * ctx->width + x) * bpp;

    for (int row = 0; row < h; row++) {
        memcpy(dst + row * ctx->width * bpp,
               src + row * cl->width * bpp,
               w * bpp);
    }

    watermark_render(&ctx->watermark, ctx->framebuffer, ctx->width, ctx->height, ctx->depth);
    watermark_maybe_move(&ctx->watermark);

    recorder_maybe_screenshot(&ctx->recorder, ctx->framebuffer,
                              ctx->width, ctx->height, ctx->depth);

    rfbMarkRectAsModified(ctx->server, x, y, x + w, y + h);

    pthread_mutex_unlock(&ctx->fb_mutex);
}

static void *ocr_monitor_thread_func(void *arg)
{
    ProxyContext *ctx = (ProxyContext *)arg;

    while (ctx->running) {
        sleep(1);

        if (!ctx->use_ocr || !ctx->ocr.initialized) continue;

        pthread_mutex_lock(&ctx->block_mutex);
        int is_blocked = ctx->blocked;
        pthread_mutex_unlock(&ctx->block_mutex);

        if (is_blocked) continue;

        int now = (int)time(NULL);
        if (now - ctx->ocr_last_scan < ctx->ocr_interval) continue;

        pthread_mutex_lock(&ctx->fb_mutex);
        if (ctx->framebuffer && ctx->width > 0 && ctx->height > 0) {
            sensitive_words_hot_reload(&ctx->sensitive_words);

            ocr_engine_queue_task(&ctx->ocr,
                                  ctx->framebuffer, ctx->width, ctx->height, ctx->depth,
                                  g_logger.session_id, ctx->client_ip, ctx->username);

            ctx->ocr_last_scan = now;
        }
        pthread_mutex_unlock(&ctx->fb_mutex);
    }

    return NULL;
}

static void *client_thread_func(void *arg)
{
    ProxyContext *ctx = (ProxyContext *)arg;

    ctx->client = rfbGetClient(8, 3, 4);
    if (!ctx->client) {
        fprintf(stderr, "Failed to create VNC client\n");
        return NULL;
    }

    ctx->client->GetPassword = client_get_password;
    ctx->client->GotFrameBufferUpdate = client_got_framebuffer_update;

    if (ctx->use_tls) {
        ctx->client->tlsClient = TRUE;
        fprintf(stderr, "[TLS] Upstream VNC connection will use TLS\n");
    }

    rfbClientSetClientData(ctx->client, NULL, ctx);

    strncpy(ctx->client->serverHost, ctx->server_host, 255);
    ctx->client->serverPort = ctx->server_port;

    int argc = 1;
    char *argv[] = {"vnc_proxy", NULL};

    if (!rfbInitClient(ctx->client, &argc, argv)) {
        fprintf(stderr, "Failed to connect to VNC server %s:%d\n",
                ctx->server_host, ctx->server_port);
        return NULL;
    }

    if (ctx->client->width > 0 && ctx->client->height > 0) {
        pthread_mutex_lock(&ctx->fb_mutex);
        if (ctx->client->width != ctx->width || ctx->client->height != ctx->height) {
            ctx->width = ctx->client->width;
            ctx->height = ctx->client->height;
        }
        pthread_mutex_unlock(&ctx->fb_mutex);
    }

    fprintf(stderr, "Connected to VNC server: %s:%d (%dx%d)\n",
            ctx->server_host, ctx->server_port, ctx->width, ctx->height);

    while (ctx->running && ctx->client->sock >= 0) {
        int ret = WaitForMessage(ctx->client, 1000);
        if (ret < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                fprintf(stderr, "VNC connection error\n");
                break;
            }
        } else if (ret > 0) {
            if (!HandleRFBServerMessage(ctx->client)) {
                fprintf(stderr, "VNC server message handling failed\n");
                break;
            }
        }
    }

    ctx->running = 0;
    return NULL;
}

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
                int email_recipient_count, int email_use_tls)
{
    memset(ctx, 0, sizeof(*ctx));

    strncpy(ctx->server_host, server_host, sizeof(ctx->server_host) - 1);
    ctx->server_port = server_port;
    if (server_password)
        strncpy(ctx->server_password, server_password, sizeof(ctx->server_password) - 1);
    ctx->proxy_port = proxy_port;
    if (username)
        strncpy(ctx->username, username, sizeof(ctx->username) - 1);
    ctx->use_tls = use_tls;
    ctx->tls_validity_days = tls_validity_days;
    ctx->tls_auto_renew = tls_auto_renew;
    ctx->use_ocr = use_ocr;
    ctx->ocr_interval = ocr_interval > 0 ? ocr_interval : 10;
    ctx->ocr_block_on_hit = ocr_block_on_hit;
    ctx->last_cert_check = 0;
    ctx->ocr_last_scan = 0;
    ctx->blocked = 0;

    if (!logger_init(&g_logger, db_path)) {
        fprintf(stderr, "Failed to initialize logger\n");
        return false;
    }

    if (use_tls) {
        if (!tls_init(&ctx->tls, true, tls_validity_days, tls_auto_renew)) {
            fprintf(stderr, "Failed to initialize TLS: %s\n", tls_get_last_error());
            return false;
        }
        fprintf(stderr, "[TLS] TLS support enabled\n");
        int days = tls_get_cert_days_remaining(&ctx->tls);
        if (days >= 0)
            fprintf(stderr, "[TLS] Certificate expires in %d days\n", days);
    }

    if (use_ocr) {
        if (!sensitive_words_init(&ctx->sensitive_words, sensitive_words_file)) {
            fprintf(stderr, "[OCR] Warning: Failed to initialize sensitive words list\n");
        }
        fprintf(stderr, "[OCR] Loaded %d sensitive words\n",
                sensitive_words_get_count(&ctx->sensitive_words));

        if (smtp_host && smtp_host[0] && email_recipient_count > 0) {
            email_alert_init(&ctx->email, smtp_host, smtp_port, smtp_user, smtp_password,
                             email_sender, email_recipients, email_recipient_count, email_use_tls);
        }

        if (!ocr_engine_init(&ctx->ocr, ocr_lang, &ctx->sensitive_words, &ctx->email, ctx->ocr_interval)) {
            fprintf(stderr, "[OCR] Failed to initialize OCR engine, continuing without OCR\n");
            ctx->use_ocr = 0;
        } else {
            if (ocr_region.enabled) {
                ocr_engine_set_region(&ctx->ocr, ocr_region);
            }
            ctx->ocr.on_hit_callback = proxy_ocr_hit_callback;
            ctx->ocr.callback_user_data = ctx;
        }
    }

    rfbClient *test_client = rfbGetClient(8, 3, 4);
    if (!test_client) {
        fprintf(stderr, "Failed to create test VNC client\n");
        return false;
    }
    test_client->GetPassword = client_get_password;
    rfbClientSetClientData(test_client, NULL, ctx);
    strncpy(test_client->serverHost, server_host, 255);
    test_client->serverPort = server_port;
    if (use_tls) {
        test_client->tlsClient = TRUE;
    }

    int argc = 1;
    char *argv[] = {"vnc_proxy", NULL};

    if (!rfbInitClient(test_client, &argc, argv)) {
        fprintf(stderr, "Failed to connect to VNC server for resolution detection\n");
        rfbClientCleanup(test_client);
        return false;
    }

    ctx->width = test_client->width;
    ctx->height = test_client->height;
    ctx->depth = 32;

    fprintf(stderr, "Detected VNC server resolution: %dx%d\n", ctx->width, ctx->height);

    rfbClientCleanup(test_client);

    if (ctx->width <= 0 || ctx->height <= 0) {
        ctx->width = 1024;
        ctx->height = 768;
    }

    ctx->framebuffer = (uint8_t *)calloc(ctx->width * ctx->height, 4);
    if (!ctx->framebuffer) {
        fprintf(stderr, "Failed to allocate framebuffer\n");
        return false;
    }

    pthread_mutex_init(&ctx->fb_mutex, NULL);
    pthread_mutex_init(&ctx->block_mutex, NULL);

    watermark_init(&ctx->watermark, ctx->width, ctx->height);

    ctx->server = rfbGetScreen(&argc, argv, ctx->width, ctx->height, 8, 3, 4);
    if (!ctx->server) {
        fprintf(stderr, "Failed to create VNC server screen\n");
        free(ctx->framebuffer);
        return false;
    }

    ctx->server->frameBuffer = ctx->framebuffer;
    ctx->server->port = ctx->proxy_port;
    ctx->server->kbdAddEvent = proxy_kbd_callback;
    ctx->server->ptrAddEvent = proxy_ptr_callback;
    ctx->server->screenData = ctx;
    ctx->server->alwaysShared = TRUE;
    ctx->server->deferUpdateTime = 5;
    ctx->server->sockets[0] = -1;

    if (use_tls) {
        ctx->server->sslCertFile = ctx->tls.cert_path;
        ctx->server->sslKeyFile = ctx->tls.key_path;
        ctx->server->veNCrypt = TRUE;
        ctx->server->tlsAnon = TRUE;
        fprintf(stderr, "[TLS] Proxy VNC server will use VeNCrypt/TLS\n");
        fprintf(stderr, "[TLS]   Cert: %s\n", ctx->tls.cert_path);
        fprintf(stderr, "[TLS]   Key:  %s\n", ctx->tls.key_path);
    }

    rfbInitServer(ctx->server);

    fprintf(stderr, "VNC proxy server listening on port %d\n", ctx->proxy_port);

    return true;
}

bool proxy_start(ProxyContext *ctx)
{
    ctx->running = 1;

    logger_start_session(&g_logger, ctx->client_ip, ctx->username,
                         ctx->server_host, ctx->server_port);

    watermark_update_text(&ctx->watermark, ctx->client_ip, ctx->username);

    recorder_init(&ctx->recorder, "recordings", g_logger.session_id, 5);

    if (ctx->use_ocr) {
        if (pthread_create(&ctx->ocr_monitor_thread, NULL, ocr_monitor_thread_func, ctx) != 0) {
            fprintf(stderr, "[OCR] Failed to create OCR monitor thread\n");
            ctx->use_ocr = 0;
        }
    }

    if (pthread_create(&ctx->client_thread, NULL, client_thread_func, ctx) != 0) {
        fprintf(stderr, "Failed to create client thread\n");
        ctx->running = 0;
        return false;
    }

    ctx->last_cert_check = (int)time(NULL);

    rfbRunEventLoop(ctx->server, -1, TRUE);

    ctx->running = 0;
    pthread_join(ctx->client_thread, NULL);

    if (ctx->use_ocr) {
        pthread_join(ctx->ocr_monitor_thread, NULL);
    }

    logger_end_session(&g_logger);
    recorder_close(&ctx->recorder);

    return true;
}

void proxy_hot_reload_cert(ProxyContext *ctx)
{
    if (!ctx->use_tls) return;

    pthread_mutex_lock(&ctx->fb_mutex);

    if (tls_hot_reload(&ctx->tls)) {
        if (ctx->server) {
            ctx->server->sslCertFile = ctx->tls.cert_path;
            ctx->server->sslKeyFile = ctx->tls.key_path;
        }
        fprintf(stderr, "[TLS] Certificate hot-reloaded successfully\n");
    } else {
        fprintf(stderr, "[TLS] Certificate hot-reload failed: %s\n", tls_get_last_error());
    }

    pthread_mutex_unlock(&ctx->fb_mutex);
}

void proxy_hot_reload_sensitive_words(ProxyContext *ctx)
{
    if (!ctx->use_ocr) return;

    if (sensitive_words_hot_reload(&ctx->sensitive_words)) {
        fprintf(stderr, "[SensitiveWords] Hot-reload completed, %d words loaded\n",
                sensitive_words_get_count(&ctx->sensitive_words));
    }
}

void proxy_cleanup(ProxyContext *ctx)
{
    if (ctx->client) {
        rfbClientCleanup(ctx->client);
        ctx->client = NULL;
    }
    if (ctx->server) {
        rfbScreenCleanup(ctx->server);
        ctx->server = NULL;
    }
    if (ctx->framebuffer) {
        free(ctx->framebuffer);
        ctx->framebuffer = NULL;
    }

    if (ctx->use_ocr) {
        ocr_engine_cleanup(&ctx->ocr);
        sensitive_words_cleanup(&ctx->sensitive_words);
        email_alert_cleanup(&ctx->email);
    }

    pthread_mutex_destroy(&ctx->fb_mutex);
    pthread_mutex_destroy(&ctx->block_mutex);

    if (ctx->use_tls) {
        tls_cleanup(&ctx->tls);
    }

    logger_close(&g_logger);
}
