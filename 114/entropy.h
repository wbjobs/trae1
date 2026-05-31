#ifndef ENTROPY_H
#define ENTROPY_H

#include "common.h"

#define ENTROPY_WINDOW_SIZE 4096
#define ENTROPY_HISTORY_SIZE 100
#define ENTROPY_THRESHOLD 0.9

typedef struct {
    unsigned char window[ENTROPY_WINDOW_SIZE];
    size_t window_pos;
    size_t window_count;
    double entropy_history[ENTROPY_HISTORY_SIZE];
    size_t history_pos;
    double avg_entropy;
    double max_entropy;
    double min_entropy;
    uint64_t total_bytes_processed;
    uint64_t high_entropy_chunks;
    pthread_mutex_t lock;
} EntropyMonitor;

double calculate_shannon_entropy(const unsigned char *data, size_t length);
int entropy_monitor_init(EntropyMonitor *monitor);
int entropy_monitor_update(EntropyMonitor *monitor, const unsigned char *data, size_t length);
double entropy_monitor_get_current(EntropyMonitor *monitor);
double entropy_monitor_get_average(EntropyMonitor *monitor);
double entropy_monitor_get_trend(EntropyMonitor *monitor);
bool entropy_monitor_is_suspicious(EntropyMonitor *monitor, double threshold);
void entropy_monitor_reset(EntropyMonitor *monitor);
void entropy_monitor_destroy(EntropyMonitor *monitor);

#endif
