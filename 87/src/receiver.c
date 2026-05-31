#include "sctp_transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/select.h>
#include <sys/stat.h>

static inline uint64_t get_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static void collect_stats_recv(transfer_context_t *tctx,
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
        stats->paths[i].bytes = p->bytes_acked;
        stats->paths[i].speed = p->speed_bps;
        stats->paths[i].rtt_us = p->avg_rtt_us;
        stats->paths[i].state = p->state;
        if (p->state != PATH_STATE_DOWN)
            stats->num_active++;
        pthread_mutex_unlock(&p->lock);

        stats->total_speed += stats->paths[i].speed;
    }

    if (tctx->file_ctx.file_size > 0) {
        uint64_t recv = tctx->file_ctx.total_received;
        stats->progress_pct = (double)recv / tctx->file_ctx.file_size * 100.0;
    }
    pthread_mutex_unlock(&tctx->global_lock);
}

typedef struct {
    transfer_context_t *tctx;
    reorder_buffer_t   *reb;
} recv_stats_arg_t;

static void *recv_stats_thread(void *arg)
{
    recv_stats_arg_t *sarg = (recv_stats_arg_t *)arg;
    transfer_stats_t stats;

    while (sarg->tctx->running) {
        collect_stats_recv(sarg->tctx, &stats, sarg->reb);
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

static void *recv_monitor_thread(void *arg)
{
    transfer_context_t *tctx = (transfer_context_t *)arg;
    lb_monitor_paths(tctx);
    return NULL;
}

static void *nack_sender_thread(void *arg)
{
    reorder_buffer_t *reb = (reorder_buffer_t *)arg;
    (void)reb;
    return NULL;
}

static bool chunk_already_received(file_context_t *ctx, uint32_t chunk_id)
{
    if (!ctx->chunk_map)
        return false;
    uint32_t byte_idx = chunk_id / 8;
    uint8_t bit_mask = (uint8_t)(1 << (chunk_id % 8));
    return (ctx->chunk_map[byte_idx] & bit_mask) != 0;
}

static int handle_file_meta(transfer_context_t *tctx, int sock_fd,
                             struct sctp_sndrcvinfo *sinfo,
                             const char *output_dir)
{
    file_meta_t meta;
    msg_header_t hdr;
    int ret = msg_recv(sock_fd, &hdr, &meta, sizeof(meta), sinfo);
    if (ret < 0) {
        fprintf(stderr, "Failed to receive file meta\n");
        return -1;
    }

    if (hdr.msg_type != MSG_FILE_META) {
        fprintf(stderr, "Expected FILE_META, got %d\n", hdr.msg_type);
        return -1;
    }

    if (meta.crc32c != 0) {
        uint32_t calc = crc32c_compute(&meta,
                       sizeof(meta) - sizeof(meta.crc32c));
        if (calc != meta.crc32c) {
            fprintf(stderr, "Meta CRC mismatch\n");
            return -1;
        }
    }

    strncpy(tctx->file_ctx.filename, meta.filename, MAX_FILENAME_LEN - 1);
    tctx->file_ctx.file_size = meta.file_size;
    tctx->file_ctx.chunk_size = meta.chunk_size;
    tctx->file_ctx.total_chunks = meta.total_chunks;
    tctx->file_ctx.file_crc32c = meta.crc32c;

    char fullpath[MAX_FILENAME_LEN + 256];
    snprintf(fullpath, sizeof(fullpath), "%s/%s",
             output_dir, tctx->file_ctx.filename);

    if (file_ctx_open_recv(&tctx->file_ctx, fullpath,
                            meta.file_size, meta.chunk_size) < 0) {
        fprintf(stderr, "Failed to open output file: %s\n", fullpath);
        return -1;
    }

    tctx->file_ctx.file_crc32c = meta.crc32c;

    if (tctx->resume_enabled) {
        snprintf(tctx->resume_file, MAX_FILENAME_LEN,
                 "%s.resume", fullpath);
        resume_load_state(tctx->resume_file, &tctx->file_ctx);
    }

    msg_send(sock_fd, MSG_FILE_META_ACK, NULL, 0,
             sinfo->sinfo_stream, 0);

    printf("Receiving: %s\n", tctx->file_ctx.filename);
    printf("File size: %lu bytes, Chunks: %u, Chunk size: %u\n",
           (unsigned long)tctx->file_ctx.file_size,
           tctx->file_ctx.total_chunks,
           tctx->file_ctx.chunk_size);
    printf("Reorder buffer: %d entries\n", REORDER_BUFFER_SIZE);
    printf("Latency diff threshold: %u ms\n", tctx->latency_diff_threshold_ms);
    if (tctx->resume_enabled) {
        uint64_t already = tctx->file_ctx.total_received;
        printf("Resuming from: %lu bytes received\n",
               (unsigned long)already);
    }

    return 0;
}

static int flush_reorder_buffer(transfer_context_t *tctx, reorder_buffer_t *reb)
{
    uint8_t data_buf[MAX_CHUNK_SIZE];
    uint32_t chunk_id;
    int path_id;
    int processed = 0;

    while (tctx->running) {
        int ret = reorder_buffer_get_next(reb, &chunk_id, data_buf,
                                           MAX_CHUNK_SIZE, &path_id);
        if (ret <= 0)
            break;

        if (chunk_id >= tctx->file_ctx.total_chunks) {
            continue;
        }

        struct timespec recv_start;
        clock_gettime(CLOCK_MONOTONIC, &recv_start);

        if (!file_ctx_chunk_received(&tctx->file_ctx, chunk_id)) {
            file_ctx_write_chunk(&tctx->file_ctx, chunk_id,
                                  data_buf, (size_t)ret);

            if (path_id >= 0 && path_id < tctx->num_paths) {
                sctp_path_t *p = &tctx->paths[path_id];
                if (p->state != PATH_STATE_DOWN) {
                    pthread_mutex_lock(&p->lock);
                    p->bytes_acked += (uint64_t)ret;
                    pthread_mutex_unlock(&p->lock);
                    sctp_path_update_bw(p, (uint64_t)ret, &recv_start);
                }
            }
        }

        processed++;
    }

    return processed;
}

static int recv_chunks(transfer_context_t *tctx, reorder_buffer_t *reb)
{
    uint8_t chunk_data[MAX_CHUNK_SIZE];
    uint32_t chunk_id;
    int consecutive_idle = 0;
    struct timespec last_save;
    uint64_t last_nack_time = 0;
    clock_gettime(CLOCK_MONOTONIC, &last_save);

    uint32_t missing_ids[256];
    uint32_t missing_count = 0;

    while (tctx->running) {
        flush_reorder_buffer(tctx, reb);

        if (file_ctx_all_chunks_received(&tctx->file_ctx))
            break;

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
            if (consecutive_idle++ > 100) {
                fprintf(stderr, "All paths down\n");
                return -1;
            }
            struct timespec ts = {0, 100 * 1000 * 1000};
            nanosleep(&ts, NULL);
            continue;
        }
        consecutive_idle = 0;

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50 * 1000;

        int ret = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            perror("select");
            return -1;
        }

        if (ret == 0) {
            uint64_t now = get_time_us();
            if ((now - last_nack_time) > (NACK_TIMEOUT_MS * 1000ULL)) {
                missing_count = 0;
                reorder_buffer_check_timeout(reb, NACK_TIMEOUT_MS * 1000ULL,
                                              missing_ids, &missing_count,
                                              sizeof(missing_ids) /
                                                sizeof(missing_ids[0]));
                if (missing_count > 0) {
                    for (int i = 0; i < tctx->num_paths; i++) {
                        sctp_path_t *p = &tctx->paths[i];
                        if (p->state != PATH_STATE_DOWN && p->sock_fd >= 0) {
                            msg_send_nack(p->sock_fd, missing_ids,
                                           missing_count, 0);
                        }
                    }
                }
                last_nack_time = now;
            }
            continue;
        }

        for (int i = 0; i < tctx->num_paths; i++) {
            sctp_path_t *p = &tctx->paths[i];
            if (p->state == PATH_STATE_DOWN || p->sock_fd < 0)
                continue;
            if (!FD_ISSET(p->sock_fd, &read_fds))
                continue;

            msg_header_t hdr;
            struct sctp_sndrcvinfo sinfo;
            uint8_t payload_buf[sizeof(chunk_payload_t) + MAX_CHUNK_SIZE];

            int payload_len = msg_recv(p->sock_fd, &hdr, payload_buf,
                                       sizeof(payload_buf), &sinfo);
            if (payload_len < 0) {
                sctp_path_set_state(p, PATH_STATE_DOWN);
                continue;
            }

            if (hdr.msg_type == MSG_CHUNK) {
                chunk_payload_t *chunk_payload = (chunk_payload_t *)payload_buf;
                chunk_id = ntohl(chunk_payload->chunk_id);
                size_t data_len = ntohl(chunk_payload->data_len);

                if (chunk_id >= tctx->file_ctx.total_chunks) {
                    fprintf(stderr, "Invalid chunk ID: %u (max %u)\n",
                            chunk_id, tctx->file_ctx.total_chunks - 1);
                    continue;
                }

                if (chunk_already_received(&tctx->file_ctx, chunk_id))
                    continue;

                if (data_len > MAX_CHUNK_SIZE) {
                    fprintf(stderr, "Chunk too large: %zu\n", data_len);
                    continue;
                }

                int insert_ret = reorder_buffer_insert(reb, chunk_id,
                                                        chunk_payload->data,
                                                        data_len, i);
                if (insert_ret == -2) {
                    missing_ids[0] = chunk_id;
                    for (int k = 0; k < tctx->num_paths; k++) {
                        sctp_path_t *pp = &tctx->paths[k];
                        if (pp->state != PATH_STATE_DOWN && pp->sock_fd >= 0) {
                            msg_send_nack(pp->sock_fd, missing_ids, 1, 0);
                        }
                    }
                } else if (insert_ret < 0) {
                    continue;
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
            } else if (hdr.msg_type == MSG_FEC_PARITY) {
                fec_parity_t *fec = (fec_parity_t *)payload_buf;
                int data_shards = fec->data_shards;
                int parity_shards = fec->parity_shards;
                int parity_idx = fec->parity_idx;

                if (data_shards > 0 && data_shards <= FEC_DATA_SHARDS &&
                    parity_idx < parity_shards) {

                    for (int d = 0; d < data_shards; d++) {
                        uint32_t cid = ntohl(fec->chunk_ids[d]);
                        if (cid < tctx->file_ctx.total_chunks &&
                            !file_ctx_chunk_received(&tctx->file_ctx, cid)) {

                            uint8_t *parity_data = (uint8_t *)malloc(FEC_SHARD_SIZE);
                            if (parity_data) {
                                memcpy(parity_data, fec->parity_data,
                                       FEC_SHARD_SIZE);

                                fec_group_t *fg = &p->current_fec_group;
                                if (fg->data_shards != (uint8_t)data_shards ||
                                    fg->parity_shards != (uint8_t)parity_shards) {
                                    fec_group_init(fg,
                                                   ntohl(fec->chunk_group_id),
                                                   data_shards, parity_shards);
                                }
                                fg->data[d] = NULL;
                                fg->parity[parity_idx] = parity_data;
                                fg->chunk_ids[d] = cid;
                            }
                        }
                    }
                }
            }
        }

        flush_reorder_buffer(tctx, reb);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec - last_save.tv_sec) >= 5) {
            if (tctx->resume_enabled) {
                resume_save_state(tctx->resume_file, &tctx->file_ctx);
            }
            last_save = now;
        }
    }

    return 0;
}

static int verify_and_complete(transfer_context_t *tctx)
{
    printf("\nComputing file CRC32C for verification...\n");
    uint32_t recv_crc;
    if (file_ctx_compute_crc(&tctx->file_ctx, &recv_crc) < 0) {
        fprintf(stderr, "Failed to compute CRC\n");
        return -1;
    }

    printf("Computed CRC32C: 0x%08x\n", recv_crc);
    if (tctx->file_ctx.file_crc32c != 0)
        printf("Expected CRC32C: 0x%08x\n", tctx->file_ctx.file_crc32c);

    bool crc_ok = true;
    if (tctx->file_ctx.file_crc32c != 0 &&
        recv_crc != tctx->file_ctx.file_crc32c) {
        printf("CRC32C verification: FAILED\n");
        crc_ok = false;
    } else {
        printf("CRC32C verification: PASSED\n");
    }

    for (int i = 0; i < tctx->num_paths; i++) {
        sctp_path_t *p = &tctx->paths[i];
        if (p->state != PATH_STATE_DOWN && p->sock_fd >= 0) {
            msg_send(p->sock_fd, MSG_COMPLETE, &recv_crc,
                     sizeof(recv_crc), 0, 0);
            break;
        }
    }

    if (crc_ok && tctx->resume_enabled) {
        remove(tctx->resume_file);
    }

    return crc_ok ? 0 : -1;
}

static int handle_reverse_send(int sock_fd, const char *output_dir)
{
    printf("\nReverse mode: enter filename to send back:\n> ");
    fflush(stdout);

    char filename[MAX_FILENAME_LEN];
    if (fgets(filename, sizeof(filename), stdin) == NULL)
        return -1;

    filename[strcspn(filename, "\n")] = 0;

    char fullpath[MAX_FILENAME_LEN + 256];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", output_dir, filename);

    file_context_t send_ctx;
    if (file_ctx_open_send(&send_ctx, fullpath) < 0) {
        fprintf(stderr, "Cannot open file for reverse send: %s\n", fullpath);
        return -1;
    }

    if (file_ctx_compute_crc(&send_ctx, &send_ctx.file_crc32c) < 0) {
        file_ctx_close(&send_ctx);
        return -1;
    }

    printf("Reverse sending: %s (%lu bytes)\n",
           send_ctx.filename, (unsigned long)send_ctx.file_size);

    msg_send_file_meta(sock_fd, &send_ctx, 0);

    msg_header_t hdr;
    uint8_t ack_buf[64];
    struct sctp_sndrcvinfo sinfo;
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock_fd, &rfds);

    if (select(sock_fd + 1, &rfds, NULL, NULL, &tv) > 0) {
        msg_recv(sock_fd, &hdr, ack_buf, sizeof(ack_buf), &sinfo);
    }

    uint8_t chunk_buf[MAX_CHUNK_SIZE];
    for (uint32_t i = 0; i < send_ctx.total_chunks; i++) {
        size_t len;
        if (file_ctx_read_chunk(&send_ctx, i, chunk_buf, &len) < 0)
            break;
        if (msg_send_chunk(sock_fd, i, chunk_buf, len,
                           (uint16_t)(i % 8)) < 0)
            break;
    }

    msg_send(sock_fd, MSG_COMPLETE, &send_ctx.file_crc32c,
             sizeof(send_ctx.file_crc32c), 0, 0);

    file_ctx_close(&send_ctx);
    printf("Reverse send complete.\n");
    return 0;
}

int receiver_run(const char *output_dir,
                  const char *bind_addr, int port,
                  bool allow_reverse, bool resume,
                  uint32_t latency_diff_ms, bool plot_graph)
{
    transfer_context_t tctx;
    memset(&tctx, 0, sizeof(tctx));
    tctx.is_sender = false;
    tctx.resume_enabled = resume;
    tctx.running = true;
    tctx.num_paths = MAX_PATHS;
    tctx.latency_diff_threshold_ms = latency_diff_ms;
    tctx.plot_enabled = plot_graph;
    tctx.switch_state = SWITCH_STATE_NORMAL;
    tctx.failing_path_idx = -1;

    pthread_mutex_init(&tctx.switch_lock, NULL);

    if (plot_graph) {
        plot_graph_init(&tctx.plot_graph, true);
        plot_graph_add_series(&tctx.plot_graph, "Path1 Loss%", 0);
        plot_graph_add_series(&tctx.plot_graph, "Path2 Loss%", 1);
        plot_graph_add_series(&tctx.plot_graph, "Path1 BW", 2);
        plot_graph_add_series(&tctx.plot_graph, "Path2 BW", 3);
        plot_graph_add_series(&tctx.plot_graph, "Total Speed", 4);
    }

    sctp_path_t listener;
    if (sctp_path_init(&listener, 0, bind_addr, (uint16_t)port,
                       "0.0.0.0", 0, "Listener") < 0)
        return -1;

    if (sctp_path_bind(&listener) < 0) {
        sctp_path_close(&listener);
        return -1;
    }

    if (sctp_path_listen(&listener) < 0) {
        sctp_path_close(&listener);
        return -1;
    }

    printf("Waiting for connection on %s:%d...\n", bind_addr, port);
    printf("Reorder buffer size: %d entries\n", REORDER_BUFFER_SIZE);
    printf("NACK timeout: %u ms\n", NACK_TIMEOUT_MS);

    int connected = 0;
    while (connected < MAX_PATHS) {
        sctp_path_t client;
        memset(&client, 0, sizeof(client));
        client.sock_fd = -1;

        if (sctp_path_accept(&listener, &client) < 0)
            continue;

        printf("Connection from %s:%d\n",
               client.remote_ip, client.remote_port);

        for (int i = 0; i < MAX_PATHS; i++) {
            if (tctx.paths[i].sock_fd < 0) {
                memcpy(&tctx.paths[i], &client, sizeof(client));
                char name[MAX_PATH_NAME_LEN];
                snprintf(name, sizeof(name), "Path-%d", i + 1);
                strncpy(tctx.paths[i].name, name, MAX_PATH_NAME_LEN - 1);
                tctx.paths[i].state = PATH_STATE_HEALTHY;
                pthread_mutex_init(&tctx.paths[i].lock, NULL);
                connected++;
                break;
            }
        }

        if (connected >= 2)
            break;
    }

    tctx.num_paths = connected;
    tctx.active_paths = connected;
    pthread_mutex_init(&tctx.global_lock, NULL);

    reorder_buffer_t reb;
    reorder_buffer_init(&reb);

    display_init(false);

    pthread_t stats_thr, monitor_thr, nack_thr;
    recv_stats_arg_t sarg;
    sarg.tctx = &tctx;
    sarg.reb = &reb;

    pthread_create(&stats_thr, NULL, recv_stats_thread, &sarg);
    pthread_create(&monitor_thr, NULL, recv_monitor_thread, &tctx);
    pthread_create(&nack_thr, NULL, nack_sender_thread, &reb);

    int primary_fd = -1;
    for (int i = 0; i < connected; i++) {
        if (tctx.paths[i].sock_fd >= 0) {
            primary_fd = tctx.paths[i].sock_fd;
            break;
        }
    }

    int ret = -1;
    if (primary_fd >= 0) {
        struct sctp_sndrcvinfo sinfo;
        memset(&sinfo, 0, sizeof(sinfo));

        ret = handle_file_meta(&tctx, primary_fd, &sinfo, output_dir);

        if (ret == 0)
            ret = recv_chunks(&tctx, &reb);

        if (ret == 0)
            ret = verify_and_complete(&tctx);

        if (ret == 0 && allow_reverse) {
            handle_reverse_send(primary_fd, output_dir);
        }
    }

    tctx.running = false;
    pthread_join(stats_thr, NULL);
    pthread_join(monitor_thr, NULL);
    pthread_join(nack_thr, NULL);

    transfer_stats_t stats;
    collect_stats_recv(&tctx, &stats, &reb);
    display_final(&stats, ret == 0);

    reorder_buffer_destroy(&reb);

    for (int i = 0; i < tctx.num_paths; i++) {
        if (tctx.paths[i].sock_fd >= 0)
            sctp_path_close(&tctx.paths[i]);
    }
    sctp_path_close(&listener);
    if (tctx.file_ctx.fd >= 0)
        file_ctx_close(&tctx.file_ctx);
    pthread_mutex_destroy(&tctx.global_lock);

    return ret;
}
