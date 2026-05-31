#include "entropy.h"
#include <math.h>

double calculate_shannon_entropy(const unsigned char *data, size_t length) {
    if (length == 0 || data == NULL) {
        return 0.0;
    }

    size_t freq[256] = {0};
    for (size_t i = 0; i < length; i++) {
        freq[data[i]]++;
    }

    double entropy = 0.0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            double p = (double)freq[i] / (double)length;
            entropy -= p * log2(p);
        }
    }

    return entropy / 8.0;
}

int entropy_monitor_init(EntropyMonitor *monitor) {
    memset(monitor, 0, sizeof(EntropyMonitor));
    monitor->min_entropy = 1.0;
    
    if (pthread_mutex_init(&monitor->lock, NULL) != 0) {
        return -1;
    }
    
    return 0;
}

static void update_history(EntropyMonitor *monitor, double new_entropy) {
    monitor->entropy_history[monitor->history_pos] = new_entropy;
    monitor->history_pos = (monitor->history_pos + 1) % ENTROPY_HISTORY_SIZE;
    
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < ENTROPY_HISTORY_SIZE; i++) {
        if (monitor->entropy_history[i] > 0) {
            sum += monitor->entropy_history[i];
            count++;
        }
    }
    monitor->avg_entropy = count > 0 ? sum / count : 0;
    
    if (new_entropy > monitor->max_entropy) {
        monitor->max_entropy = new_entropy;
    }
    if (new_entropy < monitor->min_entropy && new_entropy > 0) {
        monitor->min_entropy = new_entropy;
    }
}

int entropy_monitor_update(EntropyMonitor *monitor, const unsigned char *data, size_t length) {
    if (!monitor || !data || length == 0) {
        return -1;
    }

    pthread_mutex_lock(&monitor->lock);

    for (size_t i = 0; i < length; i++) {
        monitor->window[monitor->window_pos] = data[i];
        monitor->window_pos++;
        monitor->window_count++;

        if (monitor->window_pos >= ENTROPY_WINDOW_SIZE) {
            double entropy = calculate_shannon_entropy(monitor->window, ENTROPY_WINDOW_SIZE);
            update_history(monitor, entropy);

            if (entropy > ENTROPY_THRESHOLD) {
                monitor->high_entropy_chunks++;
            }

            monitor->window_pos = 0;
            monitor->total_bytes_processed += ENTROPY_WINDOW_SIZE;
        }
    }

    pthread_mutex_unlock(&monitor->lock);
    return 0;
}

double entropy_monitor_get_current(EntropyMonitor *monitor) {
    if (!monitor) {
        return 0.0;
    }

    pthread_mutex_lock(&monitor->lock);
    double result = monitor->entropy_history[(monitor->history_pos - 1 + ENTROPY_HISTORY_SIZE) % ENTROPY_HISTORY_SIZE];
    pthread_mutex_unlock(&monitor->lock);
    
    return result;
}

double entropy_monitor_get_average(EntropyMonitor *monitor) {
    if (!monitor) {
        return 0.0;
    }

    pthread_mutex_lock(&monitor->lock);
    double result = monitor->avg_entropy;
    pthread_mutex_unlock(&monitor->lock);
    
    return result;
}

double entropy_monitor_get_trend(EntropyMonitor *monitor) {
    if (!monitor) {
        return 0.0;
    }

    pthread_mutex_lock(&monitor->lock);
    
    double recent_avg = 0.0;
    double older_avg = 0.0;
    int recent_count = 0;
    int older_count = 0;
    
    int recent_size = ENTROPY_HISTORY_SIZE / 4;
    
    for (int i = 0; i < ENTROPY_HISTORY_SIZE; i++) {
        int idx = (monitor->history_pos - 1 - i + ENTROPY_HISTORY_SIZE) % ENTROPY_HISTORY_SIZE;
        if (monitor->entropy_history[idx] > 0) {
            if (i < recent_size) {
                recent_avg += monitor->entropy_history[idx];
                recent_count++;
            } else {
                older_avg += monitor->entropy_history[idx];
                older_count++;
            }
        }
    }
    
    double trend = 0.0;
    if (recent_count > 0 && older_count > 0) {
        recent_avg /= recent_count;
        older_avg /= older_count;
        trend = older_avg > 0 ? (recent_avg - older_avg) / older_avg : 0;
    }
    
    pthread_mutex_unlock(&monitor->lock);
    return trend;
}

bool entropy_monitor_is_suspicious(EntropyMonitor *monitor, double threshold) {
    if (!monitor) {
        return false;
    }

    pthread_mutex_lock(&monitor->lock);
    
    bool result = false;
    double recent_avg = 0.0;
    int count = 0;
    int recent_size = 10;
    
    for (int i = 0; i < recent_size; i++) {
        int idx = (monitor->history_pos - 1 - i + ENTROPY_HISTORY_SIZE) % ENTROPY_HISTORY_SIZE;
        if (monitor->entropy_history[idx] > 0) {
            recent_avg += monitor->entropy_history[idx];
            count++;
        }
    }
    
    if (count > 0) {
        recent_avg /= count;
        result = recent_avg > threshold;
    }
    
    pthread_mutex_unlock(&monitor->lock);
    return result;
}

void entropy_monitor_reset(EntropyMonitor *monitor) {
    if (!monitor) {
        return;
    }

    pthread_mutex_lock(&monitor->lock);
    monitor->window_pos = 0;
    monitor->window_count = 0;
    monitor->history_pos = 0;
    monitor->avg_entropy = 0;
    monitor->max_entropy = 0;
    monitor->min_entropy = 1.0;
    monitor->total_bytes_processed = 0;
    monitor->high_entropy_chunks = 0;
    memset(monitor->window, 0, ENTROPY_WINDOW_SIZE);
    memset(monitor->entropy_history, 0, sizeof(monitor->entropy_history));
    pthread_mutex_unlock(&monitor->lock);
}

void entropy_monitor_destroy(EntropyMonitor *monitor) {
    if (!monitor) {
        return;
    }
    pthread_mutex_destroy(&monitor->lock);
}
