#include "sctp_transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/select.h>

static inline uint64_t get_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static void collect_stats(transfer_context_t *tctx,
                           transfer_stats_t *stats,
                           reorder_buffer_t *reb)
{
    memset(stats, 0, sizeof(*stats));
    stats->total_bytes = tctx->file_ctx.file_size;
    stats->num_total = tctx->num_paths;
    if (reb)
        stats->reorder_buffer_count = reorder_buffer_get_count(reb);

    pthread_mutex_lock(&tctx->global_lock);
    for (int i = 0; i < tctx->num_paths; i++) {
        sctp_path_t *p = &tctx->paths[i];
        strncpy(stats->paths[i].name, p->name, MAX_PATH_NAME_LEN - 1);

        pthread_mutex_lock(&p->lock);
        stats->paths[i].bytes = p->bytes_sent;
        stats->paths[i].speed = p->speed_bps;
        stats->paths[i].rtt_us = p->avg_rtt_us;
        stats->paths[i].state = p->state;
        if (p->state != PATH_STATE_DOWN)
            stats->num_active++;
        pthread_mutex_unlock(&p->lock);

        stats->total_speed += stats->paths[i].speed;
    }

    if (tctx->file_ctx.file_size > 0) {
        uint64_t sent = tctx->file_ctx.total_sent;
        stats->progress_pct = (double)sent / tctx->file_ctx.file_size * 100.0;
    }
    pthread_mutex_unlock(&tctx->global_lock);
}

typedef struct {
    transfer_context_t *tctx;
    reorder_buffer_t   *reb;
} stats_arg_t;

static void *sender_stats_thread(void *arg)
{
    stats_arg_t *sarg = (stats_arg_t *)arg;
    transfer_stats_t stats;

    while (sarg->tctx->running) {
        collect_stats(sarg->tctx, &stats, sarg->reb);
        display_update(&stats);

        if (sarg->tctx->plot_enabled && sarg->tctx->num_paths >= 2) {
            for (int i = 0; i < sarg->tctx->num_paths; i++) {
                sctp_path_t *p = &sarg->tctx->paths[i];
                pthread_mutex_lock(&p->lock);
                plot_graph_add_data(&sarg->tctx->plot_graph, i * 2,
                                   (double)p->loss_rate);
                plot_graph_add_data(&sarg->tctx->plot_graph, i * 2 + 1,
                                   (double)p->avg_bw / (1024.0 * 1024.0));
                pthread_mutex_unlock(&p->lock);
            }
            plot_graph_add_data(&sarg->tctx->plot_graph,
                               sarg->tctx->num_paths * 2,
                               (double)stats.total_speed / (1024.0 * 1024.0));
            plot_graph_render(&sarg->tctx->plot_graph);
        }

        struct timespec ts = {0, 200 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }

    return NULL;
}

static void *sender_monitor_thread(void *arg)
{
    transfer_context_t *tctx = (transfer_context_t *)arg;
    lb_monitor_paths(tctx);
    return NULL;
}

static bool chunk_already_sent(file_context_t *ctx, uint32_t chunk_id)
{
    if (!ctx->chunk_map)
        return false;
    uint32_t byte_idx = chunk_id / 8;
    uint8_t bit_mask = (uint8_t)(1 << (chunk_id % 8));
    return (ctx->chunk_map[byte_idx] & bit_mask) != 0;
}

static void mark_chunk_sent(file_context_t *ctx, uint32_t chunk_id)
{
    if (!ctx->chunk_map)
        return;
    uint32_t byte_idx = chunk_id / 8;
    uint8_t bit_mask = (uint8_t)(1 << (chunk_id % 8));
    ctx->chunk_map[byte_idx] |= bit_mask;
}

static int send_file_meta(transfer_context_t *tctx)
{
    for (int i = 0; i < tctx->num_paths; i++) {
        sctp_path_t *p = &tctx->paths[i];
        if (p->state == PATH_STATE_DOWN)
            continue;

        int ret = msg_send_file_meta(p->sock_fd, &tctx->file_ctx, 0);
        if (ret < 0) {
            fprintf(stderr, "Failed to send file meta on path %s\n",
                    p->name);
        }
    }

    msg_header_t hdr;
    uint8_t ack_buf[64];
    struct sctp_sndrcvinfo sinfo;
    int ack_count = 0;

    for (int i = 0; i < tctx->num_paths; i++) {
        sctp_path_t *p = &tctx->paths[i];
        if (p->state == PATH_STATE_DOWN)
            continue;

        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(p->sock_fd, &rfds);

        int sel = select(p->sock_fd + 1, &rfds, NULL, NULL, &tv);
        if (sel <= 0)
            continue;

        int ret = msg_recv(p->sock_fd, &hdr, ack_buf, sizeof(ack_buf),
                           &sinfo);
        if (ret >= 0 && hdr.msg_type == MSG_FILE_META_ACK) {
            ack_count++;
        }
    }

    if (ack_count == 0) {
        fprintf(stderr, "No ACK received for file meta\n");
        return -1;
    }

    return 0;
}

static void apply_rate_limit(sctp_path_t *path, size_t bytes_sent)
{
    if (path->rate_limit_bps == 0)
        return;

    uint64_t now = get_time_us();

    pthread_mutex_lock(&path->lock);
    if (path->last_send_time_us > 0) {
        uint64_t min_interval_us = (bytes_sent * 1000000ULL) /
                                    path->rate_limit_bps;
        uint64_t elapsed = now - path->last_send_time_us;
        if (elapsed < min_interval_us) {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = (min_interval_us - elapsed) * 1000;
            nanosleep(&ts, NULL);
        }
    }
    path->last_send_time_us = get_time_us();
    pthread_mutex_unlock(&path->lock);
}

typedef struct {
    uint32_t    chunk_id;
    bool        sent;
    bool        acked;
    uint8_t     retries;
    uint64_t    send_time_us;
} inflight_chunk_t;

#define MAX_INFLIGHT  2048

static void *sender_nack_thread(void *arg)
{
    transfer_context_t *tctx = (transfer_context_t *)arg;
    uint8_t payload_buf[sizeof(nack_payload_t) + 256 * sizeof(uint32_t)];

    while (tctx->running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        int max_fd = 0;

        for (int i = 0; i < tctx->num_paths; i++) {
            sctp_path_t *p = &tctx->paths[i];
            if (p->state != PATH_STATE_DOWN && p->sock_fd >= 0) {
                FD_SET(p->sock_fd, &read_fds);
                if (p->sock_fd > max_fd)
                    max_fd = p->sock_fd;
            }
        }

        if (max_fd == 0) {
            struct timespec ts = {0, 100 * 1000 * 1000};
            nanosleep(&ts, NULL);
            continue;
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;
        int sel = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
        if (sel <= 0)
            continue;

        for (int i = 0; i < tctx->num_paths; i++) {
            sctp_path_t *p = &tctx->paths[i];
            if (p->state == PATH_STATE_DOWN || p->sock_fd < 0)
                continue;
            if (!FD_ISSET(p->sock_fd, &read_fds))
                continue;

            msg_header_t hdr;
            struct sctp_sndrcvinfo sinfo;
            int ret = msg_recv(p->sock_fd, &hdr, payload_buf,
                               sizeof(payload_buf), &sinfo);
            if (ret < 0)
                continue;

            if (hdr.msg_type == MSG_NACK) {
                nack_payload_t *nack = (nack_payload_t *)payload_buf;
                uint32_t count = ntohl(nack->missing_count);
                if (count > 0 && count <= 256) {
                    uint8_t chunk_buf[MAX_CHUNK_SIZE];
                    for (uint32_t m = 0; m < count; m++) {
                        uint32_t missing_id = ntohl(nack->missing_ids[m]);
                        if (missing_id >= tctx->file_ctx.total_chunks)
                            continue;

                        size_t chunk_size = tctx->base_chunk_size;
                        uint64_t offset = (uint64_t)missing_id * chunk_size;
                        if (offset + chunk_size > tctx->file_ctx.file_size)
                            chunk_size = (size_t)(tctx->file_ctx.file_size -
                                                    offset);

                        pthread_mutex_lock(&tctx->file_ctx.file_lock);
                        ssize_t bytes_read = pread(tctx->file_ctx.fd,
                                                    chunk_buf,
                                                    chunk_size,
                                                    (off_t)offset);
                        pthread_mutex_unlock(&tctx->file_ctx.file_lock);

                        if (bytes_read > 0) {
                            int path_idx = lb_select_path(tctx, missing_id);
                            if (path_idx >= 0) {
                                sctp_path_t *respath = &tctx->paths[path_idx];
                                if (respath->sock_fd >= 0) {
                                    msg_send_chunk(respath->sock_fd,
                                                    missing_id,
                                                    chunk_buf,
                                                    (size_t)bytes_read,
                                                    (uint16_t)(missing_id % 8));
                                }
                            }
                        }
                    }
                }
            } else if (hdr.msg_type == MSG_RTT_PROBE) {
                rtt_probe_t *probe = (rtt_probe_t *)payload_buf;
                rtt_probe_ack_t ack;
                ack.probe_id = probe->probe_id;
                ack.send_timestamp_us = probe->send_timestamp_us;
                ack.recv_timestamp_us = get_time_us();
                msg_send(p->sock_fd, MSG_RTT_PROBE_ACK, &ack,
                         sizeof(ack), (uint16_t)(i % 8), 0);
            } else if (hdr.msg_type == MSG_RTT_PROBE_ACK) {
                rtt_probe_ack_t *ack = (rtt_probe_ack_t *)payload_buf;
                sctp_path_handle_rtt_probe_ack(p, ack);
            }
        }
    }

    return NULL;
}

static int send_fec_parity(transfer_context_t *tctx, int path_idx,
                           uint32_t *chunk_ids, int num_chunks)
{
    if (num_chunks < FEC_DATA_SHARDS)
        return 0;

    sctp_path_t *path = &tctx->paths[path_idx];
    if (path->state == PATH_STATE_DOWN)
        return 0;

    uint8_t *data_ptrs[FEC_DATA_SHARDS];
    uint8_t *parity_ptrs[FEC_PARITY_SHARDS];
    size_t max_len = 0;
    bool all_ok = true;

    for (int i = 0; i < FEC_DATA_SHARDS; i++) {
        uint32_t cid = chunk_ids[i];
        uint64_t offset = (uint64_t)cid * tctx->base_chunk_size;
        size_t read_len = tctx->base_chunk_size;
        if (offset + read_len > tctx->file_ctx.file_size)
            read_len = (size_t)(tctx->file_ctx.file_size - offset);

        data_ptrs[i] = (uint8_t *)malloc(tctx->base_chunk_size);
        if (!data_ptrs[i]) {
            all_ok = false;
            break;
        }
        memset(data_ptrs[i], 0, tctx->base_chunk_size);

        pthread_mutex_lock(&tctx->file_ctx.file_lock);
        ssize_t n = pread(tctx->file_ctx.fd, data_ptrs[i], read_len,
                          (off_t)offset);
        pthread_mutex_unlock(&tctx->file_ctx.file_lock);

        if (n <= 0) {
            all_ok = false;
            break;
        }
        if (read_len > max_len)
            max_len = read_len;
    }

    if (!all_ok) {
        for (int i = 0; i < FEC_DATA_SHARDS; i++) {
            if (data_ptrs[i]) free(data_ptrs[i]);
        }
        return -1;
    }

    size_t parity_len;
    int ret = fec_encode((const uint8_t **)data_ptrs, max_len,
                         parity_ptrs, &parity_len,
                         FEC_DATA_SHARDS, FEC_PARITY_SHARDS);

    if (ret < 0) {
        for (int i = 0; i < FEC_DATA_SHARDS; i++)
            free(data_ptrs[i]);
        return -1;
    }

    fec_parity_t fec_msg;
    memset(&fec_msg, 0, sizeof(fec_msg));

    pthread_mutex_lock(&path->lock);
    fec_msg.chunk_group_id = path->fec_group_counter++;
    pthread_mutex_unlock(&path->lock);

    fec_msg.data_shards = FEC_DATA_SHARDS;
    fec_msg.parity_shards = FEC_PARITY_SHARDS;

    for (int i = 0; i < FEC_DATA_SHARDS && i < 4; i++)
        fec_msg.chunk_ids[i] = chunk_ids[i];

    for (int p = 0; p < FEC_PARITY_SHARDS; p++) {
        fec_msg.parity_idx = (uint8_t)p;
        size_t copy_len = parity_len < FEC_SHARD_SIZE ? parity_len : FEC_SHARD_SIZE;
        memcpy(fec_msg.parity_data, parity_ptrs[p], copy_len);

        msg_send_fec_parity(path->sock_fd, &fec_msg,
                           (uint16_t)(path_idx % 8));
    }

    for (int i = 0; i < FEC_DATA_SHARDS; i++)
        free(data_ptrs[i]);
    for (int i = 0; i < FEC_PARITY_SHARDS; i++)
        free(parity_ptrs[i]);

    return 0;
}

static int send_chunks(transfer_context_t *tctx)
{
    uint8_t chunk_buf[MAX_CHUNK_SIZE];
    uint32_t total_chunks = tctx->file_ctx.total_chunks;
    int consecutive_failures = 0;
    struct timespec last_save;
    clock_gettime(CLOCK_MONOTONIC, &last_save);

    uint32_t fec_chunk_ids[MAX_PATHS][FEC_DATA_SHARDS];
    int fec_chunk_count[MAX_PATHS];
    memset(fec_chunk_count, 0, sizeof(fec_chunk_count));

    uint64_t last_fec_send_time[MAX_PATHS];
    memset(last_fec_send_time, 0, sizeof(last_fec_send_time));

    uint64_t switch_timeout_check = 0;

    pthread_t nack_thr;
    pthread_create(&nack_thr, NULL, sender_nack_thread, tctx);

    for (uint32_t chunk_id = 0; chunk_id < total_chunks && tctx->running;
         chunk_id++) {
        if (tctx->resume_enabled && chunk_already_sent(&tctx->file_ctx,
                                                        chunk_id)) {
            continue;
        }

        lb_rebalance(tctx);

        int path_idx = lb_select_path(tctx, chunk_id);
        if (path_idx < 0) {
            if (consecutive_failures++ > 100) {
                fprintf(stderr, "All paths down, aborting\n");
                tctx->running = false;
                pthread_join(nack_thr, NULL);
                return -1;
            }
            struct timespec ts = {0, 100 * 1000 * 1000};
            nanosleep(&ts, NULL);
            chunk_id--;
            continue;
        }

        consecutive_failures = 0;
        sctp_path_t *path = &tctx->paths[path_idx];

        size_t chunk_size = lb_get_chunk_size(tctx, path_idx);
        uint64_t offset = (uint64_t)chunk_id * tctx->base_chunk_size;
        size_t remaining = 0;
        if (offset + tctx->base_chunk_size > tctx->file_ctx.file_size)
            remaining = (size_t)(tctx->file_ctx.file_size - offset);
        else
            remaining = tctx->base_chunk_size;

        if (chunk_size > remaining)
            chunk_size = remaining;

        pthread_mutex_lock(&tctx->file_ctx.file_lock);
        ssize_t bytes_read = pread(tctx->file_ctx.fd, chunk_buf,
                                    chunk_size, (off_t)offset);
        pthread_mutex_unlock(&tctx->file_ctx.file_lock);

        if (bytes_read <= 0) {
            perror("pread");
            tctx->running = false;
            pthread_join(nack_thr, NULL);
            return -1;
        }

        apply_rate_limit(path, (size_t)bytes_read);

        struct timespec send_start;
        clock_gettime(CLOCK_MONOTONIC, &send_start);

        int ret = msg_send_chunk(path->sock_fd, chunk_id,
                                  chunk_buf, (size_t)bytes_read,
                                  (uint16_t)(chunk_id % 8));
        if (ret < 0) {
            sctp_path_set_state(path, PATH_STATE_DOWN);
            sctp_path_update_loss(path, true);
            tctx->path_changed = true;
            chunk_id--;
            continue;
        }

        sctp_path_update_loss(path, false);

        mark_chunk_sent(&tctx->file_ctx, chunk_id);

        fec_chunk_ids[path_idx][fec_chunk_count[path_idx] % FEC_DATA_SHARDS] =
            chunk_id;
        fec_chunk_count[path_idx]++;

        uint64_t now_us = get_time_us();
        bool in_fec_mode = (tctx->switch_state == SWITCH_STATE_FEC);
        bool fec_timeout = (now_us - last_fec_send_time[path_idx] > 500000);

        if ((fec_chunk_count[path_idx] % FEC_DATA_SHARDS == 0 &&
             fec_chunk_count[path_idx] > 0) ||
            (in_fec_mode && fec_timeout && fec_chunk_count[path_idx] >= FEC_DATA_SHARDS)) {

            int start_idx = (fec_chunk_count[path_idx] / FEC_DATA_SHARDS - 1) *
                           FEC_DATA_SHARDS;
            if (start_idx >= 0) {
                uint32_t ids[FEC_DATA_SHARDS];
                for (int i = 0; i < FEC_DATA_SHARDS; i++) {
                    ids[i] = fec_chunk_ids[path_idx][(start_idx + i) % FEC_DATA_SHARDS];
                }
                send_fec_parity(tctx, path_idx, ids, FEC_DATA_SHARDS);
                last_fec_send_time[path_idx] = now_us;
            }
        }

        pthread_mutex_lock(&path->lock);
        path->bytes_sent += (uint64_t)bytes_read;
        pthread_mutex_unlock(&path->lock);

        pthread_mutex_lock(&tctx->global_lock);
        tctx->file_ctx.total_sent += (uint64_t)bytes_read;
        pthread_mutex_unlock(&tctx->global_lock);

        sctp_path_update_bw(path, (uint64_t)bytes_read, &send_start);

        if (tctx->switch_state != SWITCH_STATE_NORMAL &&
            now_us - switch_timeout_check > 1000000) {
            if (tctx->switch_start_time_us > 0) {
                uint64_t elapsed = now_us - tctx->switch_start_time_us;
                if (elapsed > 10000000) {
                    lb_complete_switch(tctx);
                }
            }
            switch_timeout_check = now_us;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec - last_save.tv_sec) >= 5) {
            if (tctx->resume_enabled) {
                resume_save_state(tctx->resume_file, &tctx->file_ctx);
            }
            last_save = now;
        }
    }

    if (tctx->switch_state != SWITCH_STATE_NORMAL) {
        lb_complete_switch(tctx);
    }

    tctx->running = false;
    pthread_join(nack_thr, NULL);
    return 0;
}

static int wait_for_complete(transfer_context_t *tctx)
{
    msg_header_t hdr;
    uint8_t buf[256];
    struct sctp_sndrcvinfo sinfo;
    int retries = 0;

    while (retries < 30) {
        for (int i = 0; i < tctx->num_paths; i++) {
            sctp_path_t *p = &tctx->paths[i];
            if (p->state == PATH_STATE_DOWN || p->sock_fd < 0)
                continue;

            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(p->sock_fd, &rfds);

            int sel = select(p->sock_fd + 1, &rfds, NULL, NULL, &tv);
            if (sel <= 0)
                continue;

            int ret = msg_recv(p->sock_fd, &hdr, buf, sizeof(buf), &sinfo);
            if (ret < 0)
                continue;

            if (hdr.msg_type == MSG_COMPLETE) {
                uint32_t recv_crc;
                memcpy(&recv_crc, buf, sizeof(recv_crc));

                printf("\nFile CRC32C (sender):  0x%08x\n",
                       tctx->file_ctx.file_crc32c);
                printf("File CRC32C (receiver): 0x%08x\n", recv_crc);

                if (recv_crc == tctx->file_ctx.file_crc32c) {
                    printf("CRC32C verification: PASSED\n");
                    return 0;
                } else {
                    printf("CRC32C verification: FAILED\n");
                    return -1;
                }
            } else if (hdr.msg_type == MSG_RTT_PROBE_ACK) {
                rtt_probe_ack_t *ack = (rtt_probe_ack_t *)buf;
                sctp_path_handle_rtt_probe_ack(p, ack);
            }
        }
        retries++;
    }

    fprintf(stderr, "Timeout waiting for completion message\n");
    return -1;
}

int sender_run(const char *filename,
                const char *remote_addr, int port,
                const char *const *local_addrs, int num_local,
                bool reverse, bool resume,
                uint32_t latency_diff_ms, bool plot_graph)
{
    transfer_context_t tctx;
    memset(&tctx, 0, sizeof(tctx));
    tctx.is_sender = true;
    tctx.reverse_mode = reverse;
    tctx.resume_enabled = resume;
    tctx.running = true;
    tctx.latency_diff_threshold_ms = latency_diff_ms;
    tctx.plot_enabled = plot_graph;
    tctx.switch_state = SWITCH_STATE_NORMAL;
    tctx.failing_path_idx = -1;

    pthread_mutex_init(&tctx.switch_lock, NULL);

    if (plot_graph) {
        plot_graph_init(&tctx.plot_graph, true);
        plot_graph_add_series(&tctx.plot_graph, "WiFi Loss%", 0);
        plot_graph_add_series(&tctx.plot_graph, "Eth Loss%", 1);
        plot_graph_add_series(&tctx.plot_graph, "WiFi BW", 2);
        plot_graph_add_series(&tctx.plot_graph, "Eth BW", 3);
        plot_graph_add_series(&tctx.plot_graph, "Total Speed", 4);
    }

    if (resume) {
        snprintf(tctx.resume_file, MAX_FILENAME_LEN,
                 "%s.resume", filename);
    }

    if (file_ctx_open_send(&tctx.file_ctx, filename) < 0)
        return -1;

    tctx.base_chunk_size = tctx.file_ctx.chunk_size;
    tctx.num_paths = num_local > 0 ? num_local : 2;
    if (tctx.num_paths > MAX_PATHS)
        tctx.num_paths = MAX_PATHS;

    const char *path_names[MAX_PATHS] = {"WiFi", "Ethernet",
                                          "Path3", "Path4"};
    uint16_t local_ports[MAX_PATHS];

    for (int i = 0; i < tctx.num_paths; i++)
        local_ports[i] = (uint16_t)(port + i);

    int connected = 0;
    for (int i = 0; i < tctx.num_paths; i++) {
        const char *lip = (i < num_local && local_addrs[i]) ?
                           local_addrs[i] : "0.0.0.0";
        if (sctp_path_init(&tctx.paths[i], i, lip, local_ports[i],
                           remote_addr, (uint16_t)port,
                           path_names[i]) < 0) {
            fprintf(stderr, "Failed to init path %d\n", i);
            tctx.paths[i].state = PATH_STATE_DOWN;
            continue;
        }

        if (sctp_path_connect(&tctx.paths[i]) < 0) {
            fprintf(stderr, "Failed to connect path %s (%s)\n",
                    tctx.paths[i].name, strerror(errno));
            tctx.paths[i].state = PATH_STATE_DOWN;
        } else {
            connected++;
        }
    }

    if (connected == 0) {
        fprintf(stderr, "No paths could connect\n");
        for (int i = 0; i < tctx.num_paths; i++)
            sctp_path_close(&tctx.paths[i]);
        file_ctx_close(&tctx.file_ctx);
        return -1;
    }

    pthread_mutex_init(&tctx.global_lock, NULL);
    tctx.active_paths = connected;

    if (resume) {
        resume_load_state(tctx.resume_file, &tctx.file_ctx);
    }

    printf("Computing file CRC32C...\n");
    if (file_ctx_compute_crc(&tctx.file_ctx,
                              &tctx.file_ctx.file_crc32c) < 0) {
        fprintf(stderr, "Failed to compute file CRC\n");
        for (int i = 0; i < tctx.num_paths; i++)
            sctp_path_close(&tctx.paths[i]);
        file_ctx_close(&tctx.file_ctx);
        pthread_mutex_destroy(&tctx.global_lock);
        return -1;
    }
    printf("File CRC32C: 0x%08x\n", tctx.file_ctx.file_crc32c);
    printf("Latency diff threshold: %u ms\n", latency_diff_ms);

    display_init(true);

    pthread_t stats_thr, monitor_thr;
    stats_arg_t sarg;
    sarg.tctx = &tctx;
    sarg.reb = NULL;

    pthread_create(&stats_thr, NULL, sender_stats_thread, &sarg);
    pthread_create(&monitor_thr, NULL, sender_monitor_thread, &tctx);

    int ret = 0;
    if (send_file_meta(&tctx) < 0) {
        ret = -1;
    } else if (send_chunks(&tctx) < 0) {
        ret = -1;
    } else {
        for (int i = 0; i < tctx.num_paths; i++) {
            sctp_path_t *p = &tctx.paths[i];
            if (p->state != PATH_STATE_DOWN && p->sock_fd >= 0) {
                msg_send(p->sock_fd, MSG_COMPLETE,
                         &tctx.file_ctx.file_crc32c,
                         sizeof(tctx.file_ctx.file_crc32c), 0, 0);
                break;
            }
        }
        ret = wait_for_complete(&tctx);
    }

    tctx.running = false;
    pthread_join(stats_thr, NULL);
    pthread_join(monitor_thr, NULL);

    transfer_stats_t stats;
    collect_stats(&tctx, &stats, NULL);
    display_final(&stats, ret == 0);

    if (ret == 0 && resume) {
        remove(tctx.resume_file);
    }

    for (int i = 0; i < tctx.num_paths; i++)
        sctp_path_close(&tctx.paths[i]);
    file_ctx_close(&tctx.file_ctx);
    pthread_mutex_destroy(&tctx.global_lock);

    return ret;
}
