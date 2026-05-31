#include "sctp_transfer.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static inline uint64_t get_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

void quality_history_init(quality_history_t *qh)
{
    if (!qh) return;
    memset(qh->samples, 0, sizeof(qh->samples));
    qh->head = 0;
    qh->count = 0;
    pthread_mutex_init(&qh->lock, NULL);
}

void quality_history_add(quality_history_t *qh, const quality_sample_t *sample)
{
    if (!qh || !sample) return;

    pthread_mutex_lock(&qh->lock);
    memcpy(&qh->samples[qh->head], sample, sizeof(quality_sample_t));
    qh->head = (qh->head + 1) % QUALITY_SAMPLE_COUNT;
    if (qh->count < QUALITY_SAMPLE_COUNT)
        qh->count++;
    pthread_mutex_unlock(&qh->lock);
}

int quality_history_get_samples(quality_history_t *qh,
                                 quality_sample_t *out,
                                 int max_samples)
{
    if (!qh || !out || max_samples <= 0) return 0;

    pthread_mutex_lock(&qh->lock);

    int num = qh->count < max_samples ? qh->count : max_samples;
    int start = (qh->head - num + QUALITY_SAMPLE_COUNT) % QUALITY_SAMPLE_COUNT;

    for (int i = 0; i < num; i++) {
        int idx = (start + i) % QUALITY_SAMPLE_COUNT;
        memcpy(&out[i], &qh->samples[idx], sizeof(quality_sample_t));
    }

    pthread_mutex_unlock(&qh->lock);
    return num;
}

static void linear_regression(const double *x, const double *y, int n,
                               double *slope, double *intercept, double *r_squared)
{
    double sum_x = 0.0, sum_y = 0.0;
    double sum_xy = 0.0, sum_x2 = 0.0;

    for (int i = 0; i < n; i++) {
        sum_x += x[i];
        sum_y += y[i];
        sum_xy += x[i] * y[i];
        sum_x2 += x[i] * x[i];
    }

    double denominator = (double)n * sum_x2 - sum_x * sum_x;
    if (fabs(denominator) < 1e-10) {
        *slope = 0.0;
        *intercept = sum_y / n;
        *r_squared = 0.0;
        return;
    }

    *slope = ((double)n * sum_xy - sum_x * sum_y) / denominator;
    *intercept = (sum_y - *slope * sum_x) / n;

    double ss_tot = 0.0, ss_res = 0.0;
    double y_mean = sum_y / n;
    for (int i = 0; i < n; i++) {
        double y_pred = (*slope) * x[i] + (*intercept);
        ss_tot += (y[i] - y_mean) * (y[i] - y_mean);
        ss_res += (y[i] - y_pred) * (y[i] - y_pred);
    }

    if (ss_tot > 1e-10)
        *r_squared = 1.0 - (ss_res / ss_tot);
    else
        *r_squared = 0.0;
}

int quality_predict(quality_history_t *qh,
                     quality_prediction_t *pred,
                     int predict_seconds)
{
    if (!qh || !pred || predict_seconds <= 0) return -1;

    quality_sample_t samples[QUALITY_SAMPLE_COUNT];
    int n = quality_history_get_samples(qh, samples, QUALITY_SAMPLE_COUNT);
    if (n < 5) {
        pred->predicted_loss_rate = 0.0f;
        pred->predicted_rtt_us = 50000;
        pred->predicted_bandwidth_bps = 10 * 1024 * 1024;
        pred->confidence = 0.0f;
        pred->will_fail = false;
        pred->prediction_time_us = get_time_us();
        return 0;
    }

    double time_x[QUALITY_SAMPLE_COUNT];
    double loss_y[QUALITY_SAMPLE_COUNT];
    double rtt_y[QUALITY_SAMPLE_COUNT];
    double bw_y[QUALITY_SAMPLE_COUNT];

    uint64_t first_ts = samples[0].timestamp_us;
    for (int i = 0; i < n; i++) {
        time_x[i] = (double)(samples[i].timestamp_us - first_ts) / 1000000.0;
        loss_y[i] = (double)samples[i].loss_rate;
        rtt_y[i] = (double)samples[i].rtt_us;
        bw_y[i] = (double)samples[i].bandwidth_bps;
    }

    double loss_slope, loss_intercept, loss_r2;
    double rtt_slope, rtt_intercept, rtt_r2;
    double bw_slope, bw_intercept, bw_r2;

    linear_regression(time_x, loss_y, n, &loss_slope, &loss_intercept, &loss_r2);
    linear_regression(time_x, rtt_y, n, &rtt_slope, &rtt_intercept, &rtt_r2);
    linear_regression(time_x, bw_y, n, &bw_slope, &bw_intercept, &bw_r2);

    double predict_time = time_x[n - 1] + (double)predict_seconds;

    double pred_loss = loss_slope * predict_time + loss_intercept;
    double pred_rtt = rtt_slope * predict_time + rtt_intercept;
    double pred_bw = bw_slope * predict_time + bw_intercept;

    if (pred_loss < 0.0) pred_loss = 0.0;
    if (pred_loss > 100.0) pred_loss = 100.0;
    if (pred_rtt < 1000.0) pred_rtt = 1000.0;
    if (pred_bw < 1024.0) pred_bw = 1024.0;

    pred->predicted_loss_rate = (float)pred_loss;
    pred->predicted_rtt_us = (uint64_t)pred_rtt;
    pred->predicted_bandwidth_bps = (uint64_t)pred_bw;
    pred->confidence = (float)((fabs(loss_r2) + fabs(rtt_r2) + fabs(bw_r2)) / 3.0);
    if (pred->confidence < 0.0f) pred->confidence = 0.0f;
    if (pred->confidence > 1.0f) pred->confidence = 1.0f;

    pred->will_fail = (pred_loss > PREDICTION_THRESHOLD) && (loss_slope > 0);
    pred->prediction_time_us = get_time_us();

    return 0;
}
