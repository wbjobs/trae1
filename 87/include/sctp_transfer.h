#ifndef SCTP_TRANSFER_H
#define SCTP_TRANSFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include <netinet/sctp.h>
#include <netinet/in.h>
#include <pthread.h>

#define MAX_PATHS               4
#define MAX_PATH_NAME_LEN       32
#define MAX_CHUNK_SIZE          (64 * 1024)
#define MIN_CHUNK_SIZE          (4 * 1024)
#define SCTP_PORT_BASE          9000
#define MAX_FILENAME_LEN        256
#define MAX_IP_LEN              64
#define MAX_META_LEN            512
#define PROGRESS_UPDATE_INTERVAL 200000
#define BW_SAMPLE_WINDOW        5
#define PATH_HEALTH_TIMEOUT     3
#define ACK_TIMEOUT_SEC         30
#define MAX_RETRIES             5
#define HEARTBEAT_INTERVAL      2

#define RTT_SAMPLE_WINDOW       10
#define DEFAULT_LATENCY_DIFF_MS 40
#define REORDER_BUFFER_SIZE     500
#define NACK_TIMEOUT_MS         200
#define MAX_NACK_RETRIES        3

#define QUALITY_SAMPLE_COUNT    60
#define PREDICTION_SECONDS      5
#define LOSS_RATE_THRESHOLD     5.0
#define PREDICTION_THRESHOLD    3.0
#define FEC_DATA_SHARDS         4
#define FEC_PARITY_SHARDS       2
#define FEC_SHARD_SIZE          (MAX_CHUNK_SIZE / FEC_DATA_SHARDS)

#define PLOT_WIDTH              60
#define PLOT_HEIGHT             10
#define PLOT_SAMPLES            PLOT_WIDTH

#define MAGIC_NUMBER            0x53435450
#define PROTOCOL_VERSION        1

typedef enum {
    MSG_HANDSHAKE       = 1,
    MSG_HANDSHAKE_ACK   = 2,
    MSG_FILE_META       = 3,
    MSG_FILE_META_ACK   = 4,
    MSG_CHUNK           = 5,
    MSG_CHUNK_ACK       = 6,
    MSG_RESUME_REQ      = 7,
    MSG_RESUME_RESP     = 8,
    MSG_COMPLETE        = 9,
    MSG_COMPLETE_ACK    = 10,
    MSG_HEARTBEAT       = 11,
    MSG_ERROR           = 12,
    MSG_NACK            = 13,
    MSG_RTT_PROBE       = 14,
    MSG_RTT_PROBE_ACK   = 15,
    MSG_FEC_PARITY      = 16,
    MSG_PATH_STATUS     = 17
} msg_type_t;

typedef enum {
    PATH_STATE_HEALTHY  = 0,
    PATH_STATE_SLOW     = 1,
    PATH_STATE_DEGRADED = 2,
    PATH_STATE_DOWN     = 3,
    PATH_STATE_PREFAIL  = 4,
    PATH_STATE_SWITCHING = 5
} path_state_t;

typedef enum {
    SWITCH_STATE_NORMAL = 0,
    SWITCH_STATE_PREPARE = 1,
    SWITCH_STATE_FEC     = 2,
    SWITCH_STATE_ACTIVE  = 3,
    SWITCH_STATE_RESTORE = 4
} switch_state_t;

typedef struct {
    uint32_t        magic;
    uint8_t         version;
    uint8_t         msg_type;
    uint16_t        flags;
    uint32_t        length;
    uint32_t        seq_num;
    uint32_t        crc32c;
} __attribute__((packed)) msg_header_t;

typedef struct {
    char            filename[MAX_FILENAME_LEN];
    uint64_t        file_size;
    uint32_t        chunk_size;
    uint32_t        total_chunks;
    uint32_t        crc32c;
} __attribute__((packed)) file_meta_t;

typedef struct {
    uint32_t        chunk_id;
    uint32_t        offset;
    uint32_t        data_len;
    uint8_t         data[];
} __attribute__((packed)) chunk_payload_t;

typedef struct {
    uint32_t        chunk_id;
    uint8_t         success;
} __attribute__((packed)) chunk_ack_t;

typedef struct {
    uint32_t        start_chunk;
    uint32_t        end_chunk;
    uint32_t        missing_count;
    uint32_t        missing_ids[];
} __attribute__((packed)) nack_payload_t;

typedef struct {
    uint32_t        probe_id;
    uint64_t        send_timestamp_us;
} __attribute__((packed)) rtt_probe_t;

typedef struct {
    uint32_t        probe_id;
    uint64_t        send_timestamp_us;
    uint64_t        recv_timestamp_us;
} __attribute__((packed)) rtt_probe_ack_t;

typedef struct {
    uint32_t        chunk_group_id;
    uint8_t         data_shards;
    uint8_t         parity_shards;
    uint8_t         parity_idx;
    uint32_t        chunk_ids[FEC_DATA_SHARDS];
    uint8_t         parity_data[FEC_SHARD_SIZE];
} __attribute__((packed)) fec_parity_t;

typedef struct {
    uint32_t        path_id;
    uint64_t        timestamp_us;
    float           loss_rate;
    uint64_t        rtt_us;
    uint64_t        jitter_us;
    uint64_t        bandwidth_bps;
} __attribute__((packed)) path_status_t;

typedef struct {
    uint64_t        timestamp_us;
    float           loss_rate;
    uint64_t        rtt_us;
    uint64_t        jitter_us;
    uint64_t        bandwidth_bps;
} quality_sample_t;

typedef struct {
    quality_sample_t samples[QUALITY_SAMPLE_COUNT];
    int             head;
    int             count;
    pthread_mutex_t lock;
} quality_history_t;

typedef struct {
    float           predicted_loss_rate;
    uint64_t        predicted_rtt_us;
    uint64_t        predicted_bandwidth_bps;
    float           confidence;
    bool            will_fail;
    uint64_t        prediction_time_us;
} quality_prediction_t;

typedef struct {
    uint8_t         data_shards;
    uint8_t         parity_shards;
    uint32_t        group_id;
    uint32_t        chunk_ids[FEC_DATA_SHARDS];
    uint8_t         *data[FEC_DATA_SHARDS];
    size_t          data_len[FEC_DATA_SHARDS];
    uint8_t         *parity[FEC_PARITY_SHARDS];
    int             received_count;
    bool            complete;
} fec_group_t;

typedef struct {
    uint64_t        received_bytes;
    uint32_t        received_chunks;
    uint32_t        *received_bitmap;
} resume_state_t;

typedef struct {
    uint32_t        chunk_id;
    uint8_t         *data;
    size_t          data_len;
    uint64_t        recv_timestamp_us;
    int             path_id;
    bool            valid;
} reorder_entry_t;

typedef struct {
    reorder_entry_t entries[REORDER_BUFFER_SIZE];
    uint32_t        head;
    uint32_t        tail;
    uint32_t        count;
    uint32_t        next_expected_chunk;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
} reorder_buffer_t;

typedef struct {
    double          data[PLOT_SAMPLES];
    int             head;
    int             count;
    char            label[32];
    uint32_t        color;
} plot_series_t;

typedef struct {
    plot_series_t   series[MAX_PATHS + 1];
    int             num_series;
    bool            enabled;
    pthread_mutex_t lock;
} plot_graph_t;

typedef struct {
    int                 sock_fd;
    int                 path_id;
    char                name[MAX_PATH_NAME_LEN];
    char                local_ip[MAX_IP_LEN];
    char                remote_ip[MAX_IP_LEN];
    uint16_t            local_port;
    uint16_t            remote_port;
    path_state_t        state;
    pthread_mutex_t     lock;

    uint64_t            bytes_sent;
    uint64_t            bytes_acked;
    uint64_t            speed_bps;
    uint64_t            window_size;
    uint32_t            in_flight;

    uint64_t            bw_samples[BW_SAMPLE_WINDOW];
    int                 bw_sample_idx;
    uint64_t            avg_bw;

    uint64_t            rtt_samples_us[RTT_SAMPLE_WINDOW];
    int                 rtt_sample_idx;
    uint64_t            avg_rtt_us;
    uint64_t            min_rtt_us;
    uint64_t            max_rtt_us;
    uint64_t            jitter_us;
    uint32_t            rtt_probe_counter;
    uint64_t            last_rtt_us;

    uint64_t            packets_sent;
    uint64_t            packets_lost;
    float               loss_rate;
    uint64_t            loss_start_time_us;
    int                 loss_duration_sec;

    quality_history_t   quality_history;
    quality_prediction_t prediction;

    uint32_t            max_chunk_size;
    uint32_t            rate_limit_bps;
    uint64_t            last_send_time_us;

    uint32_t            fec_group_counter;
    fec_group_t         current_fec_group;

    struct sockaddr_in  local_addr;
    struct sockaddr_in  remote_addr;
    struct sctp_paddrparams paddr_params;
} sctp_path_t;

typedef struct {
    char                filename[MAX_FILENAME_LEN];
    uint64_t            file_size;
    uint32_t            chunk_size;
    uint32_t            total_chunks;
    uint32_t            file_crc32c;

    int                 fd;
    uint8_t             *chunk_map;
    uint32_t            next_chunk_to_send;
    uint32_t            next_chunk_to_recv;
    uint64_t            total_sent;
    uint64_t            total_received;

    pthread_mutex_t     file_lock;
    bool                transfer_complete;
    bool                transfer_failed;
} file_context_t;

typedef struct {
    sctp_path_t         paths[MAX_PATHS];
    int                 num_paths;
    int                 active_paths;

    file_context_t      file_ctx;

    uint32_t            base_chunk_size;
    uint64_t            total_start_time;

    uint32_t            latency_diff_threshold_ms;

    switch_state_t      switch_state;
    int                 failing_path_idx;
    uint64_t            switch_start_time_us;
    pthread_mutex_t     switch_lock;

    plot_graph_t        plot_graph;
    bool                plot_enabled;

    pthread_t           monitor_thread;
    pthread_t           display_thread;
    pthread_mutex_t     global_lock;

    bool                is_sender;
    bool                reverse_mode;
    bool                resume_enabled;
    char                resume_file[MAX_FILENAME_LEN];

    volatile bool       running;
    volatile bool       path_changed;
} transfer_context_t;

typedef struct {
    uint64_t    total_bytes;
    uint64_t    total_speed;
    int         num_active;
    int         num_total;
    struct {
        char    name[MAX_PATH_NAME_LEN];
        uint64_t bytes;
        uint64_t speed;
        uint64_t rtt_us;
        uint64_t jitter_us;
        float   loss_rate;
        path_state_t state;
        quality_prediction_t prediction;
    } paths[MAX_PATHS];
    double      progress_pct;
    uint32_t    reorder_buffer_count;
    switch_state_t switch_state;
} transfer_stats_t;

int         crc32c_init(void);
uint32_t    crc32c_compute(const void *data, size_t length);
uint32_t    crc32c_update(uint32_t crc, const void *data, size_t length);

int         sctp_path_init(sctp_path_t *path, int path_id,
                           const char *local_ip, uint16_t local_port,
                           const char *remote_ip, uint16_t remote_port,
                           const char *name);
int         sctp_path_connect(sctp_path_t *path);
int         sctp_path_bind(sctp_path_t *path);
int         sctp_path_listen(sctp_path_t *path);
int         sctp_path_accept(sctp_path_t *path, sctp_path_t *client_path);
ssize_t     sctp_path_sendmsg(sctp_path_t *path, const void *data, size_t len,
                               uint16_t stream, uint32_t ppid);
ssize_t     sctp_path_recvmsg(sctp_path_t *path, void *data, size_t max_len,
                               struct sctp_sndrcvinfo *sinfo, int *msg_flags);
int         sctp_path_set_options(sctp_path_t *path);
void        sctp_path_close(sctp_path_t *path);
void        sctp_path_update_bw(sctp_path_t *path, uint64_t bytes_sent,
                                 struct timespec *start_time);
void        sctp_path_set_state(sctp_path_t *path, path_state_t state);
void        sctp_path_update_rtt(sctp_path_t *path, uint64_t rtt_us);
int         sctp_path_send_rtt_probe(sctp_path_t *path);
int         sctp_path_handle_rtt_probe_ack(sctp_path_t *path,
                                             rtt_probe_ack_t *ack);
void        sctp_path_update_loss(sctp_path_t *path, bool lost);

int         file_ctx_open_send(file_context_t *ctx, const char *filename);
int         file_ctx_open_recv(file_context_t *ctx, const char *filename,
                                uint64_t file_size, uint32_t chunk_size);
int         file_ctx_read_chunk(file_context_t *ctx, uint32_t chunk_id,
                                 void *buf, size_t *len);
int         file_ctx_write_chunk(file_context_t *ctx, uint32_t chunk_id,
                                  const void *buf, size_t len);
int         file_ctx_compute_crc(file_context_t *ctx, uint32_t *out_crc);
void        file_ctx_close(file_context_t *ctx);
bool        file_ctx_all_chunks_received(file_context_t *ctx);
bool        file_ctx_chunk_received(file_context_t *ctx, uint32_t chunk_id);

int         msg_send(int sock_fd, uint8_t msg_type, const void *payload,
                      size_t payload_len, uint16_t stream, uint32_t ppid);
int         msg_recv(int sock_fd, msg_header_t *hdr, void *payload,
                      size_t max_payload, struct sctp_sndrcvinfo *sinfo);
int         msg_send_file_meta(int sock_fd, const file_context_t *ctx,
                                uint16_t stream);
int         msg_recv_file_meta(int sock_fd, file_context_t *ctx,
                                struct sctp_sndrcvinfo *sinfo);
int         msg_send_chunk(int sock_fd, uint32_t chunk_id, const void *data,
                            size_t len, uint16_t stream);
int         msg_recv_chunk(int sock_fd, uint32_t *chunk_id, void *data,
                            size_t max_len, struct sctp_sndrcvinfo *sinfo);
int         msg_send_nack(int sock_fd, const uint32_t *missing_ids,
                           uint32_t count, uint16_t stream);
int         msg_send_fec_parity(int sock_fd, const fec_parity_t *fec,
                                 uint16_t stream);
int         msg_recv_fec_parity(int sock_fd, fec_parity_t *fec,
                                 struct sctp_sndrcvinfo *sinfo);

void        reorder_buffer_init(reorder_buffer_t *rb);
void        reorder_buffer_destroy(reorder_buffer_t *rb);
int         reorder_buffer_insert(reorder_buffer_t *rb, uint32_t chunk_id,
                                   const void *data, size_t len,
                                   int path_id);
int         reorder_buffer_get_next(reorder_buffer_t *rb, uint32_t *chunk_id,
                                     void *data, size_t max_len, int *path_id);
void        reorder_buffer_check_timeout(reorder_buffer_t *rb,
                                        uint64_t timeout_us,
                                        uint32_t *missing_ids,
                                        uint32_t *missing_count,
                                        uint32_t max_missing);
uint32_t    reorder_buffer_get_count(reorder_buffer_t *rb);

void        quality_history_init(quality_history_t *qh);
void        quality_history_add(quality_history_t *qh,
                                 const quality_sample_t *sample);
int         quality_history_get_samples(quality_history_t *qh,
                                         quality_sample_t *out,
                                         int max_samples);
int         quality_predict(quality_history_t *qh,
                             quality_prediction_t *pred,
                             int predict_seconds);

int         fec_encode(const uint8_t **data, size_t data_len,
                        uint8_t **parity, size_t *parity_len,
                        int num_data, int num_parity);
int         fec_decode(uint8_t **data, int *erasures, int num_erasures,
                        uint8_t **parity, size_t data_len,
                        int num_data, int num_parity);
void        fec_group_init(fec_group_t *group, uint32_t group_id,
                            int data_shards, int parity_shards);
void        fec_group_free(fec_group_t *group);
int         fec_group_add_data(fec_group_t *group, uint32_t chunk_id,
                                const uint8_t *data, size_t len);
int         fec_group_generate_parity(fec_group_t *group);
int         fec_group_restore(fec_group_t *group, uint32_t missing_idx);

void        plot_graph_init(plot_graph_t *pg, bool enabled);
void        plot_graph_add_series(plot_graph_t *pg, const char *label,
                                   uint32_t color);
void        plot_graph_add_data(plot_graph_t *pg, int series_idx,
                                 double value);
void        plot_graph_render(plot_graph_t *pg);
void        plot_graph_destroy(plot_graph_t *pg);

uint32_t    lb_get_chunk_size(transfer_context_t *tctx, int path_id);
int         lb_select_path(transfer_context_t *tctx, uint32_t chunk_id);
void        lb_monitor_paths(transfer_context_t *tctx);
void        lb_rebalance(transfer_context_t *tctx);
void        lb_update_rtt_scheduling(transfer_context_t *tctx);
int         lb_check_predicted_failure(transfer_context_t *tctx);
int         lb_initiate_switch(transfer_context_t *tctx, int failing_path);
void        lb_complete_switch(transfer_context_t *tctx);

void        display_init(bool is_sender);
void        display_update(const transfer_stats_t *stats);
void        display_final(const transfer_stats_t *stats, bool success);
void        display_shutdown(void);
void        display_plot_if_enabled(transfer_context_t *tctx);

int         resume_save_state(const char *resume_file,
                                const file_context_t *ctx);
int         resume_load_state(const char *resume_file,
                                file_context_t *ctx);

int         sender_run(const char *filename,
                        const char *remote_addr, int port,
                        const char *const *local_addrs, int num_local,
                        bool reverse, bool resume,
                        uint32_t latency_diff_ms, bool plot_graph);
int         receiver_run(const char *output_dir,
                          const char *bind_addr, int port,
                          bool allow_reverse, bool resume,
                          uint32_t latency_diff_ms, bool plot_graph);

#endif
