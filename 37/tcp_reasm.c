#include "tcp_reasm.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08

typedef struct seg {
    struct seg *next;
    uint32_t    seq;
    uint32_t    end;
    size_t      len;
    uint8_t    *data;
    uint64_t    ts_sec;
    uint32_t    ts_usec;
} seg_t;

typedef struct dir_buf {
    seg_t    *segs;
    uint32_t  next_seq;
    int       has_seq;
    uint64_t  last_ts_sec;
    uint32_t  last_ts_usec;
} dir_buf_t;

struct tcp_session {
    struct tcp_session *next;
    uint32_t            ip_a;
    uint16_t            port_a;
    uint32_t            ip_b;
    uint16_t            port_b;
    dir_buf_t           dir[2];
    time_t              last_seen;
    int                 closed;
};

static uint32_t session_hash(uint32_t a, uint16_t pa, uint32_t b, uint16_t pb, size_t n) {
    uint64_t h = (uint64_t)a * 2654435761ULL;
    h ^= (uint64_t)pa * 2246822519ULL;
    h ^= (uint64_t)b * 3266489917ULL;
    h ^= (uint64_t)pb * 668265263ULL;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return (uint32_t)(h % n);
}

static tcp_session_t *session_find(tcp_reasm_t *r, uint32_t a, uint16_t pa,
                                   uint32_t b, uint16_t pb,
                                   tcp_dir_t *out_dir) {
    uint32_t h = session_hash(a, pa, b, pb, r->n_buckets);
    for (tcp_session_t *s = r->buckets[h]; s; s = s->next) {
        if (s->ip_a == a && s->port_a == pa && s->ip_b == b && s->port_b == pb) {
            if (out_dir) *out_dir = DIR_C2S;
            return s;
        }
        if (s->ip_a == b && s->port_a == pb && s->ip_b == a && s->port_b == pa) {
            if (out_dir) *out_dir = DIR_S2C;
            return s;
        }
    }
    return NULL;
}

static tcp_session_t *session_create(tcp_reasm_t *r, uint32_t a, uint16_t pa,
                                     uint32_t b, uint16_t pb, time_t now) {
    if (r->cur_sessions >= r->max_sessions) {
        warn("tcp sessions exhausted (max=%zu), dropping flow", r->max_sessions);
        return NULL;
    }
    tcp_session_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->ip_a = a; s->port_a = pa;
    s->ip_b = b; s->port_b = pb;
    s->last_seen = now;
    uint32_t h = session_hash(a, pa, b, pb, r->n_buckets);
    s->next = r->buckets[h];
    r->buckets[h] = s;
    r->cur_sessions++;
    return s;
}

static void segs_free(seg_t *head) {
    while (head) {
        seg_t *n = head->next;
        free(head->data);
        free(head);
        head = n;
    }
}

static void session_destroy(tcp_reasm_t *r, tcp_session_t *prev, tcp_session_t *s, uint32_t h) {
    if (prev) prev->next = s->next;
    else r->buckets[h] = s->next;
    segs_free(s->dir[0].segs);
    segs_free(s->dir[1].segs);
    free(s);
    if (r->cur_sessions > 0) r->cur_sessions--;
}

static void cleanup_expired(tcp_reasm_t *r, time_t now) {
    if (r->max_idle_sec <= 0) return;
    if (now - r->last_cleanup < 5) return;
    r->last_cleanup = now;
    for (size_t i = 0; i < r->n_buckets; i++) {
        tcp_session_t *prev = NULL, *s = r->buckets[i];
        while (s) {
            tcp_session_t *next = s->next;
            if (s->closed || (now - s->last_seen > r->max_idle_sec)) {
                session_destroy(r, prev, s, (uint32_t)i);
                s = next;
            } else {
                prev = s;
                s = next;
            }
        }
    }
}

tcp_reasm_t *tcp_reasm_new(size_t n_buckets, size_t max_sessions,
                           time_t max_idle_sec, stream_cb_t cb, void *user) {
    tcp_reasm_t *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    if (n_buckets == 0) n_buckets = 1024;
    r->n_buckets = n_buckets;
    r->max_sessions = max_sessions ? max_sessions : 65536;
    r->max_idle_sec = max_idle_sec ? max_idle_sec : 300;
    r->on_stream = cb;
    r->user = user;
    r->buckets = calloc(n_buckets, sizeof(tcp_session_t *));
    if (!r->buckets) { free(r); return NULL; }
    return r;
}

void tcp_reasm_free(tcp_reasm_t *r) {
    if (!r) return;
    for (size_t i = 0; i < r->n_buckets; i++) {
        tcp_session_t *s = r->buckets[i];
        while (s) {
            tcp_session_t *n = s->next;
            segs_free(s->dir[0].segs);
            segs_free(s->dir[1].segs);
            free(s);
            s = n;
        }
    }
    free(r->buckets);
    free(r);
}

static void dir_deliver_ready(tcp_reasm_t *r, tcp_session_t *s, tcp_dir_t dir) {
    dir_buf_t *db = &s->dir[dir];
    while (db->segs && db->segs->seq == db->next_seq) {
        seg_t *seg = db->segs;
        db->segs = seg->next;
        if (seg->len > 0) {
            r->on_stream(dir, seg->data, seg->len,
                         seg->ts_sec, seg->ts_usec, s, r->user);
        }
        db->next_seq = seg->end;
        db->last_ts_sec = seg->ts_sec;
        db->last_ts_usec = seg->ts_usec;
        free(seg->data);
        free(seg);
    }
}

static void dir_insert_seg(dir_buf_t *db, uint32_t seq, uint32_t end,
                           const uint8_t *data, size_t len,
                           uint64_t ts_sec, uint32_t ts_usec) {
    if (end <= db->next_seq) return;

    uint32_t trim = 0;
    if (seq < db->next_seq) {
        trim = db->next_seq - seq;
        if (trim >= len) return;
        seq = db->next_seq;
        data += trim;
        len -= trim;
    }

    seg_t **pp = &db->segs;
    while (*pp && (*pp)->seq < seq) pp = &(*pp)->next;

    if (*pp && (*pp)->seq == seq && (*pp)->end >= end) {
        return;
    }

    while (*pp && (*pp)->seq < end) {
        if ((*pp)->end <= end) {
            seg_t *rm = *pp;
            *pp = rm->next;
            free(rm->data);
            free(rm);
        } else {
            break;
        }
    }

    seg_t *ns = calloc(1, sizeof(*ns));
    if (!ns) return;
    ns->seq = seq;
    ns->end = end;
    ns->len = len;
    ns->data = malloc(len);
    if (!ns->data) { free(ns); return; }
    memcpy(ns->data, data, len);
    ns->ts_sec = ts_sec;
    ns->ts_usec = ts_usec;
    ns->next = *pp;
    *pp = ns;
}

void tcp_reasm_input(tcp_reasm_t *r,
                     uint32_t src_ip, uint16_t src_port,
                     uint32_t dst_ip, uint16_t dst_port,
                     uint32_t seq, uint32_t ack,
                     uint8_t  tcp_flags, uint16_t window,
                     const uint8_t *payload, size_t payload_len,
                     uint64_t ts_sec, uint32_t ts_usec) {
    (void)ack; (void)window;
    time_t now = (time_t)ts_sec;
    cleanup_expired(r, now);

    tcp_dir_t dir;
    tcp_session_t *s = session_find(r, src_ip, src_port, dst_ip, dst_port, &dir);
    if (!s) {
        s = session_create(r, src_ip, src_port, dst_ip, dst_port, now);
        if (!s) return;
        dir = DIR_C2S;
    }

    s->last_seen = now;

    dir_buf_t *db = &s->dir[dir];

    if (!db->has_seq) {
        if (tcp_flags & TCP_SYN) {
            db->next_seq = seq + 1;
            db->has_seq = 1;
            return;
        } else if (payload_len > 0) {
            db->next_seq = seq;
            db->has_seq = 1;
        }
    }

    if (tcp_flags & TCP_RST) {
        s->closed = 1;
    }

    if (payload_len > 0 && db->has_seq) {
        uint32_t end = seq + (uint32_t)payload_len;
        dir_insert_seg(db, seq, end, payload, payload_len, ts_sec, ts_usec);
        dir_deliver_ready(r, s, dir);
    }

    if (tcp_flags & TCP_FIN) {
        s->closed = 1;
    }
}

int tcp_reasm_get_client(void *session_key, tcp_dir_t dir,
                         char *ip_out, size_t ip_size, uint16_t *port_out) {
    if (!session_key) return -1;
    tcp_session_t *s = (tcp_session_t *)session_key;
    uint32_t ip;
    uint16_t port;
    if (dir == DIR_C2S) { ip = s->ip_a; port = s->port_a; }
    else { ip = s->ip_b; port = s->port_b; }
    if (port == MYSQL_PORT) {
        ip = (dir == DIR_C2S) ? s->ip_b : s->ip_a;
        port = (dir == DIR_C2S) ? s->port_b : s->port_a;
    }
    if (ip_out && ip_size > 0) {
        unsigned char *p = (unsigned char *)&ip;
        snprintf(ip_out, ip_size, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
    }
    if (port_out) *port_out = port;
    return 0;
}
