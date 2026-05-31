#include "sctp_transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <time.h>

int sctp_path_init(sctp_path_t *path, int path_id,
                   const char *local_ip, uint16_t local_port,
                   const char *remote_ip, uint16_t remote_port,
                   const char *name)
{
    memset(path, 0, sizeof(*path));
    path->path_id = path_id;
    path->sock_fd = -1;
    strncpy(path->name, name, MAX_PATH_NAME_LEN - 1);
    strncpy(path->local_ip, local_ip, MAX_IP_LEN - 1);
    strncpy(path->remote_ip, remote_ip, MAX_IP_LEN - 1);
    path->local_port = local_port;
    path->remote_port = remote_port;
    path->state = PATH_STATE_HEALTHY;
    path->window_size = 1024 * 1024;
    path->avg_bw = 10 * 1024 * 1024;
    path->avg_rtt_us = 50000;
    path->min_rtt_us = ~0ULL;
    path->max_rtt_us = 0;
    path->rtt_probe_counter = 0;
    path->max_chunk_size = MAX_CHUNK_SIZE;
    path->rate_limit_bps = 0;
    path->last_send_time_us = 0;
    path->jitter_us = 0;
    path->last_rtt_us = 0;
    path->packets_sent = 0;
    path->packets_lost = 0;
    path->loss_rate = 0.0f;
    path->loss_start_time_us = 0;
    path->loss_duration_sec = 0;
    path->fec_group_counter = 0;
    memset(&path->current_fec_group, 0, sizeof(path->current_fec_group));

    quality_history_init(&path->quality_history);
    memset(&path->prediction, 0, sizeof(path->prediction));

    memset(&path->local_addr, 0, sizeof(path->local_addr));
    path->local_addr.sin_family = AF_INET;
    path->local_addr.sin_port = htons(local_port);
    if (strcmp(local_ip, "0.0.0.0") == 0) {
        path->local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        inet_pton(AF_INET, local_ip, &path->local_addr.sin_addr);
    }

    memset(&path->remote_addr, 0, sizeof(path->remote_addr));
    path->remote_addr.sin_family = AF_INET;
    path->remote_addr.sin_port = htons(remote_port);
    inet_pton(AF_INET, remote_ip, &path->remote_addr.sin_addr);

    pthread_mutex_init(&path->lock, NULL);
    return 0;
}

int sctp_path_set_options(sctp_path_t *path)
{
    int fd = path->sock_fd;
    struct sctp_initmsg initmsg;
    struct sctp_event_subscribe events;
    struct sctp_paddrparams paddr;
    int on = 1;

    if (setsockopt(fd, IPPROTO_SCTP, SCTP_NODELAY, &on, sizeof(on)) < 0) {
        perror("SCTP_NODELAY");
        return -1;
    }

    memset(&initmsg, 0, sizeof(initmsg));
    initmsg.sinit_num_ostreams = 8;
    initmsg.sinit_max_instreams = 8;
    initmsg.sinit_max_attempts = 4;
    initmsg.sinit_max_init_timeo = 60000;
    if (setsockopt(fd, IPPROTO_SCTP, SCTP_INITMSG, &initmsg,
                   sizeof(initmsg)) < 0) {
        perror("SCTP_INITMSG");
        return -1;
    }

    memset(&events, 0, sizeof(events));
    events.sctp_data_io_event = 1;
    events.sctp_association_event = 1;
    events.sctp_send_failure_event = 1;
    events.sctp_peer_error_event = 1;
    events.sctp_shutdown_event = 1;
    events.sctp_adaptation_layer_event = 1;
    events.sctp_partial_delivery_event = 1;
    events.sctp_authentication_event = 1;
    if (setsockopt(fd, IPPROTO_SCTP, SCTP_EVENTS, &events,
                   sizeof(events)) < 0) {
        perror("SCTP_EVENTS");
        return -1;
    }

    memset(&paddr, 0, sizeof(paddr));
    paddr.spp_hbinterval = HEARTBEAT_INTERVAL * 1000;
    paddr.spp_pathmaxrxt = 3;
    paddr.spp_flags = SPP_HB_ENABLE | SPP_HB_DEMAND;
    if (setsockopt(fd, IPPROTO_SCTP, SCTP_PEER_ADDR_PARAMS,
                   &paddr, sizeof(paddr)) < 0) {
        perror("SCTP_PEER_ADDR_PARAMS");
        return -1;
    }

    int buf_size = 2 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags);

    return 0;
}

int sctp_path_connect(sctp_path_t *path)
{
    path->sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    if (path->sock_fd < 0) {
        perror("socket");
        return -1;
    }

    if (sctp_path_set_options(path) < 0) {
        close(path->sock_fd);
        path->sock_fd = -1;
        return -1;
    }

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(path->local_port);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(path->sock_fd, (struct sockaddr *)&bind_addr,
             sizeof(bind_addr)) < 0) {
        if (errno != EADDRINUSE) {
            perror("bind");
            close(path->sock_fd);
            path->sock_fd = -1;
            return -1;
        }
    }

    if (connect(path->sock_fd, (struct sockaddr *)&path->remote_addr,
                sizeof(path->remote_addr)) < 0) {
        perror("connect");
        close(path->sock_fd);
        path->sock_fd = -1;
        return -1;
    }

    return 0;
}

int sctp_path_bind(sctp_path_t *path)
{
    path->sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    if (path->sock_fd < 0) {
        perror("socket");
        return -1;
    }

    int on = 1;
    setsockopt(path->sock_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(path->local_port);
    inet_pton(AF_INET, path->local_ip, &bind_addr.sin_addr);

    if (bind(path->sock_fd, (struct sockaddr *)&bind_addr,
             sizeof(bind_addr)) < 0) {
        perror("bind");
        close(path->sock_fd);
        path->sock_fd = -1;
        return -1;
    }

    if (sctp_path_set_options(path) < 0) {
        close(path->sock_fd);
        path->sock_fd = -1;
        return -1;
    }

    return 0;
}

int sctp_path_listen(sctp_path_t *path)
{
    if (listen(path->sock_fd, 8) < 0) {
        perror("listen");
        return -1;
    }
    return 0;
}

int sctp_path_accept(sctp_path_t *path, sctp_path_t *client_path)
{
    socklen_t addr_len = sizeof(client_path->local_addr);
    int client_fd = accept(path->sock_fd,
                           (struct sockaddr *)&client_path->local_addr,
                           &addr_len);
    if (client_fd < 0) {
        perror("accept");
        return -1;
    }

    client_path->sock_fd = client_fd;

    char ip_str[MAX_IP_LEN];
    inet_ntop(AF_INET, &client_path->local_addr.sin_addr,
              ip_str, sizeof(ip_str));
    strncpy(client_path->remote_ip, ip_str, MAX_IP_LEN - 1);
    client_path->remote_port = ntohs(client_path->local_addr.sin_port);

    struct sctp_event_subscribe events;
    memset(&events, 0, sizeof(events));
    events.sctp_data_io_event = 1;
    events.sctp_association_event = 1;
    events.sctp_send_failure_event = 1;
    events.sctp_peer_error_event = 1;
    events.sctp_shutdown_event = 1;
    events.sctp_adaptation_layer_event = 1;
    events.sctp_partial_delivery_event = 1;
    setsockopt(client_fd, IPPROTO_SCTP, SCTP_EVENTS, &events,
               sizeof(events));

    int on = 1;
    setsockopt(client_fd, IPPROTO_SCTP, SCTP_NODELAY, &on, sizeof(on));

    return 0;
}

ssize_t sctp_path_sendmsg(sctp_path_t *path, const void *data, size_t len,
                           uint16_t stream, uint32_t ppid)
{
    struct sctp_sndrcvinfo sinfo;
    memset(&sinfo, 0, sizeof(sinfo));
    sinfo.sinfo_stream = stream;
    sinfo.sinfo_ppid = htonl(ppid);
    sinfo.sinfo_flags = SCTP_UNORDERED | SCTP_EOF;
    sinfo.sinfo_timetolive = 0;

    ssize_t ret = sctp_sendmsg(path->sock_fd, data, len,
                               NULL, 0, htonl(ppid), 0,
                               stream, 0, 0);
    if (ret < 0) {
        if (errno != EPIPE && errno != ECONNRESET)
            perror("sctp_sendmsg");
    }
    return ret;
}

ssize_t sctp_path_recvmsg(sctp_path_t *path, void *data, size_t max_len,
                           struct sctp_sndrcvinfo *sinfo, int *msg_flags)
{
    socklen_t sinfo_len = sizeof(*sinfo);
    return sctp_recvmsg(path->sock_fd, data, max_len,
                        NULL, 0, sinfo, msg_flags);
}

void sctp_path_close(sctp_path_t *path)
{
    if (path->sock_fd >= 0) {
        close(path->sock_fd);
        path->sock_fd = -1;
    }
    path->state = PATH_STATE_DOWN;
    pthread_mutex_destroy(&path->lock);
}

void sctp_path_update_bw(sctp_path_t *path, uint64_t bytes_sent,
                          struct timespec *start_time)
{
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double elapsed = (end_time.tv_sec - start_time->tv_sec) +
                     (end_time.tv_nsec - start_time->tv_nsec) / 1e9;

    if (elapsed <= 0)
        return;

    uint64_t instant_bw = (uint64_t)(bytes_sent / elapsed);

    pthread_mutex_lock(&path->lock);
    path->bw_samples[path->bw_sample_idx] = instant_bw;
    path->bw_sample_idx = (path->bw_sample_idx + 1) % BW_SAMPLE_WINDOW;

    uint64_t sum = 0;
    int count = 0;
    for (int i = 0; i < BW_SAMPLE_WINDOW; i++) {
        if (path->bw_samples[i] > 0) {
            sum += path->bw_samples[i];
            count++;
        }
    }
    if (count > 0)
        path->avg_bw = sum / count;
    path->speed_bps = instant_bw;
    pthread_mutex_unlock(&path->lock);
}

void sctp_path_set_state(sctp_path_t *path, path_state_t state)
{
    pthread_mutex_lock(&path->lock);
    path->state = state;
    pthread_mutex_unlock(&path->lock);
}

static inline uint64_t get_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

void sctp_path_update_rtt(sctp_path_t *path, uint64_t rtt_us)
{
    if (rtt_us == 0)
        return;

    pthread_mutex_lock(&path->lock);

    path->rtt_samples_us[path->rtt_sample_idx] = rtt_us;
    path->rtt_sample_idx = (path->rtt_sample_idx + 1) % RTT_SAMPLE_WINDOW;

    if (rtt_us < path->min_rtt_us)
        path->min_rtt_us = rtt_us;
    if (rtt_us > path->max_rtt_us)
        path->max_rtt_us = rtt_us;

    uint64_t sum = 0;
    int count = 0;
    for (int i = 0; i < RTT_SAMPLE_WINDOW; i++) {
        if (path->rtt_samples_us[i] > 0) {
            sum += path->rtt_samples_us[i];
            count++;
        }
    }
    if (count > 0)
        path->avg_rtt_us = sum / count;

    pthread_mutex_unlock(&path->lock);
}

int sctp_path_send_rtt_probe(sctp_path_t *path)
{
    rtt_probe_t probe;
    memset(&probe, 0, sizeof(probe));

    pthread_mutex_lock(&path->lock);
    probe.probe_id = path->rtt_probe_counter++;
    pthread_mutex_unlock(&path->lock);

    probe.send_timestamp_us = get_time_us();

    return msg_send(path->sock_fd, MSG_RTT_PROBE, &probe, sizeof(probe),
                    (uint16_t)(path->path_id % 8), 0);
}

int sctp_path_handle_rtt_probe_ack(sctp_path_t *path, rtt_probe_ack_t *ack)
{
    if (!ack)
        return -1;

    uint64_t now = get_time_us();
    uint64_t rtt_us = now - ack->send_timestamp_us;

    sctp_path_update_rtt(path, rtt_us);

    return 0;
}

void sctp_path_update_loss(sctp_path_t *path, bool lost)
{
    if (!path) return;

    pthread_mutex_lock(&path->lock);
    path->packets_sent++;
    if (lost) {
        path->packets_lost++;
        if (path->loss_start_time_us == 0)
            path->loss_start_time_us = get_time_us();
        else {
            uint64_t elapsed = get_time_us() - path->loss_start_time_us;
            path->loss_duration_sec = (int)(elapsed / 1000000);
        }
    }

    if (path->packets_sent > 100) {
        path->loss_rate = (float)path->packets_lost / path->packets_sent * 100.0f;
        if (path->loss_rate > 100.0f) path->loss_rate = 100.0f;
        if (!lost && path->loss_rate < 1.0f) {
            path->loss_start_time_us = 0;
            path->loss_duration_sec = 0;
        }
    }

    quality_sample_t sample;
    memset(&sample, 0, sizeof(sample));
    sample.timestamp_us = get_time_us();
    sample.loss_rate = path->loss_rate;
    sample.rtt_us = path->avg_rtt_us;
    sample.jitter_us = path->jitter_us;
    sample.bandwidth_bps = path->avg_bw;
    quality_history_add(&path->quality_history, &sample);

    pthread_mutex_unlock(&path->lock);
}
