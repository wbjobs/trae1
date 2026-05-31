#include "sctp_transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t get_min_rtt(transfer_context_t *tctx)
{
    uint64_t min_rtt = ~0ULL;
    for (int i = 0; i < tctx->num_paths; i++) {
        sctp_path_t *p = &tctx->paths[i];
        if (p->state == PATH_STATE_DOWN)
            continue;
        pthread_mutex_lock(&p->lock);
        if (p->avg_rtt_us > 0 && p->avg_rtt_us < min_rtt)
            min_rtt = p->avg_rtt_us;
        pthread_mutex_unlock(&p->lock);
    }
    return min_rtt == ~0ULL ? 10000 : min_rtt;
}

static int get_primary_path(transfer_context_t *tctx)
{
    uint64_t min_rtt = ~0ULL;
    int primary_idx = -1;

    for (int i = 0; i < tctx->num_paths; i++) {
        sctp_path_t *p = &tctx->paths[i];
        if (p->state == PATH_STATE_DOWN)
            continue;
        pthread_mutex_lock(&p->lock);
        if (p->avg_rtt_us < min_rtt) {
            min_rtt = p->avg_rtt_us;
            primary_idx = i;
        }
        pthread_mutex_unlock(&p->lock);
    }
    return primary_idx;
}

uint32_t lb_get_chunk_size(transfer_context_t *tctx, int path_id)
{
    if (path_id < 0 || path_id >= tctx->num_paths)
        return tctx->base_chunk_size;

    sctp_path_t *path = &tctx->paths[path_id];
    if (path->state == PATH_STATE_DOWN)
        return 0;

    uint64_t min_rtt = get_min_rtt(tctx);

    pthread_mutex_lock(&path->lock);
    uint64_t bw = path->avg_bw;
    uint64_t rtt = path->avg_rtt_us;
    uint32_t max_size = path->max_chunk_size;
    pthread_mutex_unlock(&path->lock);

    if (bw == 0)
        return MIN_CHUNK_SIZE;

    uint64_t rtt_diff_ms = 0;
    if (rtt > min_rtt)
        rtt_diff_ms = (rtt - min_rtt) / 1000;

    uint32_t size = (uint32_t)((bw * 100) / 1000);

    uint64_t threshold = (uint64_t)tctx->latency_diff_threshold_ms;
    if (rtt_diff_ms > threshold) {
        double reduction = 1.0 - ((double)(rtt_diff_ms - threshold) /
                               (double)(threshold * 5));
        if (reduction < 0.25)
            reduction = 0.25;
        size = (uint32_t)(size * reduction);
        max_size = (uint32_t)(max_size * reduction);
    }

    if (size > max_size)
        size = max_size;
    if (size > MAX_CHUNK_SIZE)
        size = MAX_CHUNK_SIZE;
    if (size < MIN_CHUNK_SIZE)
        size = MIN_CHUNK_SIZE;

    return size;
}

int lb_select_path(transfer_context_t *tctx, uint32_t chunk_id)
{
    uint64_t total_score = 0;
    int healthy_indices[MAX_PATHS];
    int num_healthy = 0;
    double scores[MAX_PATHS];

    uint64_t min_rtt = get_min_rtt(tctx);

    pthread_mutex_lock(&tctx->global_lock);
    for (int i = 0; i < tctx->num_paths; i++) {
        sctp_path_t *p = &tctx->paths[i];
        pthread_mutex_lock(&p->lock);
        if (p->state != PATH_STATE_DOWN && p->sock_fd >= 0) {
            double bw_score = (double)p->avg_bw;
            double rtt_score = 1.0;

            if (p->avg_rtt_us > 0 && min_rtt > 0) {
                uint64_t rtt_diff = p->avg_rtt_us > min_rtt ?
                                   (p->avg_rtt_us - min_rtt) : 0;
                uint64_t threshold_us = (uint64_t)tctx->latency_diff_threshold_ms
                                        * 1000;
                if (rtt_diff > threshold_us) {
                    double penalty = (double)(rtt_diff - threshold_us) /
                                    (double)(threshold_us * 5);
                    rtt_score = 1.0 - penalty;
                    if (rtt_score < 0.1)
                        rtt_score = 0.1;
                }
            }

            scores[num_healthy] = bw_score * rtt_score;
            if (scores[num_healthy] < 0)
                scores[num_healthy] = 0;
            total_score += (uint64_t)scores[num_healthy];
            healthy_indices[num_healthy++] = i;
        }
        pthread_mutex_unlock(&p->lock);
    }
    pthread_mutex_unlock(&tctx->global_lock);

    if (num_healthy == 0)
        return -1;

    if (total_score == 0 || tctx->file_ctx.total_chunks == 0)
        return healthy_indices[chunk_id % num_healthy];

    uint64_t target = (uint64_t)((double)chunk_id /
                         (double)tctx->file_ctx.total_chunks * total_score);

    uint64_t accumulated = 0;
    for (int i = 0; i < num_healthy; i++) {
        int idx = healthy_indices[i];
        accumulated += (uint64_t)scores[i];
        if (target <= accumulated)
            return idx;
    }

    return healthy_indices[num_healthy - 1];
}

static void check_path_health(sctp_path_t *path)
{
    if (path->sock_fd < 0) {
        path->state = PATH_STATE_DOWN;
        return;
    }

    struct sctp_status status;
    socklen_t status_len = sizeof(status);
    int ret = getsockopt(path->sock_fd, IPPROTO_SCTP, SCTP_STATUS,
                         &status, &status_len);
    if (ret < 0) {
        path->state = PATH_STATE_DOWN;
        return;
    }

    if (status.sstat_state == SCTP_STATE_CLOSED ||
        status.sstat_state == SCTP_STATE_COOKIE_WAIT ||
        status.sstat_state == SCTP_STATE_COOKIE_ECHOED) {
        path->state = PATH_STATE_DOWN;
        return;
    }

    pthread_mutex_lock(&path->lock);
    if (path->avg_bw < 512 * 1024) {
        path->state = PATH_STATE_DEGRADED;
    } else if (path->avg_bw < 1024 * 1024) {
        path->state = PATH_STATE_SLOW;
    } else {
        path->state = PATH_STATE_HEALTHY;
    }
    pthread_mutex_unlock(&path->lock);
}

void lb_monitor_paths(transfer_context_t *tctx)
{
    static uint64_t last_probe_time = 0;
    struct timespec ts_now;

    while (tctx->running) {
        int changed = 0;

        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        uint64_t now_us = (uint64_t)ts_now.tv_sec * 1000000ULL +
                          (uint64_t)ts_now.tv_nsec / 1000ULL;

        if (now_us - last_probe_time > 500000) {
            for (int i = 0; i < tctx->num_paths; i++) {
                sctp_path_t *p = &tctx->paths[i];
                if (p->state != PATH_STATE_DOWN && p->sock_fd >= 0) {
                    sctp_path_send_rtt_probe(p);
                }
            }
            last_probe_time = now_us;
        }

        pthread_mutex_lock(&tctx->global_lock);
        for (int i = 0; i < tctx->num_paths; i++) {
            sctp_path_t *p = &tctx->paths[i];
            path_state_t old_state = p->state;
            check_path_health(p);
            if (p->state != old_state)
                changed = 1;
        }
        pthread_mutex_unlock(&tctx->global_lock);

        if (changed) {
            pthread_mutex_lock(&tctx->global_lock);
            tctx->path_changed = true;
            pthread_mutex_unlock(&tctx->global_lock);
        }

        lb_update_rtt_scheduling(tctx);

        lb_check_predicted_failure(tctx);

        struct timespec ts = {0, 100 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
}

void lb_rebalance(transfer_context_t *tctx)
{
    pthread_mutex_lock(&tctx->global_lock);

    int active = 0;
    for (int i = 0; i < tctx->num_paths; i++) {
        if (tctx->paths[i].state != PATH_STATE_DOWN)
            active++;
    }
    tctx->active_paths = active;

    pthread_mutex_unlock(&tctx->global_lock);
}

void lb_update_rtt_scheduling(transfer_context_t *tctx)
{
    if (tctx->num_paths < 2)
        return;

    uint64_t min_rtt = get_min_rtt(tctx);
    int primary = get_primary_path(tctx);
    uint64_t threshold_us = (uint64_t)tctx->latency_diff_threshold_ms * 1000;

    pthread_mutex_lock(&tctx->global_lock);
    for (int i = 0; i < tctx->num_paths; i++) {
        sctp_path_t *p = &tctx->paths[i];
        if (p->state == PATH_STATE_DOWN)
            continue;

        pthread_mutex_lock(&p->lock);

        uint64_t rtt = p->avg_rtt_us;
        uint64_t rtt_diff = rtt > min_rtt ? (rtt - min_rtt) : 0;

        if (i == primary) {
            p->max_chunk_size = MAX_CHUNK_SIZE;
            p->rate_limit_bps = 0;
        } else if (rtt_diff > threshold_us * 2) {
            p->max_chunk_size = MIN_CHUNK_SIZE;
            p->rate_limit_bps = (uint32_t)(p->avg_bw * 0.3);
        } else if (rtt_diff > threshold_us) {
            double ratio = 1.0 - (double)(rtt_diff - threshold_us) /
                                  (double)threshold_us;
            if (ratio < 0.5) ratio = 0.5;
            p->max_chunk_size = (uint32_t)(MAX_CHUNK_SIZE * ratio);
            if (p->max_chunk_size < MIN_CHUNK_SIZE)
                p->max_chunk_size = MIN_CHUNK_SIZE;
            p->rate_limit_bps = (uint32_t)(p->avg_bw * (0.5 + ratio / 2));
        } else {
            p->max_chunk_size = MAX_CHUNK_SIZE;
            p->rate_limit_bps = 0;
        }

        pthread_mutex_unlock(&p->lock);
    }
    pthread_mutex_unlock(&tctx->global_lock);
}

int lb_check_predicted_failure(transfer_context_t *tctx)
{
    int failing_idx = -1;

    pthread_mutex_lock(&tctx->global_lock);
    for (int i = 0; i < tctx->num_paths; i++) {
        sctp_path_t *p = &tctx->paths[i];
        if (p->state == PATH_STATE_DOWN)
            continue;

        quality_prediction_t pred;
        int ret = quality_predict(&p->quality_history, &pred, PREDICTION_SECONDS);
        if (ret < 0) continue;

        pthread_mutex_lock(&p->lock);
        p->prediction = pred;

        if (pred.will_fail &&
            pred.predicted_loss_rate > LOSS_RATE_THRESHOLD &&
            p->loss_duration_sec >= 3) {
            p->state = PATH_STATE_PREFAIL;
            if (failing_idx == -1)
                failing_idx = i;
        } else if (p->state == PATH_STATE_PREFAIL) {
            p->state = PATH_STATE_HEALTHY;
        }
        pthread_mutex_unlock(&p->lock);
    }
    pthread_mutex_unlock(&tctx->global_lock);

    if (failing_idx >= 0) {
        lb_initiate_switch(tctx, failing_idx);
    }

    return failing_idx;
}

int lb_initiate_switch(transfer_context_t *tctx, int failing_path)
{
    if (failing_path < 0 || failing_path >= tctx->num_paths)
        return -1;

    pthread_mutex_lock(&tctx->switch_lock);

    if (tctx->switch_state != SWITCH_STATE_NORMAL) {
        pthread_mutex_unlock(&tctx->switch_lock);
        return 0;
    }

    printf("\n[WARNING] Path %d predicted to fail, initiating switch...\n",
           failing_path);

    tctx->switch_state = SWITCH_STATE_PREPARE;
    tctx->failing_path_idx = failing_path;
    tctx->switch_start_time_us = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    tctx->switch_start_time_us = (uint64_t)ts.tv_sec * 1000000ULL +
                                  (uint64_t)ts.tv_nsec / 1000ULL;

    sctp_path_t *p = &tctx->paths[failing_path];
    pthread_mutex_lock(&p->lock);
    p->state = PATH_STATE_SWITCHING;
    p->max_chunk_size = MIN_CHUNK_SIZE;
    p->rate_limit_bps = (uint32_t)(p->avg_bw * 0.5);
    pthread_mutex_unlock(&p->lock);

    tctx->switch_state = SWITCH_STATE_FEC;

    pthread_mutex_unlock(&tctx->switch_lock);
    return 0;
}

void lb_complete_switch(transfer_context_t *tctx)
{
    pthread_mutex_lock(&tctx->switch_lock);

    if (tctx->switch_state == SWITCH_STATE_NORMAL) {
        pthread_mutex_unlock(&tctx->switch_lock);
        return;
    }

    if (tctx->failing_path_idx >= 0 &&
        tctx->failing_path_idx < tctx->num_paths) {
        sctp_path_t *p = &tctx->paths[tctx->failing_path_idx];
        pthread_mutex_lock(&p->lock);
        if (p->state == PATH_STATE_PREFAIL ||
            p->state == PATH_STATE_SWITCHING) {
            p->state = PATH_STATE_DEGRADED;
        }
        p->rate_limit_bps = 0;
        p->max_chunk_size = MAX_CHUNK_SIZE;
        pthread_mutex_unlock(&p->lock);
    }

    tctx->switch_state = SWITCH_STATE_NORMAL;
    tctx->failing_path_idx = -1;
    tctx->switch_start_time_us = 0;

    pthread_mutex_unlock(&tctx->switch_lock);

    printf("\n[INFO] Switch completed, traffic restored\n");
}
