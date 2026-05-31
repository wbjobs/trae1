#ifndef MYSQL_PARSER_H
#define MYSQL_PARSER_H

#include "config.h"

typedef struct mysql_parser mysql_parser_t;

typedef void (*mysql_event_cb_t)(const mysql_event_t *ev, void *user);

mysql_parser_t *mysql_parser_new(mysql_event_cb_t cb, void *user);
void            mysql_parser_free(mysql_parser_t *p);

void mysql_parser_feed(mysql_parser_t *p, void *session_key, int dir,
                       const uint8_t *data, size_t len,
                       uint64_t ts_sec, uint32_t ts_usec,
                       const char *client_ip, uint16_t client_port);

void mysql_parser_set_client(mysql_parser_t *p, void *session_key,
                             const char *client_ip, uint16_t client_port);

const char *sql_type_name(sql_type_t t);

#endif
