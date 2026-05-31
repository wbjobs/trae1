#ifndef LEARNING_H
#define LEARNING_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include "sandbox.h"

#define LEARNING_PERIOD_DAYS 7
#define MAX_LEARNED_RULES 1000
#define MAX_FEATURE_LENGTH 256

typedef enum {
    RULE_TYPE_REGEX = 0,
    RULE_TYPE_SYNTAX = 1,
    RULE_TYPE_KEYWORD = 2
} rule_type_t;

typedef struct learned_rule {
    char pattern[MAX_FEATURE_LENGTH];
    rule_type_t type;
    int weight;
    int hits;
    int false_positives;
    int false_negatives;
    double precision;
    double recall;
    time_t first_seen;
    time_t last_seen;
    time_t learned_time;
    struct learned_rule *next;
    struct learned_rule *hash_next;
} learned_rule_t;

typedef struct {
    int enabled;
    time_t start_time;
    time_t last_feedback_time;
    uint64_t total_learned;
    uint64_t total_verified;
    uint64_t total_false_positives;
    uint64_t total_false_negatives;
    double learning_rate;
    learned_rule_t *rules;
    learned_rule_t *rule_hash[MAX_LEARNED_RULES];
    size_t rule_count;
    sandbox_ctx_t *sandbox;
    pthread_mutex_t mutex;
    pthread_t worker_thread;
    int worker_running;
} learning_ctx_t;

typedef struct {
    char feature[MAX_FEATURE_LENGTH];
    rule_type_t type;
    int initial_weight;
    int sandbox_confirmed;
    int user_confirmed;
} feedback_entry_t;

learning_ctx_t *learning_create(void);
void learning_destroy(learning_ctx_t *ctx);
int learning_init(learning_ctx_t *ctx, sandbox_ctx_t *sandbox);
int learning_start(learning_ctx_t *ctx);
void learning_stop(learning_ctx_t *ctx);
int learning_process_suspicious(learning_ctx_t *ctx, const char *sql, int score, sandbox_report_t *report);
int learning_feedback(learning_ctx_t *ctx, const char *feature, rule_type_t type, int is_injection);
int learning_export_rules(learning_ctx_t *ctx, const char *filename);
int learning_import_rules(learning_ctx_t *ctx, const char *filename);
int learning_update_model(learning_ctx_t *ctx, const char *sql, int is_injection);
double learning_calculate_score(learning_ctx_t *ctx, const char *sql);
int learning_get_rule_weight(learning_ctx_t *ctx, const char *pattern);
void *learning_worker(void *arg);

#endif
