#include "sctp_transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline uint64_t get_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

void reorder_buffer_init(reorder_buffer_t *rb)
{
    memset(rb, 0, sizeof(*rb));
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    rb->next_expected_chunk = 0;
    pthread_mutex_init(&rb->lock, NULL);
    pthread_cond_init(&rb->cond, NULL);

    for (uint32_t i = 0; i < REORDER_BUFFER_SIZE; i++) {
        rb->entries[i].data = (uint8_t *)malloc(MAX_CHUNK_SIZE);
        rb->entries[i].valid = false;
    }
}

void reorder_buffer_destroy(reorder_buffer_t *rb)
{
    if (!rb)
        return;

    for (uint32_t i = 0; i < REORDER_BUFFER_SIZE; i++) {
        if (rb->entries[i].data) {
            free(rb->entries[i].data);
            rb->entries[i].data = NULL;
        }
    }

    pthread_mutex_destroy(&rb->lock);
    pthread_cond_destroy(&rb->cond);
}

static int find_insert_pos(reorder_buffer_t *rb, uint32_t chunk_id)
{
    if (chunk_id < rb->next_expected_chunk)
        return -1;

    if (rb->count == 0)
        return 0;

    uint32_t target = chunk_id;
    uint32_t idx = rb->head;

    for (uint32_t i = 0; i < rb->count; i++) {
        if (!rb->entries[idx].valid)
            break;
        if (rb->entries[idx].chunk_id == target)
            return -1;
        if (rb->entries[idx].chunk_id > target)
            return (int)i;
        idx = (idx + 1) % REORDER_BUFFER_SIZE;
    }

    return (int)rb->count;
}

int reorder_buffer_insert(reorder_buffer_t *rb, uint32_t chunk_id,
                           const void *data, size_t len, int path_id)
{
    if (!rb || !data || len == 0)
        return -1;

    if (len > MAX_CHUNK_SIZE)
        return -1;

    pthread_mutex_lock(&rb->lock);

    if (chunk_id < rb->next_expected_chunk) {
        pthread_mutex_unlock(&rb->lock);
        return -1;
    }

    if (chunk_id >= rb->next_expected_chunk + REORDER_BUFFER_SIZE) {
        pthread_mutex_unlock(&rb->lock);
        return -2;
    }

    if (rb->count >= REORDER_BUFFER_SIZE) {
        pthread_mutex_unlock(&rb->lock);
        return -3;
    }

    int pos = find_insert_pos(rb, chunk_id);
    if (pos < 0) {
        pthread_mutex_unlock(&rb->lock);
        return pos;
    }

    uint32_t insert_idx = (rb->head + pos) % REORDER_BUFFER_SIZE;

    if (rb->count > 0 && pos < (int)rb->count) {
        uint32_t curr = (rb->head + rb->count) % REORDER_BUFFER_SIZE;
        for (int i = (int)rb->count - 1; i >= pos; i--) {
            uint32_t src = (rb->head + i) % REORDER_BUFFER_SIZE;
            uint32_t dst = (src + 1) % REORDER_BUFFER_SIZE;
            memcpy(&rb->entries[dst], &rb->entries[src],
                   sizeof(reorder_entry_t));
        }
    }

    rb->entries[insert_idx].chunk_id = chunk_id;
    rb->entries[insert_idx].data_len = len;
    rb->entries[insert_idx].path_id = path_id;
    rb->entries[insert_idx].recv_timestamp_us = get_time_us();
    rb->entries[insert_idx].valid = true;
    memcpy(rb->entries[insert_idx].data, data, len);

    rb->count++;
    rb->tail = (rb->tail + 1) % REORDER_BUFFER_SIZE;

    if (chunk_id == rb->next_expected_chunk) {
        pthread_cond_signal(&rb->cond);
    }

    pthread_mutex_unlock(&rb->lock);
    return 0;
}

int reorder_buffer_get_next(reorder_buffer_t *rb, uint32_t *chunk_id,
                             void *data, size_t max_len, int *path_id)
{
    if (!rb || !chunk_id || !data)
        return -1;

    pthread_mutex_lock(&rb->lock);

    if (rb->count == 0 || !rb->entries[rb->head].valid ||
        rb->entries[rb->head].chunk_id != rb->next_expected_chunk) {
        pthread_mutex_unlock(&rb->lock);
        return 0;
    }

    reorder_entry_t *entry = &rb->entries[rb->head];

    if (entry->data_len > max_len) {
        pthread_mutex_unlock(&rb->lock);
        return -1;
    }

    *chunk_id = entry->chunk_id;
    memcpy(data, entry->data, entry->data_len);
    if (path_id)
        *path_id = entry->path_id;

    entry->valid = false;
    rb->head = (rb->head + 1) % REORDER_BUFFER_SIZE;
    rb->count--;
    rb->next_expected_chunk++;

    pthread_mutex_unlock(&rb->lock);
    return (int)entry->data_len;
}

void reorder_buffer_check_timeout(reorder_buffer_t *rb,
                                   uint64_t timeout_us,
                                   uint32_t *missing_ids,
                                   uint32_t *missing_count,
                                   uint32_t max_missing)
{
    if (!rb || !missing_ids || !missing_count)
        return;

    uint64_t now = get_time_us();
    *missing_count = 0;

    pthread_mutex_lock(&rb->lock);

    if (rb->count == 0) {
        pthread_mutex_unlock(&rb->lock);
        return;
    }

    uint32_t expected = rb->next_expected_chunk;
    uint32_t idx = rb->head;

    bool found_gap = false;
    for (uint32_t i = 0; i < rb->count && *missing_count < max_missing; i++) {
        if (!rb->entries[idx].valid)
            break;

        if (rb->entries[idx].chunk_id > expected) {
            for (uint32_t missing = expected;
                 missing < rb->entries[idx].chunk_id &&
                 *missing_count < max_missing; missing++) {
                missing_ids[*missing_count] = missing;
                (*missing_count)++;
            }
            found_gap = true;
            expected = rb->entries[idx].chunk_id + 1;
        } else if (rb->entries[idx].chunk_id == expected) {
            expected++;
        }

        idx = (idx + 1) % REORDER_BUFFER_SIZE;
    }

    if (*missing_count == 0 && rb->entries[rb->head].valid) {
        uint64_t oldest_ts = ~0ULL;
        for (uint32_t i = 0; i < rb->count; i++) {
            uint32_t curr = (rb->head + i) % REORDER_BUFFER_SIZE;
            if (rb->entries[curr].valid &&
                rb->entries[curr].recv_timestamp_us < oldest_ts) {
                oldest_ts = rb->entries[curr].recv_timestamp_us;
            }
        }

        if (oldest_ts != ~0ULL && (now - oldest_ts) > timeout_us) {
            uint32_t min_chunk = ~0U;
            for (uint32_t i = 0; i < rb->count; i++) {
                uint32_t curr = (rb->head + i) % REORDER_BUFFER_SIZE;
                if (rb->entries[curr].valid &&
                    rb->entries[curr].chunk_id < min_chunk) {
                    min_chunk = rb->entries[curr].chunk_id;
                }
            }

            if (min_chunk > rb->next_expected_chunk &&
                *missing_count < max_missing) {
                for (uint32_t missing = rb->next_expected_chunk;
                     missing < min_chunk && *missing_count < max_missing;
                     missing++) {
                    missing_ids[*missing_count] = missing;
                    (*missing_count)++;
                }
            }
        }
    }

    pthread_mutex_unlock(&rb->lock);
}

uint32_t reorder_buffer_get_count(reorder_buffer_t *rb)
{
    if (!rb)
        return 0;

    pthread_mutex_lock(&rb->lock);
    uint32_t count = rb->count;
    pthread_mutex_unlock(&rb->lock);

    return count;
}
