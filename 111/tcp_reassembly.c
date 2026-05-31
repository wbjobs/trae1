#include "tcp_reassembly.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static tcp_stream_t **g_streams = NULL;
static size_t g_stream_count = 0;
static size_t g_stream_capacity = MAX_TRACKED_CONNECTIONS;
static pthread_mutex_t g_streams_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint32_t get_current_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

static uint32_t stream_hash(uint32_t src_ip, uint32_t dst_ip, uint16_t src_port, uint16_t dst_port) {
    return (src_ip ^ dst_ip ^ ((uint32_t)src_port << 16) ^ (uint32_t)dst_port) % g_stream_capacity;
}

void tcp_reassembly_init(void) {
    g_streams = calloc(g_stream_capacity, sizeof(tcp_stream_t *));
    g_stream_count = 0;
}

void tcp_reassembly_destroy(void) {
    pthread_mutex_lock(&g_streams_mutex);
    for (size_t i = 0; i < g_stream_capacity; i++) {
        tcp_stream_t *stream = g_streams[i];
        while (stream) {
            tcp_stream_t *next = stream->segments;
            while (next) {
                tcp_segment_t *seg = next;
                next = next->next;
                free(seg->data);
                free(seg);
            }
            free(stream->reassembled_data);
            pthread_mutex_destroy(&stream->mutex);
            tcp_stream_t *tmp = stream;
            stream = stream->next;
            free(tmp);
        }
        g_streams[i] = NULL;
    }
    free(g_streams);
    g_streams = NULL;
    g_stream_count = 0;
    pthread_mutex_unlock(&g_streams_mutex);
}

tcp_stream_t *tcp_reassembly_get_stream(const char *src_ip, const char *dst_ip,
                                        uint16_t src_port, uint16_t dst_port,
                                        uint32_t src_ip_int, uint32_t dst_ip_int) {
    uint32_t h = stream_hash(src_ip_int, dst_ip_int, src_port, dst_port);

    pthread_mutex_lock(&g_streams_mutex);

    tcp_stream_t *stream = g_streams[h];
    while (stream) {
        if (stream->src_ip_int == src_ip_int && stream->dst_ip_int == dst_ip_int &&
            stream->src_port == src_port && stream->dst_port == dst_port) {
            stream->last_activity = get_current_time();
            pthread_mutex_unlock(&g_streams_mutex);
            return stream;
        }
        stream = stream->next;
    }

    if (g_stream_count >= MAX_TRACKED_CONNECTIONS) {
        uint32_t now = get_current_time();
        tcp_stream_t *oldest = NULL;
        tcp_stream_t **oldest_prev = NULL;
        tcp_stream_t **prev = &g_streams[h];
        stream = g_streams[h];
        while (stream) {
            if (!oldest || stream->last_activity < oldest->last_activity) {
                oldest = stream;
                oldest_prev = prev;
            }
            prev = &stream->next;
            stream = stream->next;
        }
        if (oldest && oldest->last_activity < now - CONNECTION_TIMEOUT) {
            *oldest_prev = oldest->next;
            tcp_segment_t *seg = oldest->segments;
            while (seg) {
                tcp_segment_t *next = seg->next;
                free(seg->data);
                free(seg);
                seg = next;
            }
            free(oldest->reassembled_data);
            pthread_mutex_destroy(&oldest->mutex);
            free(oldest);
            g_stream_count--;
        }
    }

    stream = calloc(1, sizeof(tcp_stream_t));
    if (!stream) {
        pthread_mutex_unlock(&g_streams_mutex);
        return NULL;
    }

    strncpy(stream->src_ip, src_ip, INET_ADDRSTRLEN - 1);
    strncpy(stream->dst_ip, dst_ip, INET_ADDRSTRLEN - 1);
    stream->src_port = src_port;
    stream->dst_port = dst_port;
    stream->src_ip_int = src_ip_int;
    stream->dst_ip_int = dst_ip_int;
    stream->next_seq = 0;
    stream->last_activity = get_current_time();
    stream->segments = NULL;
    stream->reassembled_len = 0;
    stream->reassembled_data = NULL;
    stream->syn_seen = 0;
    stream->fin_seen = 0;
    pthread_mutex_init(&stream->mutex, NULL);

    stream->next = g_streams[h];
    g_streams[h] = stream;
    g_stream_count++;

    pthread_mutex_unlock(&g_streams_mutex);
    return stream;
}

static void reassemble_stream(tcp_stream_t *stream) {
    size_t total_len = 0;
    tcp_segment_t *seg = stream->segments;

    while (seg) {
        total_len += seg->len;
        seg = seg->next;
    }

    if (total_len == 0) {
        free(stream->reassembled_data);
        stream->reassembled_data = NULL;
        stream->reassembled_len = 0;
        return;
    }

    if (stream->reassembled_data && stream->reassembled_len == total_len) {
        return;
    }

    free(stream->reassembled_data);
    stream->reassembled_data = malloc(total_len);
    if (!stream->reassembled_data) {
        stream->reassembled_len = 0;
        return;
    }

    size_t offset = 0;
    seg = stream->segments;
    while (seg) {
        memcpy(stream->reassembled_data + offset, seg->data, seg->len);
        offset += seg->len;
        seg = seg->next;
    }
    stream->reassembled_len = total_len;
}

void tcp_reassembly_add_data(tcp_stream_t *stream, const uint8_t *data, size_t len, int is_syn, int is_fin) {
    if (!stream || !data || len == 0) return;

    pthread_mutex_lock(&stream->mutex);

    if (is_syn) {
        stream->syn_seen = 1;
        stream->next_seq = 0;
        tcp_segment_t *seg = stream->segments;
        while (seg) {
            tcp_segment_t *next = seg->next;
            free(seg->data);
            free(seg);
            seg = next;
        }
        stream->segments = NULL;
        stream->reassembled_len = 0;
        free(stream->reassembled_data);
        stream->reassembled_data = NULL;
    }

    if (is_fin) {
        stream->fin_seen = 1;
    }

    if (len > 0 && data) {
        tcp_segment_t *new_seg = malloc(sizeof(tcp_segment_t));
        if (new_seg) {
            new_seg->seq = stream->next_seq;
            new_seg->len = len;
            new_seg->data = malloc(len);
            if (new_seg->data) {
                memcpy(new_seg->data, data, len);
                new_seg->next = NULL;

                tcp_segment_t **pprev = &stream->segments;
                tcp_segment_t *prev = NULL;
                tcp_segment_t *curr = stream->segments;
                while (curr && curr->seq < new_seg->seq) {
                    prev = curr;
                    pprev = &curr->next;
                    curr = curr->next;
                }
                new_seg->next = curr;
                *pprev = new_seg;
            } else {
                free(new_seg);
                new_seg = NULL;
            }
        }
    }

    stream->next_seq += len;
    stream->last_activity = get_current_time();

    reassemble_stream(stream);

    pthread_mutex_unlock(&stream->mutex);
}

void tcp_reassembly_remove_stream(tcp_stream_t *stream) {
    if (!stream) return;

    pthread_mutex_lock(&stream->mutex);

    tcp_segment_t *seg = stream->segments;
    while (seg) {
        tcp_segment_t *next = seg->next;
        free(seg->data);
        free(seg);
        seg = next;
    }
    stream->segments = NULL;
    free(stream->reassembled_data);
    stream->reassembled_data = NULL;
    stream->reassembled_len = 0;

    pthread_mutex_unlock(&stream->mutex);
}

char *tcp_reassembly_get_data(tcp_stream_t *stream, size_t *len) {
    if (!stream || !len) return NULL;

    pthread_mutex_lock(&stream->mutex);

    *len = stream->reassembled_len;
    char *data = NULL;
    if (stream->reassembled_len > 0 && stream->reassembled_data) {
        data = malloc(stream->reassembled_len);
        if (data) {
            memcpy(data, stream->reassembled_data, stream->reassembled_len);
        }
    }

    pthread_mutex_unlock(&stream->mutex);

    return data;
}
