#ifndef ALERT_H
#define ALERT_H

#include "config.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char     timestamp[64];
    char     client_ip[64];
    uint16_t client_port;
    char     user[64];
    char     database[64];
    char     sql[4096];
    double   execution_time_ms;
    uint64_t affected_rows;
} slow_query_entry_t;

typedef struct alert_ctx alert_ctx_t;

alert_ctx_t *alert_init(double threshold_ms,
                        const char *slack_webhook,
                        const char *dingtalk_webhook,
                        size_t ring_buffer_size);

void alert_free(alert_ctx_t *ctx);

void alert_check(alert_ctx_t *ctx, const mysql_event_t *ev);

size_t alert_get_recent(alert_ctx_t *ctx, slow_query_entry_t *buf, size_t max);

#endif
