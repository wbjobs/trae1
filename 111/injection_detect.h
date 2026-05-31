#ifndef INJECTION_DETECT_H
#define INJECTION_DETECT_H

#include <stddef.h>
#include "learning.h"

#define DETECTION_THRESHOLD_BLOCK 60
#define DETECTION_THRESHOLD_SUSPICIOUS 30

typedef enum {
    MATCH_NONE = 0,
    MATCH_REGEX = 1,
    MATCH_SYNTAX = 2,
    MATCH_LEARNING = 3
} match_type_t;

typedef struct {
    int detected;
    int suspicious;
    int score;
    int regex_score;
    int syntax_score;
    int learning_score;
    match_type_t match_type;
    char pattern[256];
    char details[512];
} injection_result_t;

typedef struct {
    char *keyword;
    int severity;
    int weight;
} sql_keyword_t;

void injection_detector_init(void);
void injection_detector_destroy(void);
void reload_rules(void);
injection_result_t detect_injection(const char *sql);
int check_sql_syntax_patterns(const char *sql);
int calculate_injection_score(const char *sql);
void set_learning_context(learning_ctx_t *ctx);

#endif
