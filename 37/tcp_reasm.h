#ifndef TCP_REASM_H
#define TCP_REASM_H

#include "config.h"
#include <stdint.h>
#include <stddef.h>

typedef enum {
    DIR_C2S = 0,
    DIR_S2C = 1
} tcp_dir_t;

typedef void (*stream_cb_t)(tcp_dir_t dir,
                            const uint8_t *data, size_t len,
                            uint64_t ts_sec, uint32_t ts_usec,
                            void *session_key, void *user);

typedef struct tcp_session tcp_session_t;

typedef struct {
    tcp_session_t **buckets;
    size_t          n_buckets;
    size_t          max_sessions;
    size_t          cur_sessions;
    stream_cb_t     on_stream;
    void           *user;
    time_t          max_idle_sec;
    time_t          last_cleanup;
} tcp_reasm_t;

tcp_reasm_t *tcp_reasm_new(size_t n_buckets, size_t max_sessions,
                           time_t max_idle_sec,
                           stream_cb_t cb, void *user);
void         tcp_reasm_free(tcp_reasm_t *r);

void tcp_reasm_input(tcp_reasm_t *r,
                     uint32_t src_ip, uint16_t src_port,
                     uint32_t dst_ip, uint16_t dst_port,
                     uint32_t seq, uint32_t ack,
                     uint8_t  tcp_flags, uint16_t window,
                     const uint8_t *payload, size_t payload_len,
                     uint64_t ts_sec, uint32_t ts_usec);

int tcp_reasm_get_client(void *session_key, tcp_dir_t dir,
                         char *ip_out, size_t ip_size, uint16_t *port_out);

#endif
