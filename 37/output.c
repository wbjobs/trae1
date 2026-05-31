#include "output.h"
#include "mysql_parser.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#ifndef _WIN32
#include <syslog.h>
#endif

struct output_ctx {
    output_format_t format;
    FILE           *fp;
    int             use_syslog;
    int             syslog_opened;
    int             table_header_printed;
    pthread_mutex_t lock;
    uint64_t        count;
};

static void json_escape(FILE *fp, const char *s) {
    fputc('"', fp);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  fputs("\\\"", fp); break;
            case '\\': fputs("\\\\", fp); break;
            case '\b': fputs("\\b", fp); break;
            case '\f': fputs("\\f", fp); break;
            case '\n': fputs("\\n", fp); break;
            case '\r': fputs("\\r", fp); break;
            case '\t': fputs("\\t", fp); break;
            default:
                if (c < 0x20) fprintf(fp, "\\u%04x", c);
                else fputc(c, fp);
        }
    }
    fputc('"', fp);
}

static void write_json(FILE *fp, const mysql_event_t *ev) {
    fputc('{', fp);
    fputs("\"timestamp\":", fp); json_escape(fp, ev->timestamp); fputs(",", fp);
    fputs("\"client_ip\":", fp); json_escape(fp, ev->client_ip); fputs(",", fp);
    fprintf(fp, "\"client_port\":%u,", ev->client_port);
    fputs("\"user\":", fp); json_escape(fp, ev->user); fputs(",", fp);
    fputs("\"database\":", fp); json_escape(fp, ev->database); fputs(",", fp);
    fprintf(fp, "\"sql_type\":\"%s\",", sql_type_name(ev->sql_type));
    fputs("\"sql\":", fp); json_escape(fp, ev->sql); fputs(",", fp);
    fprintf(fp, "\"execution_time_ms\":%.3f,", ev->execution_time_ms);
    fprintf(fp, "\"affected_rows\":%llu,", (unsigned long long)ev->affected_rows);
    fprintf(fp, "\"matched_rows\":%llu,", (unsigned long long)ev->matched_rows);
    fprintf(fp, "\"warning_count\":%u,", ev->warning_count);
    fprintf(fp, "\"has_error\":%u,", ev->has_error);
    fprintf(fp, "\"error_code\":%u,", ev->error_code);
    fputs("\"error_message\":", fp); json_escape(fp, ev->error_message);
    fputs("}\n", fp);
}

static void write_table_header(FILE *fp) {
    fprintf(fp, "%-27s %-16s %-6s %-16s %-10s %-8s %10s %10s %s\n",
            "TIME", "CLIENT", "PORT", "USER", "DATABASE", "TYPE",
            "TIME_MS", "AFFECTED", "SQL");
    for (int i = 0; i < 130; i++) fputc('-', fp);
    fputc('\n', fp);
}

static void write_table(FILE *fp, const mysql_event_t *ev) {
    char sql_short[128];
    size_t n = strlen(ev->sql);
    if (n > 100) {
        memcpy(sql_short, ev->sql, 100);
        sql_short[100] = '.';
        sql_short[101] = '.';
        sql_short[102] = '.';
        sql_short[103] = '\0';
    } else {
        snprintf(sql_short, sizeof(sql_short), "%s", ev->sql);
    }
    fprintf(fp, "%-27s %-16s %-6u %-16s %-10s %-8s %10.3f %10llu %s\n",
            ev->timestamp,
            ev->client_ip,
            ev->client_port,
            ev->user[0] ? ev->user : "-",
            ev->database[0] ? ev->database : "-",
            sql_type_name(ev->sql_type),
            ev->execution_time_ms,
            (unsigned long long)ev->affected_rows,
            sql_short);
}

static void write_syslog(const mysql_event_t *ev) {
#ifndef _WIN32
    char buf[2048];
    int n = snprintf(buf, sizeof(buf),
                     "mysql query client=%s:%u user=%s db=%s type=%s "
                     "time_ms=%.3f affected=%llu sql=%s",
                     ev->client_ip, ev->client_port,
                     ev->user[0] ? ev->user : "-",
                     ev->database[0] ? ev->database : "-",
                     sql_type_name(ev->sql_type),
                     ev->execution_time_ms,
                     (unsigned long long)ev->affected_rows,
                     ev->sql);
    if (n > 0 && (size_t)n >= sizeof(buf)) buf[sizeof(buf) - 1] = '\0';
    syslog(LOG_INFO, "%s", buf);
#else
    (void)ev;
#endif
}

output_ctx_t *output_new(output_format_t fmt, const char *path,
                         int use_syslog, const char *syslog_ident,
                         int syslog_facility) {
    output_ctx_t *o = calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->format = fmt;
    pthread_mutex_init(&o->lock, NULL);

    if (fmt == FMT_SYSLOG || use_syslog) {
        o->use_syslog = 1;
#ifndef _WIN32
        openlog(syslog_ident ? syslog_ident : "mysql-sniffer",
                LOG_PID | LOG_NDELAY,
                syslog_facility ? syslog_facility : LOG_LOCAL0);
        o->syslog_opened = 1;
#endif
    }

    if (fmt == FMT_TEXT || fmt == FMT_JSON) {
        if (path && *path) {
            o->fp = fopen(path, "a");
            if (!o->fp) {
                warn("cannot open output file %s, using stdout", path);
                o->fp = stdout;
            }
        } else {
            o->fp = stdout;
        }
        if (o->fp != stdout) {
            setvbuf(o->fp, NULL, _IOLBF, 0);
        }
    }

    return o;
}

void output_free(output_ctx_t *o) {
    if (!o) return;
    pthread_mutex_lock(&o->lock);
    if (o->fp && o->fp != stdout) fclose(o->fp);
    o->fp = NULL;
#ifndef _WIN32
    if (o->syslog_opened) closelog();
#endif
    pthread_mutex_unlock(&o->lock);
    pthread_mutex_destroy(&o->lock);
    free(o);
}

void output_write(output_ctx_t *o, const mysql_event_t *ev) {
    if (!o || !ev) return;
    pthread_mutex_lock(&o->lock);
    if (o->format == FMT_JSON && o->fp) {
        write_json(o->fp, ev);
    } else if (o->format == FMT_TEXT && o->fp) {
        if (!o->table_header_printed) {
            write_table_header(o->fp);
            o->table_header_printed = 1;
        }
        write_table(o->fp, ev);
    }
    if (o->use_syslog) {
        write_syslog(ev);
    }
    o->count++;
    pthread_mutex_unlock(&o->lock);
}

void output_flush(output_ctx_t *o) {
    if (!o) return;
    pthread_mutex_lock(&o->lock);
    if (o->fp) fflush(o->fp);
    pthread_mutex_unlock(&o->lock);
}
