#ifndef DB_DISPATCHER_H
#define DB_DISPATCHER_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/time.h>
#include <stdint.h>
#include <math.h>

#define MAX_SLAVES 10
#define MAX_CLIENTS 1024
#define HEALTH_CHECK_INTERVAL 3
#define MAX_REPLICATION_LAG 60
#define RECOVERY_CHECK_COUNT 3
#define RECOVERY_LAG_THRESHOLD 10
#define WEIGHT_RAMPUP_MINUTES 5
#define CONFIG_FILE "db_dispatcher.conf"
#define IP_DB_FILE "ip_db.dat"
#define PREDICTION_HORIZON 30
#define MODEL_RETRAIN_INTERVAL 600
#define METRICS_HISTORY_SIZE 120
#define MIN_WEIGHT_RATIO 0.20
#define CPU_THRESHOLD 80.0

typedef enum {
    REGION_BEIJING,
    REGION_SHANGHAI,
    REGION_SHENZHEN,
    REGION_CHENGDU,
    REGION_UNKNOWN
} region_t;

typedef enum {
    DB_TYPE_MASTER,
    DB_TYPE_SLAVE
} db_type_t;

typedef enum {
    HEALTH_OK,
    HEALTH_DEGRADED,
    HEALTH_DOWN
} health_status_t;

typedef struct {
    double values[METRICS_HISTORY_SIZE];
    int head;
    int count;
    time_t timestamps[METRICS_HISTORY_SIZE];
} time_series_t;

typedef struct {
    double ar_coeffs[3];
    double ma_coeffs[3];
    int p;
    int d;
    int q;
    double intercept;
    double variance;
    time_t last_trained;
} arima_model_t;

typedef struct {
    time_series_t qps;
    time_series_t cpu_usage;
    time_series_t io_wait;
    time_series_t replication_lag;
    arima_model_t qps_model;
    arima_model_t cpu_model;
    arima_model_t lag_model;
    double predicted_qps[PREDICTION_HORIZON];
    double predicted_cpu[PREDICTION_HORIZON];
    double predicted_lag[PREDICTION_HORIZON];
    time_t last_metrics_collected;
    time_t last_model_updated;
    uint64_t query_count;
    time_t query_count_start;
    double current_cpu;
    double current_io_wait;
    int metrics_collected;
} slave_metrics_t;

typedef struct {
    char ip[64];
    int port;
    char user[64];
    char password[128];
    db_type_t type;
    region_t region;
    int weight;
    int current_weight;
    int original_weight;
    int target_weight;
    int weight_last_adjusted;
    health_status_t health;
    int64_t seconds_behind_master;
    int io_thread_ok;
    int sql_thread_ok;
    int64_t last_check_time;
    int recovery_consecutive_ok;
    int64_t recovery_start_time;
    int in_recovery;
    slave_metrics_t metrics;
    pthread_mutex_t conn_mutex;
    int active_connections;
    uint64_t total_queries;
    uint64_t query_errors;
    double avg_latency_ms;
    double min_latency_ms;
    double max_latency_ms;
    uint64_t latency_samples;
    double latency_sum;
} db_server_t;

typedef struct {
    char ip[32];
    region_t region;
    struct in_addr addr;
} ip_geo_entry_t;

typedef struct {
    char sql[1024];
    db_type_t target_type;
    struct sockaddr_in client_addr;
    struct timespec start_time;
} query_record_t;

typedef struct {
    db_server_t master;
    db_server_t slaves[MAX_SLAVES];
    int slave_count;
    ip_geo_entry_t* ip_geo_db;
    int ip_geo_count;
    pthread_mutex_t stats_mutex;
    uint64_t total_reads;
    uint64_t total_writes;
    uint64_t redirected_reads;
    uint64_t dropped_connections;
    int running;
    int auto_recovery;
    int adaptive_load_balancing;
    int dry_run;
    int master_fd;
    struct epoll_event events[MAX_CLIENTS];
} dispatcher_ctx_t;

int load_config(const char* config_file, dispatcher_ctx_t* ctx);
int init_ip_geo_db(const char* db_file, dispatcher_ctx_t* ctx);
region_t get_region_by_ip(const char* client_ip, dispatcher_ctx_t* ctx);
db_server_t* select_slave_by_weight(dispatcher_ctx_t* ctx, region_t preferred_region);
int health_check_slave(db_server_t* slave);
int check_recovery_condition(db_server_t* slave);
int try_recover_slave(db_server_t* slave);
void update_weight_rampup(db_server_t* slave);
void* health_check_thread(void* arg);
int route_query(dispatcher_ctx_t* ctx, const char* sql, struct sockaddr_in* client_addr, db_server_t** target_server);
int execute_query(db_server_t* server, const char* sql, char* result, size_t result_len);
void print_statistics(dispatcher_ctx_t* ctx);
int init_dispatcher(dispatcher_ctx_t* ctx);
void cleanup_dispatcher(dispatcher_ctx_t* ctx);
int setup_lvs_dr(dispatcher_ctx_t* ctx);
int setup_keepalived_config(dispatcher_ctx_t* ctx);
void init_time_series(time_series_t* ts);
void add_to_time_series(time_series_t* ts, double value, time_t timestamp);
double get_series_average(time_series_t* ts, int n);
double get_series_variance(time_series_t* ts, int n);
void train_arima_model(arima_model_t* model, time_series_t* ts);
void arima_predict(arima_model_t* model, time_series_t* ts, double* predictions, int horizon);
void collect_metrics(db_server_t* slave, dispatcher_ctx_t* ctx);
void* metrics_collection_thread(void* arg);
int predict_bottleneck(db_server_t* slave);
void adjust_slave_weight(db_server_t* slave, dispatcher_ctx_t* ctx);
void redistribute_weights(dispatcher_ctx_t* ctx);
void* model_training_thread(void* arg);

#endif
