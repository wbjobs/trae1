#ifndef OUTPUT_H
#define OUTPUT_H

#include "config.h"

typedef struct output_ctx output_ctx_t;

output_ctx_t *output_new(output_format_t fmt, const char *path,
                         int use_syslog, const char *syslog_ident,
                         int syslog_facility);
void          output_free(output_ctx_t *o);

void output_write(output_ctx_t *o, const mysql_event_t *ev);
void output_flush(output_ctx_t *o);

#endif
