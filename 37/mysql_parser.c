#include "mysql_parser.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <inttypes.h>

#define MYSQL_PHASE_WAIT_HANDSHAKE     0
#define MYSQL_PHASE_WAIT_AUTH          1
#define MYSQL_PHASE_WAIT_CMD           2
#define MYSQL_PHASE_WAIT_RESP          3
#define MYSQL_PHASE_WAIT_PREPARE_RESP  4

#define COM_QUERY          0x03
#define COM_STMT_PREPARE   0x16
#define COM_STMT_EXECUTE   0x17
#define COM_STMT_SEND_LONG 0x18
#define COM_STMT_CLOSE     0x19
#define COM_STMT_RESET     0x1a
#define COM_INIT_DB        0x02
#define COM_QUIT           0x01
#define COM_PING           0x0e

#define PKT_OK   0x00
#define PKT_ERR  0xff
#define PKT_EOF  0xfe

#define MAX_STMT_CACHE 512
#define MAX_PARAMS 64
#define MAX_PARAM_VAL_LEN 1024

#define MYSQL_TYPE_DECIMAL    0
#define MYSQL_TYPE_TINY       1
#define MYSQL_TYPE_SHORT      2
#define MYSQL_TYPE_LONG       3
#define MYSQL_TYPE_FLOAT      4
#define MYSQL_TYPE_DOUBLE     5
#define MYSQL_TYPE_NULL       6
#define MYSQL_TYPE_TIMESTAMP  7
#define MYSQL_TYPE_LONGLONG   8
#define MYSQL_TYPE_INT24      9
#define MYSQL_TYPE_DATE       10
#define MYSQL_TYPE_TIME       11
#define MYSQL_TYPE_DATETIME   12
#define MYSQL_TYPE_YEAR       13
#define MYSQL_TYPE_NEWDATE    14
#define MYSQL_TYPE_VARCHAR    15
#define MYSQL_TYPE_BIT        16
#define MYSQL_TYPE_TIMESTAMP2 17
#define MYSQL_TYPE_DATETIME2  18
#define MYSQL_TYPE_TIME2      19
#define MYSQL_TYPE_NEWDECIMAL 246
#define MYSQL_TYPE_ENUM       247
#define MYSQL_TYPE_SET        248
#define MYSQL_TYPE_TINY_BLOB  249
#define MYSQL_TYPE_MEDIUM_BLOB 250
#define MYSQL_TYPE_LONG_BLOB  251
#define MYSQL_TYPE_BLOB       252
#define MYSQL_TYPE_VAR_STRING 253
#define MYSQL_TYPE_STRING     254
#define MYSQL_TYPE_GEOMETRY   255

typedef struct {
    uint32_t stmt_id;
    char     sql[MAX_SQL_LEN];
    uint16_t num_params;
    uint16_t num_columns;
    uint16_t param_types[MAX_PARAMS];
    uint8_t  param_unsigned[MAX_PARAMS];
    uint8_t  has_param_types;
} stmt_entry_t;

typedef struct sess {
    struct sess *next;
    void        *key;
    int          phase;
    uint8_t      pkt_seq_expected_c2s;
    uint8_t      pkt_seq_expected_s2c;
    int          auth_done;

    char         user[MAX_USER_LEN];
    char         database[MAX_USER_LEN];
    char         client_ip[64];
    uint16_t     client_port;

    char         pending_sql[MAX_SQL_LEN];
    uint64_t     pending_ts_sec;
    uint32_t     pending_ts_usec;
    sql_type_t   pending_sql_type;
    int          pending_active;

    char         pending_prepare_sql[MAX_SQL_LEN];
    int          prepare_pkt_count;
    int          prepare_eof_count;
    uint16_t     prepare_num_params;
    uint16_t     prepare_num_columns;

    stmt_entry_t stmts[MAX_STMT_CACHE];
    int          stmt_count;

    uint8_t     *buf;
    size_t       buf_len;
    size_t       buf_cap;

    uint8_t     *resp_buf;
    size_t       resp_len;
    size_t       resp_cap;
    int          resp_pkt_count;

    uint64_t     last_ts_sec;
    uint32_t     last_ts_usec;
} sess_t;

struct mysql_parser {
    sess_t      **buckets;
    size_t        n_buckets;
    mysql_event_cb_t cb;
    void         *user;
    size_t        n_sessions;
    size_t        max_sessions;
};

static size_t hash_ptr(void *p, size_t n) {
    uint64_t v = (uint64_t)(uintptr_t)p;
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdULL;
    v ^= v >> 33;
    v *= 0xc4ceb9fe1a85ec53ULL;
    v ^= v >> 33;
    return (size_t)(v % n);
}

const char *sql_type_name(sql_type_t t) {
    switch (t) {
        case SQL_TYPE_SELECT:   return "SELECT";
        case SQL_TYPE_INSERT:   return "INSERT";
        case SQL_TYPE_UPDATE:   return "UPDATE";
        case SQL_TYPE_DELETE:   return "DELETE";
        case SQL_TYPE_REPLACE:  return "REPLACE";
        case SQL_TYPE_CREATE:   return "CREATE";
        case SQL_TYPE_ALTER:    return "ALTER";
        case SQL_TYPE_DROP:     return "DROP";
        case SQL_TYPE_TRUNCATE: return "TRUNCATE";
        case SQL_TYPE_GRANT:    return "GRANT";
        case SQL_TYPE_SET:      return "SET";
        case SQL_TYPE_SHOW:     return "SHOW";
        case SQL_TYPE_USE:      return "USE";
        case SQL_TYPE_BEGIN:    return "BEGIN";
        case SQL_TYPE_COMMIT:   return "COMMIT";
        case SQL_TYPE_ROLLBACK: return "ROLLBACK";
        case SQL_TYPE_CALL:     return "CALL";
        case SQL_TYPE_PREPARE:  return "PREPARE";
        case SQL_TYPE_EXECUTE:  return "EXECUTE";
        default:                return "OTHER";
    }
}

static sql_type_t classify_sql(const char *sql) {
    if (!sql || !*sql) return SQL_TYPE_OTHER;
    while (*sql && isspace((unsigned char)*sql)) sql++;
    if (!*sql) return SQL_TYPE_OTHER;

    if (!strncasecmp(sql, "SELECT", 6) && (isspace((unsigned char)sql[6]) || sql[6] == '\0')) return SQL_TYPE_SELECT;
    if (!strncasecmp(sql, "INSERT", 6)) return SQL_TYPE_INSERT;
    if (!strncasecmp(sql, "UPDATE", 6)) return SQL_TYPE_UPDATE;
    if (!strncasecmp(sql, "DELETE", 6)) return SQL_TYPE_DELETE;
    if (!strncasecmp(sql, "REPLACE", 7)) return SQL_TYPE_REPLACE;
    if (!strncasecmp(sql, "CREATE", 6)) return SQL_TYPE_CREATE;
    if (!strncasecmp(sql, "ALTER", 5)) return SQL_TYPE_ALTER;
    if (!strncasecmp(sql, "DROP", 4)) return SQL_TYPE_DROP;
    if (!strncasecmp(sql, "TRUNCATE", 8)) return SQL_TYPE_TRUNCATE;
    if (!strncasecmp(sql, "GRANT", 5)) return SQL_TYPE_GRANT;
    if (!strncasecmp(sql, "SET", 3) && (isspace((unsigned char)sql[3]) || sql[3] == '\0')) return SQL_TYPE_SET;
    if (!strncasecmp(sql, "SHOW", 4)) return SQL_TYPE_SHOW;
    if (!strncasecmp(sql, "USE", 3) && (isspace((unsigned char)sql[3]) || sql[3] == '\0')) return SQL_TYPE_USE;
    if (!strncasecmp(sql, "BEGIN", 5) || !strncasecmp(sql, "START", 5)) return SQL_TYPE_BEGIN;
    if (!strncasecmp(sql, "COMMIT", 6)) return SQL_TYPE_COMMIT;
    if (!strncasecmp(sql, "ROLLBACK", 8)) return SQL_TYPE_ROLLBACK;
    if (!strncasecmp(sql, "CALL", 4)) return SQL_TYPE_CALL;
    if (!strncasecmp(sql, "PREPARE", 7)) return SQL_TYPE_PREPARE;
    if (!strncasecmp(sql, "EXECUTE", 7)) return SQL_TYPE_EXECUTE;

    return SQL_TYPE_OTHER;
}

static void format_ts(uint64_t sec, uint32_t usec, char *out, size_t sz) {
    time_t t = (time_t)sec;
    struct tm tm;
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    int n = strftime(out, sz, "%Y-%m-%dT%H:%M:%S", &tm);
    if (n > 0 && (size_t)n + 8 < sz) {
        snprintf(out + n, sz - n, ".%06uZ", usec);
    }
}

static void emit_event(mysql_parser_t *p, sess_t *s, const char *sql,
                       sql_type_t st, uint64_t ts_sec, uint32_t ts_usec,
                       double ms, uint64_t affected, uint64_t matched,
                       uint16_t warnings, int has_err,
                       uint16_t err_code, const char *err_msg) {
    mysql_event_t ev;
    memset(&ev, 0, sizeof(ev));
    format_ts(ts_sec, ts_usec, ev.timestamp, sizeof(ev.timestamp));
    snprintf(ev.client_ip, sizeof(ev.client_ip), "%s", s->client_ip);
    ev.client_port = s->client_port;
    snprintf(ev.user, sizeof(ev.user), "%s", s->user);
    snprintf(ev.database, sizeof(ev.database), "%s", s->database);
    ev.sql_type = st;
    snprintf(ev.sql, sizeof(ev.sql), "%s", sql ? sql : "");
    ev.execution_time_ms = ms;
    ev.affected_rows = affected;
    ev.matched_rows = matched;
    ev.warning_count = warnings;
    ev.has_error = (uint8_t)has_err;
    ev.error_code = err_code;
    if (err_msg) snprintf(ev.error_message, sizeof(ev.error_message), "%s", err_msg);
    if (p->cb) p->cb(&ev, p->user);
}

static sess_t *find_or_create_sess(mysql_parser_t *p, void *key) {
    size_t h = hash_ptr(key, p->n_buckets);
    for (sess_t *s = p->buckets[h]; s; s = s->next) {
        if (s->key == key) return s;
    }
    if (p->n_sessions >= p->max_sessions) {
        warn("mysql sessions exhausted");
        return NULL;
    }
    sess_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->key = key;
    s->phase = MYSQL_PHASE_WAIT_HANDSHAKE;
    s->pkt_seq_expected_c2s = 0;
    s->pkt_seq_expected_s2c = 0;
    s->next = p->buckets[h];
    p->buckets[h] = s;
    p->n_sessions++;
    return s;
}

static void sess_free(mysql_parser_t *p, sess_t *target) {
    size_t h = hash_ptr(target->key, p->n_buckets);
    sess_t **pp = &p->buckets[h];
    while (*pp && *pp != target) pp = &(*pp)->next;
    if (!*pp) return;
    *pp = target->next;
    free(target->buf);
    free(target->resp_buf);
    free(target);
    if (p->n_sessions > 0) p->n_sessions--;
}

static void buf_append(sess_t *s, const uint8_t *data, size_t len) {
    if (s->buf_len + len > s->buf_cap) {
        size_t nc = s->buf_cap ? s->buf_cap : 4096;
        while (nc < s->buf_len + len) nc *= 2;
        uint8_t *nb = realloc(s->buf, nc);
        if (!nb) return;
        s->buf = nb;
        s->buf_cap = nc;
    }
    memcpy(s->buf + s->buf_len, data, len);
    s->buf_len += len;
}

static void parse_ok_packet(const uint8_t *p, size_t len,
                            uint64_t *affected, uint64_t *matched,
                            uint16_t *warnings) {
    size_t pos = 1;
    size_t c;
    if (affected) *affected = ntoh_lenenc_int(p + pos, len - pos, &c);
    pos += c;
    if (matched) *matched = ntoh_lenenc_int(p + pos, len - pos, &c);
    pos += c;
    if (warnings) {
        if (pos + 2 <= len) {
            *warnings = (uint16_t)p[pos] | ((uint16_t)p[pos + 1] << 8);
        }
    }
}

static void parse_err_packet(const uint8_t *p, size_t len,
                             uint16_t *err_code, char *msg, size_t msg_sz) {
    if (err_code) {
        if (len >= 3) *err_code = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
    }
    if (msg && msg_sz > 0) {
        msg[0] = '\0';
        size_t off = 3;
        if (len > 3 && p[3] == '#') off = 9;
        if (off < len) {
            size_t n = len - off;
            if (n >= msg_sz) n = msg_sz - 1;
            memcpy(msg, p + off, n);
            msg[n] = '\0';
        }
    }
}

static stmt_entry_t *stmt_find(sess_t *s, uint32_t stmt_id) {
    for (int i = 0; i < s->stmt_count; i++) {
        if (s->stmts[i].stmt_id == stmt_id) return &s->stmts[i];
    }
    return NULL;
}

static void stmt_remove(sess_t *s, uint32_t stmt_id) {
    for (int i = 0; i < s->stmt_count; i++) {
        if (s->stmts[i].stmt_id == stmt_id) {
            if (i < s->stmt_count - 1) {
                memmove(&s->stmts[i], &s->stmts[i + 1],
                        (size_t)(s->stmt_count - 1 - i) * sizeof(stmt_entry_t));
            }
            s->stmt_count--;
            return;
        }
    }
}

static stmt_entry_t *stmt_add(sess_t *s, uint32_t stmt_id) {
    if (s->stmt_count >= MAX_STMT_CACHE) {
        memmove(&s->stmts[0], &s->stmts[1],
                (size_t)(MAX_STMT_CACHE - 1) * sizeof(stmt_entry_t));
        s->stmt_count = MAX_STMT_CACHE - 1;
    }
    stmt_entry_t *e = &s->stmts[s->stmt_count++];
    memset(e, 0, sizeof(*e));
    e->stmt_id = stmt_id;
    return e;
}

static int parse_binary_param(const uint8_t **pp, size_t *remain,
                              uint16_t type, uint8_t is_unsigned,
                              char *out, size_t out_size) {
    const uint8_t *p = *pp;
    size_t r = *remain;

    switch (type) {
        case MYSQL_TYPE_NULL:
            snprintf(out, out_size, "NULL");
            *pp = p;
            *remain = r;
            return 0;
        case MYSQL_TYPE_TINY:
            if (r < 1) return -1;
            if (is_unsigned)
                snprintf(out, out_size, "%u", (unsigned)p[0]);
            else
                snprintf(out, out_size, "%d", (int8_t)p[0]);
            *pp = p + 1;
            *remain = r - 1;
            return 0;
        case MYSQL_TYPE_SHORT:
            if (r < 2) return -1;
            {
                uint16_t v = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
                if (is_unsigned)
                    snprintf(out, out_size, "%u", (unsigned)v);
                else
                    snprintf(out, out_size, "%d", (int16_t)v);
            }
            *pp = p + 2;
            *remain = r - 2;
            return 0;
        case MYSQL_TYPE_INT24:
        case MYSQL_TYPE_LONG:
            if (r < 4) return -1;
            {
                uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                             ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                if (is_unsigned)
                    snprintf(out, out_size, "%u", (unsigned)v);
                else
                    snprintf(out, out_size, "%d", (int32_t)v);
            }
            *pp = p + 4;
            *remain = r - 4;
            return 0;
        case MYSQL_TYPE_LONGLONG:
            if (r < 8) return -1;
            {
                uint64_t v = 0;
                for (int i = 0; i < 8; i++) v |= ((uint64_t)p[i]) << (8 * i);
                if (is_unsigned)
                    snprintf(out, out_size, "%llu", (unsigned long long)v);
                else
                    snprintf(out, out_size, "%lld", (long long)v);
            }
            *pp = p + 8;
            *remain = r - 8;
            return 0;
        case MYSQL_TYPE_FLOAT:
            if (r < 4) return -1;
            {
                float f;
                memcpy(&f, p, 4);
                snprintf(out, out_size, "%g", (double)f);
            }
            *pp = p + 4;
            *remain = r - 4;
            return 0;
        case MYSQL_TYPE_DOUBLE:
            if (r < 8) return -1;
            {
                double d;
                memcpy(&d, p, 8);
                snprintf(out, out_size, "%g", d);
            }
            *pp = p + 8;
            *remain = r - 8;
            return 0;
        case MYSQL_TYPE_YEAR:
            if (r < 2) return -1;
            snprintf(out, out_size, "%u",
                     (unsigned)((uint16_t)p[0] | ((uint16_t)p[1] << 8)));
            *pp = p + 2;
            *remain = r - 2;
            return 0;
        case MYSQL_TYPE_DATE:
        case MYSQL_TYPE_TIME:
        case MYSQL_TYPE_DATETIME:
        case MYSQL_TYPE_TIMESTAMP:
        case MYSQL_TYPE_DATETIME2:
        case MYSQL_TYPE_TIMESTAMP2:
        case MYSQL_TYPE_TIME2:
            if (r < 1) return -1;
            {
                uint8_t len = p[0];
                if (r < 1 + len) return -1;
                const uint8_t *dp = p + 1;
                char tmp[128];
                int pos = 0;
                if (type == MYSQL_TYPE_DATE || type == MYSQL_TYPE_DATETIME ||
                    type == MYSQL_TYPE_TIMESTAMP || type == MYSQL_TYPE_DATETIME2 ||
                    type == MYSQL_TYPE_TIMESTAMP2) {
                    if (len >= 4) {
                        uint16_t year = (uint16_t)dp[0] | ((uint16_t)dp[1] << 8);
                        uint8_t month = dp[2], day = dp[3];
                        pos = snprintf(tmp, sizeof(tmp), "%04u-%02u-%02u",
                                       (unsigned)year, month, day);
                        if ((type == MYSQL_TYPE_DATETIME ||
                             type == MYSQL_TYPE_TIMESTAMP ||
                             type == MYSQL_TYPE_DATETIME2 ||
                             type == MYSQL_TYPE_TIMESTAMP2) && len >= 7) {
                            pos += snprintf(tmp + pos, sizeof(tmp) - pos,
                                            " %02u:%02u:%02u",
                                            dp[4], dp[5], dp[6]);
                            if (len >= 11) {
                                uint32_t micro = (uint32_t)dp[7] | ((uint32_t)dp[8] << 8) |
                                                 ((uint32_t)dp[9] << 16) | ((uint32_t)dp[10] << 24);
                                snprintf(tmp + pos, sizeof(tmp) - pos,
                                         ".%06u", (unsigned)micro);
                            }
                        }
                    }
                } else if (type == MYSQL_TYPE_TIME || type == MYSQL_TYPE_TIME2) {
                    if (len >= 8) {
                        uint8_t neg = dp[0];
                        uint32_t days = (uint32_t)dp[1] | ((uint32_t)dp[2] << 8) |
                                        ((uint32_t)dp[3] << 16) | ((uint32_t)dp[4] << 24);
                        uint8_t hour = dp[5], minute = dp[6], second = dp[7];
                        pos = snprintf(tmp, sizeof(tmp), "%s%u:%02u:%02u",
                                       neg ? "-" : "",
                                       days * 24 + hour, minute, second);
                        if (len >= 12) {
                            uint32_t micro = (uint32_t)dp[8] | ((uint32_t)dp[9] << 8) |
                                             ((uint32_t)dp[10] << 16) | ((uint32_t)dp[11] << 24);
                            snprintf(tmp + pos, sizeof(tmp) - pos,
                                     ".%06u", (unsigned)micro);
                        }
                    }
                }
                snprintf(out, out_size, "'%s'", tmp);
                *pp = p + 1 + len;
                *remain = r - 1 - len;
                return 0;
            }
        case MYSQL_TYPE_BIT:
        case MYSQL_TYPE_STRING:
        case MYSQL_TYPE_VAR_STRING:
        case MYSQL_TYPE_VARCHAR:
        case MYSQL_TYPE_ENUM:
        case MYSQL_TYPE_SET:
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_GEOMETRY:
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_NEWDECIMAL:
            {
                size_t c;
                uint64_t slen = ntoh_lenenc_int(p, r, &c);
                p += c;
                r -= c;
                if (slen > r) slen = (uint64_t)r;
                size_t write_len = (size_t)slen;
                if (write_len >= out_size - 3) write_len = out_size - 4;
                out[0] = '\'';
                size_t out_pos = 1;
                for (size_t i = 0; i < write_len; i++) {
                    char ch = (char)p[i];
                    if (ch == '\'') {
                        if (out_pos + 2 < out_size) {
                            out[out_pos++] = '\'';
                            out[out_pos++] = '\'';
                        }
                    } else if (ch == '\\') {
                        if (out_pos + 2 < out_size) {
                            out[out_pos++] = '\\';
                            out[out_pos++] = '\\';
                        }
                    } else if ((unsigned char)ch < 0x20) {
                        if (out_pos + 4 < out_size) {
                            out_pos += snprintf(out + out_pos, out_size - out_pos,
                                                "\\%03o", (unsigned char)ch);
                        }
                    } else {
                        if (out_pos + 1 < out_size) {
                            out[out_pos++] = ch;
                        }
                    }
                }
                if (out_pos + 1 < out_size) out[out_pos++] = '\'';
                out[out_pos] = '\0';
                *pp = p + (size_t)slen;
                *remain = r - (size_t)slen;
                return 0;
            }
        default:
            if (r >= 1) {
                size_t c;
                uint64_t slen = ntoh_lenenc_int(p, r, &c);
                if (slen > r - c) slen = (uint64_t)(r - c);
                size_t write_len = (size_t)slen;
                if (write_len > out_size - 3) write_len = out_size - 4;
                out[0] = '\'';
                memcpy(out + 1, p + c, write_len);
                out[1 + write_len] = '\'';
                out[2 + write_len] = '\0';
                *pp = p + c + (size_t)slen;
                *remain = r - c - (size_t)slen;
                return 0;
            }
            snprintf(out, out_size, "?");
            return 0;
    }
}

static int reconstruct_sql(stmt_entry_t *stmt, const uint8_t *data,
                           size_t data_len, uint8_t new_params_bound,
                           char *out, size_t out_size) {
    size_t null_bitmap_len = ((size_t)stmt->num_params + 7) / 8;
    size_t pos = 0;

    if (pos + null_bitmap_len > data_len) return -1;
    const uint8_t *null_bitmap = data;
    pos += null_bitmap_len;

    if (pos + 1 > data_len) return -1;
    pos += 1;

    const uint8_t *vp = data + pos;
    size_t vr = data_len - pos;

    uint16_t ptypes[MAX_PARAMS];
    uint8_t  punsigned[MAX_PARAMS];

    if (new_params_bound) {
        for (int i = 0; i < stmt->num_params && i < MAX_PARAMS; i++) {
            if (vr < 2) return -1;
            ptypes[i] = (uint16_t)vp[0] | ((uint16_t)vp[1] << 8);
            vp += 2;
            vr -= 2;
            punsigned[i] = (ptypes[i] & 0x8000) ? 1 : 0;
            ptypes[i] &= 0x7fff;
        }
        stmt->has_param_types = 1;
        for (int i = 0; i < stmt->num_params && i < MAX_PARAMS; i++) {
            stmt->param_types[i] = ptypes[i];
            stmt->param_unsigned[i] = punsigned[i];
        }
    } else if (stmt->has_param_types) {
        for (int i = 0; i < stmt->num_params && i < MAX_PARAMS; i++) {
            ptypes[i] = stmt->param_types[i];
            punsigned[i] = stmt->param_unsigned[i];
        }
    }

    out[0] = '\0';
    const char *tpl = stmt->sql;
    const char *src = tpl;
    size_t out_pos = 0;
    int param_idx = 0;

    while (*src && out_pos < out_size - 1) {
        if (*src == '?') {
            if (param_idx >= stmt->num_params ||
                param_idx >= MAX_PARAMS) {
                if (out_pos + 1 < out_size)
                    out[out_pos++] = '?';
                src++;
                param_idx++;
                continue;
            }
            int is_null = 0;
            if ((size_t)param_idx < null_bitmap_len * 8 &&
                (size_t)(param_idx / 8) < null_bitmap_len) {
                is_null = (null_bitmap[param_idx / 8] & (1 << (param_idx % 8))) ? 1 : 0;
            }
            if (is_null) {
                int n = snprintf(out + out_pos, out_size - out_pos, "NULL");
                if (n > 0) out_pos += (size_t)n;
            } else {
                uint16_t type = ptypes[param_idx];
                uint8_t is_unsigned = punsigned[param_idx];
                char val[MAX_PARAM_VAL_LEN];
                if (parse_binary_param(&vp, &vr, type, is_unsigned,
                                       val, sizeof(val)) == 0) {
                    size_t vlen = strlen(val);
                    if (out_pos + vlen < out_size) {
                        memcpy(out + out_pos, val, vlen);
                        out_pos += vlen;
                    }
                } else {
                    if (out_pos + 1 < out_size)
                        out[out_pos++] = '?';
                }
            }
            src++;
            param_idx++;
        } else {
            out[out_pos++] = *src++;
        }
    }
    if (out_pos >= out_size) out_pos = out_size - 1;
    out[out_pos] = '\0';
    return 0;
}

static void flush_request(mysql_parser_t *p, sess_t *s, uint8_t cmd,
                          const uint8_t *payload, size_t pay_len) {
    (void)p;
    if (cmd == COM_QUERY) {
        size_t sql_len = pay_len;
        char sql_buf[MAX_SQL_LEN];
        if (sql_len >= sizeof(sql_buf)) sql_len = sizeof(sql_buf) - 1;
        memcpy(sql_buf, payload, sql_len);
        sql_buf[sql_len] = '\0';
        trim(sql_buf);

        sql_type_t st = classify_sql(sql_buf);
        snprintf(s->pending_sql, sizeof(s->pending_sql), "%s", sql_buf);
        s->pending_ts_sec = s->last_ts_sec;
        s->pending_ts_usec = s->last_ts_usec;
        s->pending_sql_type = st;
        s->pending_active = 1;
        s->resp_len = 0;
        s->resp_pkt_count = 0;
    } else if (cmd == COM_STMT_PREPARE) {
        size_t sql_len = pay_len;
        if (sql_len >= sizeof(s->pending_prepare_sql))
            sql_len = sizeof(s->pending_prepare_sql) - 1;
        memcpy(s->pending_prepare_sql, payload, sql_len);
        s->pending_prepare_sql[sql_len] = '\0';
        trim(s->pending_prepare_sql);

        s->prepare_pkt_count = 0;
        s->prepare_eof_count = 0;
        s->prepare_num_params = 0;
        s->prepare_num_columns = 0;
        s->phase = MYSQL_PHASE_WAIT_PREPARE_RESP;
    } else if (cmd == COM_INIT_DB) {
        size_t n = pay_len;
        if (n >= sizeof(s->database)) n = sizeof(s->database) - 1;
        memcpy(s->database, payload, n);
        s->database[n] = '\0';
    } else if (cmd == COM_STMT_CLOSE) {
        if (pay_len >= 4) {
            uint32_t stmt_id = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                               ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
            stmt_remove(s, stmt_id);
        }
    } else if (cmd == COM_STMT_EXECUTE) {
        if (pay_len < 9) {
            snprintf(s->pending_sql, sizeof(s->pending_sql), "(execute stmt)");
            s->pending_sql_type = SQL_TYPE_EXECUTE;
            s->pending_ts_sec = s->last_ts_sec;
            s->pending_ts_usec = s->last_ts_usec;
            s->pending_active = 1;
            s->resp_len = 0;
            s->resp_pkt_count = 0;
            return;
        }
        uint32_t stmt_id = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                           ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);

        stmt_entry_t *stmt = stmt_find(s, stmt_id);
        if (!stmt) {
            snprintf(s->pending_sql, sizeof(s->pending_sql),
                     "(execute stmt_id=%u, unknown)", (unsigned)stmt_id);
            s->pending_sql_type = SQL_TYPE_EXECUTE;
            s->pending_ts_sec = s->last_ts_sec;
            s->pending_ts_usec = s->last_ts_usec;
            s->pending_active = 1;
            s->resp_len = 0;
            s->resp_pkt_count = 0;
            return;
        }

        size_t exec_offset = 4 + 1 + 4;
        uint8_t new_params_bound = 0;

        if (stmt->num_params > 0 && pay_len > exec_offset) {
            size_t null_bitmap_len = ((size_t)stmt->num_params + 7) / 8;
            if (pay_len > exec_offset + null_bitmap_len) {
                if (pay_len > exec_offset + null_bitmap_len + 1) {
                    new_params_bound = payload[exec_offset + null_bitmap_len];
                }
            }
        }

        char reconstructed[MAX_SQL_LEN];
        if (stmt->num_params > 0 && pay_len > exec_offset) {
            reconstruct_sql(stmt, payload + exec_offset,
                            (size_t)(pay_len - exec_offset),
                            new_params_bound,
                            reconstructed, sizeof(reconstructed));
        } else {
            snprintf(reconstructed, sizeof(reconstructed), "%s", stmt->sql);
        }

        snprintf(s->pending_sql, sizeof(s->pending_sql), "%s", reconstructed);
        s->pending_sql_type = classify_sql(reconstructed);
        s->pending_ts_sec = s->last_ts_sec;
        s->pending_ts_usec = s->last_ts_usec;
        s->pending_active = 1;
        s->resp_len = 0;
        s->resp_pkt_count = 0;
    }
}

static void parse_handshake_response(sess_t *s, const uint8_t *payload, size_t pay_len) {
    if (pay_len < 4 + 4 + 1 + 23) return;
    size_t off = 4 + 4 + 1 + 23;
    if (off >= pay_len) return;
    size_t ulen = strnlen((const char *)(payload + off), pay_len - off);
    if (ulen >= sizeof(s->user)) ulen = sizeof(s->user) - 1;
    memcpy(s->user, payload + off, ulen);
    s->user[ulen] = '\0';
    off += ulen + 1;
    if (off >= pay_len) return;

    uint32_t cap = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                   ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);

    if (cap & 0x00080000) {
        if (off + 1 > pay_len) return;
        size_t alen = payload[off];
        off += 1 + alen;
        if (off > pay_len) return;
    } else {
        size_t alen = strnlen((const char *)(payload + off), pay_len - off);
        off += alen + 1;
        if (off > pay_len) return;
    }

    if (cap & 0x00000008) {
        size_t dlen = strnlen((const char *)(payload + off), pay_len - off);
        if (dlen >= sizeof(s->database)) dlen = sizeof(s->database) - 1;
        memcpy(s->database, payload + off, dlen);
        s->database[dlen] = '\0';
    }
}

static void try_parse_packets(mysql_parser_t *p, sess_t *s, int dir) {
    if (dir == DIR_C2S) {
        while (s->buf_len >= 4) {
            uint32_t plen = ntoh3(s->buf);
            uint8_t pseq = s->buf[3];
            (void)pseq;
            if (plen > 16 * 1024 * 1024) {
                s->buf_len = 0;
                return;
            }
            if (s->buf_len < 4 + plen) return;
            const uint8_t *payload = s->buf + 4;
            if (s->phase == MYSQL_PHASE_WAIT_AUTH && plen > 0) {
                parse_handshake_response(s, payload, plen);
                s->auth_done = 1;
                s->phase = MYSQL_PHASE_WAIT_CMD;
            } else if ((s->phase == MYSQL_PHASE_WAIT_CMD ||
                        s->phase == MYSQL_PHASE_WAIT_PREPARE_RESP) && plen > 0) {
                uint8_t cmd = payload[0];
                if (cmd == COM_QUIT) {
                    s->phase = MYSQL_PHASE_WAIT_CMD;
                } else if (plen > 1) {
                    flush_request(p, s, cmd, payload + 1, plen - 1);
                    if (s->pending_active) {
                        s->phase = MYSQL_PHASE_WAIT_RESP;
                    }
                }
            }
            memmove(s->buf, s->buf + 4 + plen, s->buf_len - 4 - plen);
            s->buf_len -= 4 + plen;
        }
    } else {
        while (s->buf_len >= 4) {
            uint32_t plen = ntoh3(s->buf);
            uint8_t pseq = s->buf[3];
            (void)pseq;
            if (plen > 16 * 1024 * 1024) {
                s->buf_len = 0;
                return;
            }
            if (s->buf_len < 4 + plen) return;
            const uint8_t *payload = s->buf + 4;

            if (s->phase == MYSQL_PHASE_WAIT_HANDSHAKE) {
                s->phase = MYSQL_PHASE_WAIT_AUTH;
            } else if (s->phase == MYSQL_PHASE_WAIT_PREPARE_RESP) {
                s->prepare_pkt_count++;
                if (plen > 0 && payload[0] == PKT_ERR) {
                    uint16_t err_code = 0;
                    char msg[256] = {0};
                    parse_err_packet(payload, plen, &err_code, msg, sizeof(msg));
                    sql_type_t st = classify_sql(s->pending_prepare_sql);
                    if (st == SQL_TYPE_OTHER) st = SQL_TYPE_PREPARE;
                    emit_event(p, s, s->pending_prepare_sql, st,
                               s->last_ts_sec, s->last_ts_usec,
                               0, 0, 0, 0, 1, err_code, msg);
                    s->phase = MYSQL_PHASE_WAIT_CMD;
                    s->prepare_pkt_count = 0;
                } else if (s->prepare_pkt_count == 1 && plen >= 12 && payload[0] == PKT_OK) {
                    uint32_t stmt_id = (uint32_t)payload[1] |
                                       ((uint32_t)payload[2] << 8) |
                                       ((uint32_t)payload[3] << 16) |
                                       ((uint32_t)payload[4] << 24);
                    uint16_t num_cols = (uint16_t)payload[5] |
                                        ((uint16_t)payload[6] << 8);
                    uint16_t num_params = (uint16_t)payload[7] |
                                          ((uint16_t)payload[8] << 8);
                    s->prepare_num_params = num_params;
                    s->prepare_num_columns = num_cols;

                    stmt_entry_t *entry = stmt_add(s, stmt_id);
                    if (entry) {
                        snprintf(entry->sql, sizeof(entry->sql), "%s",
                                 s->pending_prepare_sql);
                        entry->num_params = num_params;
                        entry->num_columns = num_cols;
                    }

                    sql_type_t st = classify_sql(s->pending_prepare_sql);
                    if (st == SQL_TYPE_OTHER) st = SQL_TYPE_PREPARE;
                    emit_event(p, s, s->pending_prepare_sql, st,
                               s->last_ts_sec, s->last_ts_usec,
                               0, 0, 0, 0, 0, 0, NULL);

                    if (num_params == 0 && num_cols == 0) {
                        s->phase = MYSQL_PHASE_WAIT_CMD;
                        s->prepare_pkt_count = 0;
                    }
                } else if (payload[0] == PKT_EOF) {
                    s->prepare_eof_count++;
                    if (s->prepare_eof_count >= 2 ||
                        (s->prepare_num_params == 0 && s->prepare_eof_count >= 1 &&
                         s->prepare_num_columns == 0)) {
                        s->phase = MYSQL_PHASE_WAIT_CMD;
                        s->prepare_pkt_count = 0;
                        s->prepare_eof_count = 0;
                    }
                }

            } else if (s->phase == MYSQL_PHASE_WAIT_RESP && s->pending_active) {
                s->resp_pkt_count++;
                if (plen > 0 && payload[0] == PKT_ERR) {
                    uint16_t err_code = 0;
                    char msg[256] = {0};
                    parse_err_packet(payload, plen, &err_code, msg, sizeof(msg));
                    double ms = 0;
                    uint64_t ts = s->last_ts_sec;
                    uint32_t tus = s->last_ts_usec;
                    if (s->pending_ts_sec) {
                        int64_t dsec = (int64_t)ts - (int64_t)s->pending_ts_sec;
                        int32_t dus = (int32_t)tus - (int32_t)s->pending_ts_usec;
                        ms = dsec * 1000.0 + dus / 1000.0;
                    }
                    emit_event(p, s, s->pending_sql, s->pending_sql_type,
                               s->pending_ts_sec, s->pending_ts_usec,
                               ms, 0, 0, 0, 1, err_code, msg);
                    s->pending_active = 0;
                    s->phase = MYSQL_PHASE_WAIT_CMD;
                } else if (plen > 0 && payload[0] == PKT_OK) {
                    uint64_t affected = 0, matched = 0;
                    uint16_t warnings = 0;
                    parse_ok_packet(payload, plen, &affected, &matched, &warnings);
                    double ms = 0;
                    uint64_t ts = s->last_ts_sec;
                    uint32_t tus = s->last_ts_usec;
                    if (s->pending_ts_sec) {
                        int64_t dsec = (int64_t)ts - (int64_t)s->pending_ts_sec;
                        int32_t dus = (int32_t)tus - (int32_t)s->pending_ts_usec;
                        ms = dsec * 1000.0 + dus / 1000.0;
                        if (ms < 0) ms = 0;
                    }
                    emit_event(p, s, s->pending_sql, s->pending_sql_type,
                               s->pending_ts_sec, s->pending_ts_usec,
                               ms, affected, matched, warnings, 0, 0, NULL);
                    s->pending_active = 0;
                    s->phase = MYSQL_PHASE_WAIT_CMD;
                } else if (plen > 0 && payload[0] == 0xfb) {
                    double ms = 0;
                    uint64_t ts = s->last_ts_sec;
                    uint32_t tus = s->last_ts_usec;
                    if (s->pending_ts_sec) {
                        int64_t dsec = (int64_t)ts - (int64_t)s->pending_ts_sec;
                        int32_t dus = (int32_t)tus - (int32_t)s->pending_ts_usec;
                        ms = dsec * 1000.0 + dus / 1000.0;
                    }
                    emit_event(p, s, s->pending_sql, s->pending_sql_type,
                               s->pending_ts_sec, s->pending_ts_usec,
                               ms, 0, 0, 0, 0, 0, NULL);
                    s->pending_active = 0;
                    s->phase = MYSQL_PHASE_WAIT_CMD;
                } else if (plen > 0 && s->resp_pkt_count > 1 && payload[0] == PKT_EOF &&
                           plen >= 5) {
                    uint64_t affected = 0, matched = 0;
                    uint16_t warnings = 0;
                    if (plen >= 9) parse_ok_packet(payload, plen, &affected, &matched, &warnings);
                    double ms = 0;
                    uint64_t ts = s->last_ts_sec;
                    uint32_t tus = s->last_ts_usec;
                    if (s->pending_ts_sec) {
                        int64_t dsec = (int64_t)ts - (int64_t)s->pending_ts_sec;
                        int32_t dus = (int32_t)tus - (int32_t)s->pending_ts_usec;
                        ms = dsec * 1000.0 + dus / 1000.0;
                    }
                    emit_event(p, s, s->pending_sql, s->pending_sql_type,
                               s->pending_ts_sec, s->pending_ts_usec,
                               ms, affected, matched, warnings, 0, 0, NULL);
                    s->pending_active = 0;
                    s->phase = MYSQL_PHASE_WAIT_CMD;
                }
            }
            memmove(s->buf, s->buf + 4 + plen, s->buf_len - 4 - plen);
            s->buf_len -= 4 + plen;
        }
    }
}

void mysql_parser_feed(mysql_parser_t *p, void *session_key, int dir,
                       const uint8_t *data, size_t len,
                       uint64_t ts_sec, uint32_t ts_usec,
                       const char *client_ip, uint16_t client_port) {
    sess_t *s = find_or_create_sess(p, session_key);
    if (!s) return;
    if (client_ip) snprintf(s->client_ip, sizeof(s->client_ip), "%s", client_ip);
    if (client_port) s->client_port = client_port;
    s->last_ts_sec = ts_sec;
    s->last_ts_usec = ts_usec;
    buf_append(s, data, len);
    try_parse_packets(p, s, dir);
}

void mysql_parser_set_client(mysql_parser_t *p, void *session_key,
                             const char *client_ip, uint16_t client_port) {
    if (!p || !session_key) return;
    sess_t *s = find_or_create_sess(p, session_key);
    if (!s) return;
    if (client_ip) snprintf(s->client_ip, sizeof(s->client_ip), "%s", client_ip);
    if (client_port) s->client_port = client_port;
}

void mysql_parser_free(mysql_parser_t *p) {
    if (!p) return;
    for (size_t i = 0; i < p->n_buckets; i++) {
        sess_t *s = p->buckets[i];
        while (s) {
            sess_t *n = s->next;
            free(s->buf);
            free(s->resp_buf);
            free(s);
            s = n;
        }
    }
    free(p->buckets);
    free(p);
}

mysql_parser_t *mysql_parser_new(mysql_event_cb_t cb, void *user) {
    mysql_parser_t *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->n_buckets = 1024;
    p->max_sessions = 65536;
    p->buckets = calloc(p->n_buckets, sizeof(sess_t *));
    if (!p->buckets) { free(p); return NULL; }
    p->cb = cb;
    p->user = user;
    return p;
}
