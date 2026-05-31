#include "alert.h"
#include "mysql_parser.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>

#ifndef _WIN32
#include <sys/time.h>
#endif

typedef struct {
    slow_query_entry_t *entries;
    size_t              capacity;
    size_t              head;
    size_t              count;
    pthread_mutex_t     lock;
} ring_buffer_t;

struct alert_ctx {
    double          threshold_ms;
    char            slack_webhook[2048];
    char            dingtalk_webhook[2048];
    ring_buffer_t  *ring;
    int             has_slack;
    int             has_dingtalk;
    uint64_t        alert_count;
    uint64_t        alert_failures;
    pthread_mutex_t lock;
};

static int parse_url(const char *url, char *host, size_t host_sz,
                     char *path, size_t path_sz, int *port) {
    if (strncmp(url, "https://", 8) == 0) {
        *port = 443;
        url += 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        *port = 80;
        url += 7;
    } else {
        return -1;
    }

    const char *slash = strchr(url, '/');
    if (!slash) {
        snprintf(host, host_sz, "%s", url);
        snprintf(path, path_sz, "/");
    } else {
        size_t hlen = (size_t)(slash - url);
        if (hlen >= host_sz) hlen = host_sz - 1;
        memcpy(host, url, hlen);
        host[hlen] = '\0';
        snprintf(path, path_sz, "%s", slash);
    }

    char *colon = strchr(host, ':');
    if (colon) {
        *port = atoi(colon + 1);
        *colon = '\0';
    }

    return 0;
}

static int connect_with_timeout(const char *host, int port, int timeout_sec) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        struct timeval tv;
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

static int http_post_json(const char *host, int port, const char *path,
                          const char *json_body, size_t body_len) {
    int fd = connect_with_timeout(host, port, 5);
    if (fd < 0) return -1;

    char header[2048];
    int hlen = snprintf(header, sizeof(header),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host, body_len);

    ssize_t n = send(fd, header, (size_t)hlen, 0);
    if (n <= 0) { close(fd); return -1; }

    size_t total = 0;
    while (total < body_len) {
        n = send(fd, json_body + total, body_len - total, 0);
        if (n <= 0) { close(fd); return -1; }
        total += (size_t)n;
    }

    char resp[1024];
    recv(fd, resp, sizeof(resp) - 1, 0);

    close(fd);
    return 0;
}

static void json_escape_str(const char *src, char *dst, size_t dst_sz) {
    size_t j = 0;
    dst[j++] = '"';
    for (size_t i = 0; src[i] && j + 8 < dst_sz; i++) {
        unsigned char c = (unsigned char)src[i];
        switch (c) {
            case '"':  if (j + 2 < dst_sz) { dst[j++] = '\\'; dst[j++] = '"'; } break;
            case '\\': if (j + 2 < dst_sz) { dst[j++] = '\\'; dst[j++] = '\\'; } break;
            case '\n': if (j + 2 < dst_sz) { dst[j++] = '\\'; dst[j++] = 'n'; } break;
            case '\r': if (j + 2 < dst_sz) { dst[j++] = '\\'; dst[j++] = 'r'; } break;
            case '\t': if (j + 2 < dst_sz) { dst[j++] = '\\'; dst[j++] = 't'; } break;
            default:
                if (c < 0x20) {
                    if (j + 7 < dst_sz) j += snprintf(dst + j, 8, "\\u%04x", c);
                } else {
                    dst[j++] = (char)c;
                }
        }
    }
    if (j < dst_sz) dst[j++] = '"';
    dst[j] = '\0';
}

static void build_alert_text(const slow_query_entry_t *e, char *buf, size_t sz) {
    snprintf(buf, sz,
        "[MySQL SLOW QUERY ALERT]\n"
        "Time: %s\n"
        "Client: %s:%u\n"
        "User: %s\n"
        "Database: %s\n"
        "Duration: %.3f ms\n"
        "Affected rows: %llu\n"
        "SQL: %s",
        e->timestamp,
        e->client_ip, e->client_port,
        e->user[0] ? e->user : "-",
        e->database[0] ? e->database : "-",
        e->execution_time_ms,
        (unsigned long long)e->affected_rows,
        e->sql);
}

static void build_slack_json(const slow_query_entry_t *e, char *buf, size_t sz) {
    char text[8192];
    build_alert_text(e, text, sizeof(text));

    char escaped[8192];
    json_escape_str(text, escaped, sizeof(escaped));

    snprintf(buf, sz, "{\"text\":%s}", escaped);
}

static void build_dingtalk_json(const slow_query_entry_t *e, char *buf, size_t sz) {
    char text[8192];
    build_alert_text(e, text, sizeof(text));

    char escaped[8192];
    json_escape_str(text, escaped, sizeof(escaped));

    snprintf(buf, sz, "{\"msgtype\":\"text\",\"text\":{\"content\":%s}}", escaped);
}

typedef struct {
    char host[256];
    char path[2048];
    int  port;
    char json[16384];
    size_t json_len;
    alert_ctx_t *ctx;
} post_data_t;

static void *post_thread(void *arg) {
    post_data_t *d = (post_data_t *)arg;
    int ret = http_post_json(d->host, d->port, d->path, d->json, d->json_len);
    if (ret != 0) {
        pthread_mutex_lock(&d->ctx->lock);
        d->ctx->alert_failures++;
        pthread_mutex_unlock(&d->ctx->lock);
    }
    free(d);
    return NULL;
}

static void send_alert_async(alert_ctx_t *ctx, const slow_query_entry_t *e) {
    char host[256], path[2048];
    int port;

    if (ctx->has_slack &&
        parse_url(ctx->slack_webhook, host, sizeof(host), path, sizeof(path), &port) == 0) {
        post_data_t *d = calloc(1, sizeof(*d));
        if (d) {
            snprintf(d->host, sizeof(d->host), "%s", host);
            snprintf(d->path, sizeof(d->path), "%s", path);
            d->port = port;
            d->ctx = ctx;
            build_slack_json(e, d->json, sizeof(d->json));
            d->json_len = strlen(d->json);

            pthread_t tid;
            if (pthread_create(&tid, NULL, post_thread, d) == 0) {
                pthread_detach(tid);
            } else {
                free(d);
            }
        }
    }

    if (ctx->has_dingtalk &&
        parse_url(ctx->dingtalk_webhook, host, sizeof(host), path, sizeof(path), &port) == 0) {
        post_data_t *d = calloc(1, sizeof(*d));
        if (d) {
            snprintf(d->host, sizeof(d->host), "%s", host);
            snprintf(d->path, sizeof(d->path), "%s", path);
            d->port = port;
            d->ctx = ctx;
            build_dingtalk_json(e, d->json, sizeof(d->json));
            d->json_len = strlen(d->json);

            pthread_t tid;
            if (pthread_create(&tid, NULL, post_thread, d) == 0) {
                pthread_detach(tid);
            } else {
                free(d);
            }
        }
    }
}

static ring_buffer_t *ring_create(size_t capacity) {
    ring_buffer_t *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->entries = calloc(capacity, sizeof(slow_query_entry_t));
    if (!r->entries) { free(r); return NULL; }
    r->capacity = capacity;
    r->head = 0;
    r->count = 0;
    pthread_mutex_init(&r->lock, NULL);
    return r;
}

static void ring_free(ring_buffer_t *r) {
    if (!r) return;
    pthread_mutex_destroy(&r->lock);
    free(r->entries);
    free(r);
}

static void ring_push(ring_buffer_t *r, const slow_query_entry_t *e) {
    pthread_mutex_lock(&r->lock);
    memcpy(&r->entries[r->head], e, sizeof(*e));
    r->head = (r->head + 1) % r->capacity;
    if (r->count < r->capacity) r->count++;
    pthread_mutex_unlock(&r->lock);
}

static size_t ring_snapshot(ring_buffer_t *r, slow_query_entry_t *buf, size_t max) {
    pthread_mutex_lock(&r->lock);
    size_t n = r->count < max ? r->count : max;
    for (size_t i = 0; i < n; i++) {
        size_t idx = (r->head + r->capacity - n + i) % r->capacity;
        memcpy(&buf[i], &r->entries[idx], sizeof(slow_query_entry_t));
    }
    pthread_mutex_unlock(&r->lock);
    return n;
}

alert_ctx_t *alert_init(double threshold_ms,
                        const char *slack_webhook,
                        const char *dingtalk_webhook,
                        size_t ring_buffer_size) {
    alert_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->threshold_ms = threshold_ms > 0 ? threshold_ms : 100.0;
    ctx->has_slack = slack_webhook && *slack_webhook ? 1 : 0;
    ctx->has_dingtalk = dingtalk_webhook && *dingtalk_webhook ? 1 : 0;
    if (slack_webhook) snprintf(ctx->slack_webhook, sizeof(ctx->slack_webhook), "%s", slack_webhook);
    if (dingtalk_webhook) snprintf(ctx->dingtalk_webhook, sizeof(ctx->dingtalk_webhook), "%s", dingtalk_webhook);

    if (ring_buffer_size == 0) ring_buffer_size = 100;
    ctx->ring = ring_create(ring_buffer_size);
    if (!ctx->ring) { free(ctx); return NULL; }

    pthread_mutex_init(&ctx->lock, NULL);
    return ctx;
}

void alert_free(alert_ctx_t *ctx) {
    if (!ctx) return;
    pthread_mutex_destroy(&ctx->lock);
    ring_free(ctx->ring);
    free(ctx);
}

void alert_check(alert_ctx_t *ctx, const mysql_event_t *ev) {
    if (!ctx || !ev) return;
    if (ev->execution_time_ms < ctx->threshold_ms) return;

    slow_query_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.timestamp, sizeof(entry.timestamp), "%s", ev->timestamp);
    snprintf(entry.client_ip, sizeof(entry.client_ip), "%s", ev->client_ip);
    entry.client_port = ev->client_port;
    snprintf(entry.user, sizeof(entry.user), "%s", ev->user);
    snprintf(entry.database, sizeof(entry.database), "%s", ev->database);
    snprintf(entry.sql, sizeof(entry.sql), "%s", ev->sql);
    entry.execution_time_ms = ev->execution_time_ms;
    entry.affected_rows = ev->affected_rows;

    ring_push(ctx->ring, &entry);

    pthread_mutex_lock(&ctx->lock);
    ctx->alert_count++;
    pthread_mutex_unlock(&ctx->lock);

    send_alert_async(ctx, &entry);
}

size_t alert_get_recent(alert_ctx_t *ctx, slow_query_entry_t *buf, size_t max) {
    if (!ctx || !buf || max == 0) return 0;
    return ring_snapshot(ctx->ring, buf, max);
}
