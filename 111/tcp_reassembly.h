#ifndef TCP_REASSEMBLY_H
#define TCP_REASSEMBLY_H

#include <stdint.h>
#include <pthread.h>
#include <netinet/in.h>

typedef struct tcp_segment {
    uint32_t seq;
    uint32_t len;
    uint8_t *data;
    struct tcp_segment *next;
} tcp_segment_t;

typedef struct {
    char src_ip[INET_ADDRSTRLEN];
    char dst_ip[INET_ADDRSTRLEN];
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t src_ip_int;
    uint32_t dst_ip_int;
    uint32_t next_seq;
    uint32_t last_activity;
    tcp_segment_t *segments;
    size_t reassembled_len;
    uint8_t *reassembled_data;
    int syn_seen;
    int fin_seen;
    pthread_mutex_t mutex;
} tcp_stream_t;

void tcp_reassembly_init(void);
void tcp_reassembly_destroy(void);
tcp_stream_t *tcp_reassembly_get_stream(const char *src_ip, const char *dst_ip,
                                        uint16_t src_port, uint16_t dst_port,
                                        uint32_t src_ip_int, uint32_t dst_ip_int);
void tcp_reassembly_add_data(tcp_stream_t *stream, const uint8_t *data, size_t len, int is_syn, int is_fin);
void tcp_reassembly_remove_stream(tcp_stream_t *stream);
char *tcp_reassembly_get_data(tcp_stream_t *stream, size_t *len);

#endif
