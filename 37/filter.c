#include "filter.h"
#include "mysql_parser.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

struct mysql_filter {
    int       types[32];
    int       n_types;
    char      user[256];
    int       has_user;
    char      ip_prefix[64];
    int       has_ip;
    int       case_sensitive;
};

static sql_type_t parse_type(const char *name) {
    struct { const char *n; sql_type_t t; } table[] = {
        {"SELECT", SQL_TYPE_SELECT},
        {"INSERT", SQL_TYPE_INSERT},
        {"UPDATE", SQL_TYPE_UPDATE},
        {"DELETE", SQL_TYPE_DELETE},
        {"REPLACE", SQL_TYPE_REPLACE},
        {"CREATE", SQL_TYPE_CREATE},
        {"ALTER", SQL_TYPE_ALTER},
        {"DROP", SQL_TYPE_DROP},
        {"TRUNCATE", SQL_TYPE_TRUNCATE},
        {"GRANT", SQL_TYPE_GRANT},
        {"SET", SQL_TYPE_SET},
        {"SHOW", SQL_TYPE_SHOW},
        {"USE", SQL_TYPE_USE},
        {"BEGIN", SQL_TYPE_BEGIN},
        {"COMMIT", SQL_TYPE_COMMIT},
        {"ROLLBACK", SQL_TYPE_ROLLBACK},
        {"CALL", SQL_TYPE_CALL},
        {"PREPARE", SQL_TYPE_PREPARE},
        {"EXECUTE", SQL_TYPE_EXECUTE},
        {"OTHER", SQL_TYPE_OTHER},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (!strcasecmp(name, table[i].n)) return table[i].t;
    }
    return SQL_TYPE_UNKNOWN;
}

mysql_filter_t *filter_new(const char *sql_types_csv,
                           const char *user_substr,
                           const char *client_ip_prefix,
                           int case_sensitive) {
    mysql_filter_t *f = calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->case_sensitive = case_sensitive;

    if (sql_types_csv && *sql_types_csv) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s", sql_types_csv);
        char *save = NULL;
        char *tok = strtok_r(buf, ",", &save);
        while (tok && f->n_types < 32) {
            char *t = trim(tok);
            sql_type_t st = parse_type(t);
            if (st != SQL_TYPE_UNKNOWN) {
                f->types[f->n_types++] = (int)st;
            }
            tok = strtok_r(NULL, ",", &save);
        }
    }
    if (user_substr && *user_substr) {
        snprintf(f->user, sizeof(f->user), "%s", user_substr);
        f->has_user = 1;
    }
    if (client_ip_prefix && *client_ip_prefix) {
        snprintf(f->ip_prefix, sizeof(f->ip_prefix), "%s", client_ip_prefix);
        f->has_ip = 1;
    }
    return f;
}

void filter_free(mysql_filter_t *f) {
    free(f);
}

int filter_match(const mysql_filter_t *f, const mysql_event_t *ev) {
    if (!f) return 1;

    if (f->n_types > 0) {
        int found = 0;
        for (int i = 0; i < f->n_types; i++) {
            if (f->types[i] == (int)ev->sql_type) { found = 1; break; }
        }
        if (!found) return 0;
    }

    if (f->has_user) {
        if (f->case_sensitive) {
            if (!strstr(ev->user, f->user)) return 0;
        } else {
            if (!strcasestr(ev->user, f->user)) return 0;
        }
    }

    if (f->has_ip) {
        if (strncmp(ev->client_ip, f->ip_prefix, strlen(f->ip_prefix)) != 0)
            return 0;
    }

    return 1;
}
