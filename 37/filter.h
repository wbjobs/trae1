#ifndef FILTER_H
#define FILTER_H

#include "config.h"

typedef struct mysql_filter mysql_filter_t;

mysql_filter_t *filter_new(const char *sql_types_csv,
                           const char *user_substr,
                           const char *client_ip_prefix,
                           int case_sensitive);
void            filter_free(mysql_filter_t *f);

int filter_match(const mysql_filter_t *f, const mysql_event_t *ev);

#endif
