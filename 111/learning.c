#include "learning.h"
#include "injection_detect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

static uint32_t hash_pattern(const char *pattern) {
    uint32_t hash = 5381;
    int c;
    while ((c = *pattern++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % MAX_LEARNED_RULES;
}

learning_ctx_t *learning_create(void) {
    learning_ctx_t *ctx = (learning_ctx_t *)calloc(1, sizeof(learning_ctx_t));
    if (!ctx) return NULL;

    ctx->enabled = 0;
    ctx->start_time = 0;
    ctx->last_feedback_time = 0;
    ctx->total_learned = 0;
    ctx->total_verified = 0;
    ctx->total_false_positives = 0;
    ctx->total_false_negatives = 0;
    ctx->learning_rate = 0.1;
    ctx->rules = NULL;
    ctx->rule_count = 0;
    ctx->sandbox = NULL;
    ctx->worker_running = 0;
    memset(ctx->rule_hash, 0, sizeof(ctx->rule_hash));
    pthread_mutex_init(&ctx->mutex, NULL);

    return ctx;
}

void learning_destroy(learning_ctx_t *ctx) {
    if (!ctx) return;

    learning_stop(ctx);

    pthread_mutex_lock(&ctx->mutex);
    learned_rule_t *rule = ctx->rules;
    while (rule) {
        learned_rule_t *next = rule->next;
        free(rule);
        rule = next;
    }
    memset(ctx->rule_hash, 0, sizeof(ctx->rule_hash));
    pthread_mutex_unlock(&ctx->mutex);

    pthread_mutex_destroy(&ctx->mutex);
    free(ctx);
}

int learning_init(learning_ctx_t *ctx, sandbox_ctx_t *sandbox) {
    if (!ctx) return -1;

    ctx->sandbox = sandbox;
    ctx->start_time = time(NULL);
    return 0;
}

static int learning_add_rule(learning_ctx_t *ctx, const char *pattern, rule_type_t type, int weight) {
    if (!ctx || !pattern || strlen(pattern) == 0) return -1;
    if (ctx->rule_count >= MAX_LEARNED_RULES) return -1;

    uint32_t hash = hash_pattern(pattern);

    pthread_mutex_lock(&ctx->mutex);

    learned_rule_t *existing = ctx->rule_hash[hash];
    while (existing) {
        if (strcmp(existing->pattern, pattern) == 0 && existing->type == type) {
            existing->weight = weight;
            existing->hits++;
            existing->last_seen = time(NULL);
            pthread_mutex_unlock(&ctx->mutex);
            return 0;
        }
        existing = existing->hash_next;
    }

    learned_rule_t *rule = (learned_rule_t *)calloc(1, sizeof(learned_rule_t));
    if (!rule) {
        pthread_mutex_unlock(&ctx->mutex);
        return -1;
    }

    strncpy(rule->pattern, pattern, MAX_FEATURE_LENGTH - 1);
    rule->type = type;
    rule->weight = weight;
    rule->hits = 1;
    rule->false_positives = 0;
    rule->false_negatives = 0;
    rule->precision = 1.0;
    rule->recall = 1.0;
    rule->first_seen = time(NULL);
    rule->last_seen = time(NULL);
    rule->learned_time = time(NULL);

    rule->next = ctx->rules;
    ctx->rules = rule;

    rule->hash_next = ctx->rule_hash[hash];
    ctx->rule_hash[hash] = rule;

    ctx->rule_count++;
    ctx->total_learned++;

    pthread_mutex_unlock(&ctx->mutex);
    return 0;
}

int learning_process_suspicious(learning_ctx_t *ctx, const char *sql, int score, sandbox_report_t *report) {
    if (!ctx || !sql || strlen(sql) == 0) return -1;

    if (!ctx->enabled) return 0;

    sandbox_report_t local_report;
    if (!report) {
        if (!ctx->sandbox) return -1;
        local_report = sandbox_execute(ctx->sandbox, sql);
        report = &local_report;
    }

    ctx->total_verified++;

    char sql_lower[65536];
    size_t sql_len = strlen(sql);
    if (sql_len >= sizeof(sql_lower)) sql_len = sizeof(sql_lower) - 1;
    for (size_t i = 0; i < sql_len; i++) {
        sql_lower[i] = tolower((unsigned char)sql[i]);
    }
    sql_lower[sql_len] = '\0';

    int is_injection = 0;
    if (report->result == SANDBOX_RESULT_DANGEROUS) {
        is_injection = 1;
    } else if (report->result == SANDBOX_RESULT_SUSPICIOUS && score >= 40) {
        is_injection = 1;
    }

    if (is_injection) {
        int weight = report->risk_score;
        if (weight < 10) weight = 10;
        if (weight > 100) weight = 100;

        if (report->detected_patterns[0]) {
            char *token = strtok(report->detected_patterns, ",");
            while (token) {
                learning_add_rule(ctx, token, RULE_TYPE_KEYWORD, weight);
                token = strtok(NULL, ",");
            }
        }

        if (score >= 50) {
            learning_add_rule(ctx, sql, RULE_TYPE_SYNTAX, weight);
        }
    }

    learning_update_model(ctx, sql, is_injection);

    return is_injection;
}

int learning_feedback(learning_ctx_t *ctx, const char *feature, rule_type_t type, int is_injection) {
    if (!ctx || !feature) return -1;

    pthread_mutex_lock(&ctx->mutex);
    ctx->last_feedback_time = time(NULL);

    uint32_t hash = hash_pattern(feature);
    learned_rule_t *rule = ctx->rule_hash[hash];

    while (rule) {
        if (strcmp(rule->pattern, feature) == 0 && rule->type == type) {
            if (is_injection) {
                rule->hits++;
                rule->weight = (int)(rule->weight * (1.0 + ctx->learning_rate));
                if (rule->weight > 100) rule->weight = 100;
                rule->false_negatives++;
            } else {
                rule->false_positives++;
                rule->weight = (int)(rule->weight * (1.0 - ctx->learning_rate));
                if (rule->weight < 5) rule->weight = 0;
            }

            rule->precision = (double)(rule->hits) / (rule->hits + rule->false_positives);
            rule->recall = (double)(rule->hits) / (rule->hits + rule->false_negatives);

            if (rule->weight == 0 || (rule->precision < 0.3 && rule->hits > 10)) {
                uint32_t h = hash_pattern(rule->pattern);
                learned_rule_t **prev = &ctx->rule_hash[h];
                while (*prev && *prev != rule) {
                    prev = &(*prev)->hash_next;
                }
                if (*prev) *prev = rule->hash_next;

                learned_rule_t **prev_list = &ctx->rules;
                while (*prev_list && *prev_list != rule) {
                    prev_list = &(*prev_list)->next;
                }
                if (*prev_list) *prev_list = rule->next;

                free(rule);
                ctx->rule_count--;
            }

            pthread_mutex_unlock(&ctx->mutex);
            return 0;
        }
        rule = rule->hash_next;
    }

    pthread_mutex_unlock(&ctx->mutex);

    if (is_injection) {
        return learning_add_rule(ctx, feature, type, 50);
    }

    return 0;
}

int learning_update_model(learning_ctx_t *ctx, const char *sql, int is_injection) {
    if (!ctx || !sql) return -1;

    char *tokens[256];
    int token_count = 0;

    char *copy = strdup(sql);
    if (!copy) return -1;

    char *token = strtok(copy, " \t\n\r(),;=\"'");
    while (token && token_count < 256) {
        if (strlen(token) >= 3) {
            tokens[token_count++] = token;
        }
        token = strtok(NULL, " \t\n\r(),;=\"'");
    }

    for (int i = 0; i < token_count; i++) {
        if (strlen(tokens[i]) >= 4) {
            learning_feedback(ctx, tokens[i], RULE_TYPE_KEYWORD, is_injection);
        }
    }

    for (int i = 0; i < token_count - 1; i++) {
        char combined[512];
        snprintf(combined, sizeof(combined), "%s %s", tokens[i], tokens[i + 1]);
        learning_feedback(ctx, combined, RULE_TYPE_SYNTAX, is_injection);
    }

    free(copy);
    return 0;
}

double learning_calculate_score(learning_ctx_t *ctx, const char *sql) {
    if (!ctx || !sql || !ctx->enabled) return 0.0;

    double total_score = 0.0;
    char sql_lower[65536];

    size_t sql_len = strlen(sql);
    if (sql_len >= sizeof(sql_lower)) sql_len = sizeof(sql_lower) - 1;
    for (size_t i = 0; i < sql_len; i++) {
        sql_lower[i] = tolower((unsigned char)sql[i]);
    }
    sql_lower[sql_len] = '\0';

    pthread_mutex_lock(&ctx->mutex);

    learned_rule_t *rule = ctx->rules;
    while (rule) {
        char pattern_lower[MAX_FEATURE_LENGTH];
        size_t plen = strlen(rule->pattern);
        for (size_t i = 0; i < plen; i++) {
            pattern_lower[i] = tolower((unsigned char)rule->pattern[i]);
        }
        pattern_lower[plen] = '\0';

        if (strstr(sql_lower, pattern_lower)) {
            double weight = rule->weight * rule->precision;
            total_score += weight;
        }
        rule = rule->next;
    }

    pthread_mutex_unlock(&ctx->mutex);

    return total_score;
}

int learning_get_rule_weight(learning_ctx_t *ctx, const char *pattern) {
    if (!ctx || !pattern) return 0;

    uint32_t hash = hash_pattern(pattern);

    pthread_mutex_lock(&ctx->mutex);
    learned_rule_t *rule = ctx->rule_hash[hash];
    while (rule) {
        if (strcmp(rule->pattern, pattern) == 0) {
            int weight = rule->weight;
            pthread_mutex_unlock(&ctx->mutex);
            return weight;
        }
        rule = rule->hash_next;
    }

    pthread_mutex_unlock(&ctx->mutex);
    return 0;
}

int learning_export_rules(learning_ctx_t *ctx, const char *filename) {
    if (!ctx || !filename) return -1;

    FILE *fp = fopen(filename, "w");
    if (!fp) return -1;

    fprintf(fp, "# MySQL Firewall Learned Rules\n");
    fprintf(fp, "# Exported: %s", ctime(&(time_t){time(NULL)}));
    fprintf(fp, "# Total rules: %zu\n\n", ctx->rule_count);

    fprintf(fp, "[metadata]\n");
    fprintf(fp, "learning_period_days = %d\n", LEARNING_PERIOD_DAYS);
    fprintf(fp, "start_time = %ld\n", ctx->start_time);
    fprintf(fp, "total_learned = %lu\n", ctx->total_learned);
    fprintf(fp, "total_verified = %lu\n", ctx->total_verified);
    fprintf(fp, "total_false_positives = %lu\n", ctx->total_false_positives);
    fprintf(fp, "total_false_negatives = %lu\n", ctx->total_false_negatives);
    fprintf(fp, "learning_rate = %.4f\n\n", ctx->learning_rate);

    fprintf(fp, "[rules]\n");
    fprintf(fp, "# format: pattern | type | weight | hits | false_positives | false_negatives | precision | recall\n");

    pthread_mutex_lock(&ctx->mutex);
    learned_rule_t *rule = ctx->rules;
    while (rule) {
        if (rule->weight > 0) {
            fprintf(fp, "%s|%d|%d|%d|%d|%d|%.4f|%.4f\n",
                    rule->pattern, rule->type, rule->weight,
                    rule->hits, rule->false_positives, rule->false_negatives,
                    rule->precision, rule->recall);
        }
        rule = rule->next;
    }
    pthread_mutex_unlock(&ctx->mutex);

    fclose(fp);
    return 0;
}

int learning_import_rules(learning_ctx_t *ctx, const char *filename) {
    if (!ctx || !filename) return -1;

    FILE *fp = fopen(filename, "r");
    if (!fp) return -1;

    char line[8192];
    int in_rules_section = 0;
    int imported = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        if (strncmp(line, "[rules]", 7) == 0) {
            in_rules_section = 1;
            continue;
        }

        if (line[0] == '[') {
            in_rules_section = 0;
            continue;
        }

        if (!in_rules_section) continue;

        char *pattern = strtok(line, "|");
        char *type_str = strtok(NULL, "|");
        char *weight_str = strtok(NULL, "|");
        char *hits_str = strtok(NULL, "|");
        char *fp_str = strtok(NULL, "|");
        char *fn_str = strtok(NULL, "|");
        char *prec_str = strtok(NULL, "|");
        char *recall_str = strtok(NULL, "|\r\n");

        if (!pattern || !type_str || !weight_str) continue;

        rule_type_t type = (rule_type_t)atoi(type_str);
        int weight = atoi(weight_str);

        learning_add_rule(ctx, pattern, type, weight);
        imported++;
    }

    fclose(fp);
    return imported;
}

void *learning_worker(void *arg) {
    learning_ctx_t *ctx = (learning_ctx_t *)arg;
    if (!ctx) return NULL;

    ctx->worker_running = 1;

    while (ctx->worker_running) {
        sleep(60);

        if (!ctx->enabled) continue;

        time_t now = time(NULL);
        if (now - ctx->start_time > LEARNING_PERIOD_DAYS * 86400) {
            ctx->enabled = 0;
            fprintf(stderr, "Learning period completed (%d days). Export rules to persist.\n",
                    LEARNING_PERIOD_DAYS);
            break;
        }
    }

    ctx->worker_running = 0;
    return NULL;
}

int learning_start(learning_ctx_t *ctx) {
    if (!ctx) return -1;

    ctx->enabled = 1;
    if (ctx->start_time == 0) {
        ctx->start_time = time(NULL);
    }

    if (pthread_create(&ctx->worker_thread, NULL, learning_worker, ctx) != 0) {
        return -1;
    }

    return 0;
}

void learning_stop(learning_ctx_t *ctx) {
    if (!ctx) return;

    ctx->enabled = 0;
    ctx->worker_running = 0;

    pthread_join(ctx->worker_thread, NULL);
}
