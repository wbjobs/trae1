#include "config_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *skip_ws(const char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static int expect_char(const char **p, char c)
{
    const char *s = skip_ws(*p);
    if (*s != c) return -1;
    *p = s + 1;
    return 0;
}

static int parse_string(const char **p, char *out, size_t max)
{
    const char *s = skip_ws(*p);
    if (*s != '"') return -1;
    s++;
    size_t i = 0;
    while (*s && *s != '"') {
        if (*s == '\\' && s[1]) { s++; }
        if (i + 1 >= max) return -1;
        out[i++] = *s++;
    }
    if (*s != '"') return -1;
    out[i] = '\0';
    *p = s + 1;
    return 0;
}

static int parse_int(const char **p, long long *out)
{
    const char *s = skip_ws(*p);
    char *end;
    long long v = strtoll(s, &end, 0);
    if (end == s) return -1;
    *out = v;
    *p = end;
    return 0;
}

static int parse_bool(const char **p, int *out)
{
    const char *s = skip_ws(*p);
    if (strncmp(s, "true", 4) == 0) { *out = 1; *p = s + 4; return 0; }
    if (strncmp(s, "false", 5) == 0) { *out = 0; *p = s + 5; return 0; }
    return -1;
}

static int parse_value(const char **p, char *out_str, size_t max,
                       long long *out_int, int *out_bool, int *type);

static int skip_value(const char **p);

static int parse_array(const char **p, char out[][NVME_MAX_STR], size_t max, size_t *count)
{
    const char *s = *p;
    if (expect_char(&s, '[') != 0) return -1;
    *count = 0;
    s = skip_ws(s);
    if (*s == ']') { *p = s + 1; return 0; }
    while (1) {
        if (*count >= max) return -1;
        if (parse_string(&s, out[*count], NVME_MAX_STR) != 0) return -1;
        (*count)++;
        s = skip_ws(s);
        if (*s == ',') { s++; continue; }
        if (*s == ']') { *p = s + 1; return 0; }
        return -1;
    }
}

static int parse_ns_listener(const char **p, struct nvmeof_listener_cfg *l)
{
    const char *s = *p;
    if (expect_char(&s, '{') != 0) return -1;
    memset(l, 0, sizeof(*l));
    s = skip_ws(s);
    while (*s && *s != '}') {
        char key[64];
        if (parse_string(&s, key, sizeof(key)) != 0) return -1;
        s = skip_ws(s);
        if (expect_char(&s, ':') != 0) return -1;
        if (strcmp(key, "trtype") == 0) parse_string(&s, l->trtype, sizeof(l->trtype));
        else if (strcmp(key, "traddr") == 0) parse_string(&s, l->traddr, sizeof(l->traddr));
        else if (strcmp(key, "trsvcid") == 0) parse_string(&s, l->trsvcid, sizeof(l->trsvcid));
        else if (strcmp(key, "adrfam") == 0) { long long v; parse_int(&s, &v); l->adrfam = (int)v; }
        else skip_value(&s);
        s = skip_ws(s);
        if (*s == ',') { s++; s = skip_ws(s); }
    }
    if (*s != '}') return -1;
    *p = s + 1;
    return 0;
}

static int parse_ns(const char **p, struct nvmeof_ns_cfg *n)
{
    const char *s = *p;
    if (expect_char(&s, '{') != 0) return -1;
    memset(n, 0, sizeof(*n));
    n->nsid = 0;
    s = skip_ws(s);
    while (*s && *s != '}') {
        char key[64];
        if (parse_string(&s, key, sizeof(key)) != 0) return -1;
        s = skip_ws(s);
        if (expect_char(&s, ':') != 0) return -1;
        if (strcmp(key, "bdev_name") == 0) parse_string(&s, n->bdev_name, sizeof(n->bdev_name));
        else if (strcmp(key, "nsid") == 0) { long long v; if (parse_int(&s, &v) == 0) n->nsid = (uint32_t)v; }
        else if (strcmp(key, "nguid") == 0) parse_string(&s, n->nguid, sizeof(n->nguid));
        else if (strcmp(key, "eui64") == 0) parse_string(&s, n->eui64, sizeof(n->eui64));
        else if (strcmp(key, "uuid") == 0) parse_string(&s, n->uuid, sizeof(n->uuid));
        else if (strcmp(key, "enable_barrier") == 0) { int v; if (parse_bool(&s, &v) == 0) n->enable_barrier = (bool)v; }
        else if (strcmp(key, "enable_replication") == 0) { int v; if (parse_bool(&s, &v) == 0) n->enable_replication = (bool)v; }
        else skip_value(&s);
        s = skip_ws(s);
        if (*s == ',') { s++; s = skip_ws(s); }
    }
    if (*s != '}') return -1;
    *p = s + 1;
    return 0;
}

static int parse_repl_backup(const char **p, struct nvmeof_repl_backup_cfg *b)
{
    const char *s = *p;
    if (expect_char(&s, '{') != 0) return -1;
    memset(b, 0, sizeof(*b));
    s = skip_ws(s);
    while (*s && *s != '}') {
        char key[64];
        if (parse_string(&s, key, sizeof(key)) != 0) return -1;
        s = skip_ws(s);
        if (expect_char(&s, ':') != 0) return -1;
        if (strcmp(key, "traddr") == 0) parse_string(&s, b->traddr, sizeof(b->traddr));
        else if (strcmp(key, "trsvcid") == 0) parse_string(&s, b->trsvcid, sizeof(b->trsvcid));
        else if (strcmp(key, "nqn") == 0) parse_string(&s, b->nqn, sizeof(b->nqn));
        else if (strcmp(key, "remote_nsid") == 0) { long long v; if (parse_int(&s, &v) == 0) b->remote_nsid = (uint32_t)v; }
        else skip_value(&s);
        s = skip_ws(s);
        if (*s == ',') { s++; s = skip_ws(s); }
    }
    if (*s != '}') return -1;
    *p = s + 1;
    return 0;
}

static int parse_array_objects(const char **p, void *out, size_t elem_sz, size_t max, size_t *count,
                               int (*parse_one)(const char **, void *))
{
    const char *s = *p;
    if (expect_char(&s, '[') != 0) return -1;
    *count = 0;
    s = skip_ws(s);
    if (*s == ']') { *p = s + 1; return 0; }
    while (1) {
        if (*count >= max) return -1;
        void *e = (char *)out + (*count) * elem_sz;
        if (parse_one(&s, e) != 0) return -1;
        (*count)++;
        s = skip_ws(s);
        if (*s == ',') { s++; s = skip_ws(s); continue; }
        if (*s == ']') { *p = s + 1; return 0; }
        return -1;
    }
}

static int parse_subsys(const char **p, struct nvmeof_subsys_cfg *sub)
{
    const char *s = *p;
    if (expect_char(&s, '{') != 0) return -1;
    memset(sub, 0, sizeof(*sub));
    sub->allow_any_host = true;
    s = skip_ws(s);
    while (*s && *s != '}') {
        char key[64];
        if (parse_string(&s, key, sizeof(key)) != 0) return -1;
        s = skip_ws(s);
        if (expect_char(&s, ':') != 0) return -1;
        if (strcmp(key, "nqn") == 0) parse_string(&s, sub->nqn, sizeof(sub->nqn));
        else if (strcmp(key, "allow_any_host") == 0) { int v; parse_bool(&s, &v); sub->allow_any_host = (bool)v; }
        else if (strcmp(key, "hosts") == 0)
            parse_array(&s, sub->hosts, NVME_MAX_HOSTS, &sub->host_count);
        else if (strcmp(key, "namespaces") == 0)
            parse_array_objects(&s, sub->namespaces, sizeof(sub->namespaces[0]),
                                NVME_MAX_NAMESPACES, &sub->ns_count,
                                (int (*)(const char **, void *))parse_ns);
        else if (strcmp(key, "listeners") == 0)
            parse_array_objects(&s, sub->listeners, sizeof(sub->listeners[0]),
                                NVME_MAX_LISTENERS, &sub->listener_count,
                                (int (*)(const char **, void *))parse_ns_listener);
        else if (strcmp(key, "replication") == 0) {
            const char *ss = skip_ws(s);
            if (*ss == '{') {
                ss++;
                sub->replication.enabled = true;
                while (*ss && *ss != '}') {
                    char k2[64];
                    if (parse_string(&ss, k2, sizeof(k2)) != 0) break;
                    ss = skip_ws(ss);
                    if (expect_char(&ss, ':') != 0) break;
                    if (strcmp(k2, "role") == 0) {
                        char val[32];
                        if (parse_string(&ss, val, sizeof(val)) == 0) {
                            if (strcmp(val, "backup") == 0)
                                sub->replication.role = NVME_REPL_ROLE_BACKUP_CFG;
                            else
                                sub->replication.role = NVME_REPL_ROLE_PRIMARY_CFG;
                        }
                    } else if (strcmp(k2, "backups") == 0) {
                        parse_array_objects(&ss, sub->replication.backups,
                                            sizeof(sub->replication.backups[0]),
                                            NVME_REPL_MAX_BACKUPS,
                                            &sub->replication.backup_count,
                                            (int (*)(const char **, void *))parse_repl_backup);
                    } else skip_value(&ss);
                    ss = skip_ws(ss);
                    if (*ss == ',') { ss++; ss = skip_ws(ss); }
                }
                s = ss;
            } else skip_value(&s);
        }
        else skip_value(&s);
        s = skip_ws(s);
        if (*s == ',') { s++; s = skip_ws(s); }
    }
    if (*s != '}') return -1;
    *p = s + 1;
    return 0;
}

static int parse_bdev(const char **p, struct nvmeof_bdev_cfg *b)
{
    const char *s = *p;
    if (expect_char(&s, '{') != 0) return -1;
    memset(b, 0, sizeof(*b));
    s = skip_ws(s);
    while (*s && *s != '}') {
        char key[64];
        if (parse_string(&s, key, sizeof(key)) != 0) return -1;
        s = skip_ws(s);
        if (expect_char(&s, ':') != 0) return -1;
        if (strcmp(key, "name") == 0) parse_string(&s, b->name, sizeof(b->name));
        else if (strcmp(key, "trtype") == 0) parse_string(&s, b->trtype, sizeof(b->trtype));
        else if (strcmp(key, "traddr") == 0) parse_string(&s, b->traddr, sizeof(b->traddr));
        else skip_value(&s);
        s = skip_ws(s);
        if (*s == ',') { s++; s = skip_ws(s); }
    }
    if (*s != '}') return -1;
    *p = s + 1;
    return 0;
}

static int skip_value(const char **p)
{
    const char *s = skip_ws(*p);
    if (*s == '"') { char buf[1]; return parse_string(&s, buf, sizeof(buf)); }
    if (*s == '{') {
        int depth = 1; s++;
        while (depth > 0 && *s) {
            if (*s == '{') depth++;
            else if (*s == '}') depth--;
            else if (*s == '"') { s++; while (*s && *s != '"') { if (*s == '\\' && s[1]) s++; s++; } }
            s++;
        }
        *p = s; return 0;
    }
    if (*s == '[') {
        int depth = 1; s++;
        while (depth > 0 && *s) {
            if (*s == '[') depth++;
            else if (*s == ']') depth--;
            else if (*s == '"') { s++; while (*s && *s != '"') { if (*s == '\\' && s[1]) s++; s++; } }
            s++;
        }
        *p = s; return 0;
    }
    while (*s && *s != ',' && *s != '}' && *s != ']') s++;
    *p = s; return 0;
}

static int parse_value(const char **p, char *out_str, size_t max,
                       long long *out_int, int *out_bool, int *type)
{
    const char *s = skip_ws(*p);
    if (*s == '"') { *type = 0; return parse_string(&s, out_str, max); }
    if (*s == 't' || *s == 'f') { *type = 2; int b; int rc = parse_bool(&s, &b); if (rc == 0) *out_bool = b; *p = s; return rc; }
    if (isdigit((unsigned char)*s) || *s == '-') { *type = 1; long long v; int rc = parse_int(&s, &v); if (rc == 0) *out_int = v; *p = s; return rc; }
    return -1;
}

void nvmeof_config_defaults(struct nvmeof_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->rpc_socket, sizeof(cfg->rpc_socket), "/var/tmp/nvmeof.sock");
    cfg->hugepage_size_mb = 2048;
    snprintf(cfg->core_mask, sizeof(cfg->core_mask), "0x1");
    snprintf(cfg->log_level, sizeof(cfg->log_level), "NOTICE");
}

int nvmeof_config_load(const char *path, struct nvmeof_config *cfg)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, sz, f) != (size_t)sz) { free(buf); fclose(f); return -1; }
    buf[sz] = '\0';
    fclose(f);

    nvmeof_config_defaults(cfg);

    const char *p = buf;
    p = skip_ws(p);
    if (*p != '{') { free(buf); return -1; }
    p++;
    while (*p && *p != '}') {
        p = skip_ws(p);
        if (*p == '}') break;
        char key[64];
        if (parse_string(&p, key, sizeof(key)) != 0) { free(buf); return -1; }
        p = skip_ws(p);
        if (expect_char(&p, ':') != 0) { free(buf); return -1; }
        if (strcmp(key, "rpc_socket") == 0) parse_string(&p, cfg->rpc_socket, sizeof(cfg->rpc_socket));
        else if (strcmp(key, "hugepage_size_mb") == 0) { long long v; if (parse_int(&p, &v) == 0) cfg->hugepage_size_mb = (uint32_t)v; }
        else if (strcmp(key, "core_mask") == 0) parse_string(&p, cfg->core_mask, sizeof(cfg->core_mask));
        else if (strcmp(key, "log_level") == 0) parse_string(&p, cfg->log_level, sizeof(cfg->log_level));
        else if (strcmp(key, "nvme_bdevs") == 0)
            parse_array_objects(&p, cfg->bdevs, sizeof(cfg->bdevs[0]),
                                NVME_MAX_BDEVS, &cfg->bdev_count,
                                (int (*)(const char **, void *))parse_bdev);
        else if (strcmp(key, "subsystems") == 0)
            parse_array_objects(&p, cfg->subsystems, sizeof(cfg->subsystems[0]),
                                NVME_MAX_SUBSYSTEMS, &cfg->subsys_count,
                                (int (*)(const char **, void *))parse_subsys);
        else skip_value(&p);
        p = skip_ws(p);
        if (*p == ',') { p++; }
    }
    free(buf);
    return 0;
}
