#include "injection_detect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pcre.h>
#include <pthread.h>

#define MAX_REGEX_PATTERNS 64
#define MAX_SYNTAX_PATTERNS 32

static pcre *g_regex_patterns[MAX_REGEX_PATTERNS];
static pcre_extra *g_regex_extra[MAX_REGEX_PATTERNS];
static int g_regex_count = 0;
static char g_regex_names[MAX_REGEX_PATTERNS][128];
static int g_regex_weights[MAX_REGEX_PATTERNS];
static int g_syntax_count = 0;
static char g_syntax_patterns[MAX_SYNTAX_PATTERNS][256];
static int g_syntax_weights[MAX_SYNTAX_PATTERNS];
static pthread_mutex_t g_rules_mutex = PTHREAD_MUTEX_INITIALIZER;
static learning_ctx_t *g_learning_ctx = NULL;

static const char *g_default_regex_patterns[] = {
    "('\\s*(OR|AND)\\s*['\"]?\\d+['\"]?\\s*=\\s*['\"]?\\d+)",
    "('\\s*OR\\s*'\\s*'\\s*=\\s*')",
    "('\\s*OR\\s*1\\s*=\\s*1)",
    "(UNION\\s+(ALL\\s+)?SELECT)",
    "(DROP\\s+(TABLE|DATABASE|INDEX))",
    "(DELETE\\s+FROM)",
    "(UPDATE\\s+.*\\s+SET)",
    "(INSERT\\s+INTO)",
    "(ALTER\\s+(TABLE|DATABASE))",
    "(CREATE\\s+(TABLE|DATABASE|INDEX))",
    "(EXEC\\s*\\(|EXECUTE\\s*\\()",
    "( xp_)",
    "( sp_)",
    "(0x[0-9a-f]+)",
    "(['\"][\\s]*;[\\s]*(DROP|DELETE|INSERT|UPDATE))",
    "(WAITFOR\\s+DELAY)",
    "(BENCHMARK\\s*\\()",
    "(SLEEP\\s*\\()",
    "(LOAD_FILE\\s*\\()",
    "(INTO\\s+(OUTFILE|DUMPFILE))",
    "([\\s]union[\\s]+[\\s]*select[\\s])",
    "([\\s]or[\\s]+[\\s]*[\\'\"]?[\\w]+[\\s]*=[\\s]*[\\'\"]?[\\w]+)",
    "([\\s]and[\\s]+[\\s]*[\\'\"]?[\\w]+[\\s]*=[\\s]*[\\'\"]?[\\w]+)",
    "([\\s]like[\\s]+[\\'\"]%)",
    "(REGEXP\\s+[\\'\"])",
    "(HAVING\\s+[\\'\"]?\\w+[\\'\"]?\\s*=\\s*[\\'\"]?\\w+)",
    "([\\s]order\\s+by\\s+\\d+)",
    "(--[\\s]*$)",
    "(#[\\s]*$)",
    "(\\/\\*.*\\*\\/)",
    "([\\s]if\\s*\\([^,]+,[^,]+,[^)]+\\)@?)",
    "([\\s]cast\\s*\\()",
    "([\\s]extractvalue\\s*\\()",
    "([\\s]updatexml\\s*\\()",
    "([\\s]floor\\s*\\()",
    "([\\s]rand\\s*\\()",
    "([\\s]join\\s+[\\w]+\\s+on)",
};

static int g_default_regex_weights[] = {
    100, 100, 100, 90, 95, 85, 80, 75, 85, 85,
    90, 95, 90, 60, 95, 90, 85, 90, 95, 95,
    90, 85, 80, 70, 75, 70, 65, 50, 50, 50,
    60, 55, 75, 75, 50, 50, 60
};

static const char *g_default_syntax_patterns[] = {
    "OR 1=1",
    "OR '1'='1'",
    "OR \"1\"=\"1\"",
    "OR 1=1--",
    "OR 1=1#",
    "UNION SELECT",
    "UNION ALL SELECT",
    "DROP TABLE",
    "DROP DATABASE",
    "DELETE FROM",
    "INSERT INTO",
    "UPDATE SET",
    "LOAD_FILE",
    "INTO OUTFILE",
    "INTO DUMPFILE",
    "EXEC(",
    "EXECUTE(",
    "xp_cmdshell",
    "sp_executesql",
    "WAITFOR DELAY",
    "BENCHMARK(",
    "SLEEP(",
    "0x",
    "--",
    "/*",
    "ORDER BY",
    "HAVING",
    "LIKE '%",
};

static int g_default_syntax_weights[] = {
    100, 100, 100, 100, 100, 90, 90, 95, 95, 85,
    75, 80, 95, 95, 95, 90, 90, 100, 95, 90,
    85, 90, 60, 50, 50, 65, 70, 60
};

void set_learning_context(learning_ctx_t *ctx) {
    g_learning_ctx = ctx;
}

int compile_regex_patterns(void) {
    pthread_mutex_lock(&g_rules_mutex);

    for (int i = 0; i < g_regex_count; i++) {
        if (g_regex_patterns[i]) {
            if (g_regex_extra[i]) {
                pcre_free(g_regex_extra[i]);
            }
            pcre_free(g_regex_patterns[i]);
            g_regex_patterns[i] = NULL;
            g_regex_extra[i] = NULL;
        }
    }
    g_regex_count = 0;

    int num_patterns = sizeof(g_default_regex_patterns) / sizeof(g_default_regex_patterns[0]);

    for (int i = 0; i < num_patterns && g_regex_count < MAX_REGEX_PATTERNS; i++) {
        const char *errptr;
        int erroffset;

        pcre *re = pcre_compile(g_default_regex_patterns[i],
                               PCRE_CASELESS | PCRE_DOTALL,
                               &errptr, &erroffset, NULL);

        if (re) {
            pcre_extra *extra = pcre_study(re, 0, &errptr);
            g_regex_patterns[g_regex_count] = re;
            g_regex_extra[g_regex_count] = extra;
            strncpy(g_regex_names[g_regex_count], g_default_regex_patterns[i], 127);
            g_regex_names[g_regex_count][127] = '\0';
            g_regex_weights[g_regex_count] = g_default_regex_weights[i];
            g_regex_count++;
        }
    }

    g_syntax_count = sizeof(g_default_syntax_patterns) / sizeof(g_default_syntax_patterns[0]);
    for (int i = 0; i < g_syntax_count && i < MAX_SYNTAX_PATTERNS; i++) {
        strncpy(g_syntax_patterns[i], g_default_syntax_patterns[i], 255);
        g_syntax_patterns[i][255] = '\0';
        g_syntax_weights[i] = g_default_syntax_weights[i];
    }

    pthread_mutex_unlock(&g_rules_mutex);
    return 0;
}

void injection_detector_init(void) {
    compile_regex_patterns();
}

void injection_detector_destroy(void) {
    pthread_mutex_lock(&g_rules_mutex);

    for (int i = 0; i < g_regex_count; i++) {
        if (g_regex_patterns[i]) {
            if (g_regex_extra[i]) {
                pcre_free(g_regex_extra[i]);
            }
            pcre_free(g_regex_patterns[i]);
            g_regex_patterns[i] = NULL;
        }
    }
    g_regex_count = 0;

    pthread_mutex_unlock(&g_rules_mutex);
    pthread_mutex_destroy(&g_rules_mutex);
}

void reload_rules(void) {
    compile_regex_patterns();
}

static int check_pattern_match(pcre *re, pcre_extra *extra, const char *subject, char *ovector, int ovector_size) {
    int rc = pcre_exec(re, extra, subject, strlen(subject), 0, 0, (int *)ovector, ovector_size);
    return rc > 0;
}

int check_sql_syntax_patterns(const char *sql) {
    if (!sql) return -1;

    size_t sql_len = strlen(sql);
    char sql_lower[65536];
    if (sql_len >= sizeof(sql_lower)) {
        sql_len = sizeof(sql_lower) - 1;
    }

    for (size_t i = 0; i < sql_len; i++) {
        sql_lower[i] = tolower((unsigned char)sql[i]);
    }
    sql_lower[sql_len] = '\0';

    pthread_mutex_lock(&g_rules_mutex);

    for (int i = 0; i < g_syntax_count; i++) {
        if (strstr(sql_lower, g_syntax_patterns[i])) {
            pthread_mutex_unlock(&g_rules_mutex);
            return i;
        }
    }

    pthread_mutex_unlock(&g_rules_mutex);
    return -1;
}

int calculate_injection_score(const char *sql) {
    if (!sql || strlen(sql) == 0) return 0;

    int total_score = 0;
    int regex_score = 0;
    int syntax_score = 0;
    int learning_score = 0;

    pthread_mutex_lock(&g_rules_mutex);

    int ovector[30];
    for (int i = 0; i < g_regex_count; i++) {
        if (check_pattern_match(g_regex_patterns[i], g_regex_extra[i], sql, (char*)ovector, 30)) {
            regex_score += g_regex_weights[i];
        }
    }

    pthread_mutex_unlock(&g_rules_mutex);

    size_t sql_len = strlen(sql);
    char sql_lower[65536];
    if (sql_len >= sizeof(sql_lower)) sql_len = sizeof(sql_lower) - 1;
    for (size_t i = 0; i < sql_len; i++) {
        sql_lower[i] = tolower((unsigned char)sql[i]);
    }
    sql_lower[sql_len] = '\0';

    pthread_mutex_lock(&g_rules_mutex);
    for (int i = 0; i < g_syntax_count; i++) {
        if (strstr(sql_lower, g_syntax_patterns[i])) {
            syntax_score += g_syntax_weights[i];
        }
    }
    pthread_mutex_unlock(&g_rules_mutex);

    if (contains_or_always_true(sql)) {
        syntax_score += 100;
    }

    int keyword_count = count_sql_keywords(sql);
    int encoded_count = detect_encoded_chars(sql);
    int has_comments = detect_suspicious_comments(sql);

    if (keyword_count >= 6 && encoded_count >= 2) {
        syntax_score += 75;
    }
    if (has_comments && keyword_count >= 4) {
        syntax_score += 65;
    }

    if (encoded_count > 0) {
        syntax_score += encoded_count * 10;
    }

    if (keyword_count >= 8) {
        syntax_score += 30;
    }

    if (g_learning_ctx && g_learning_ctx->enabled) {
        learning_score = (int)learning_calculate_score(g_learning_ctx, sql);
    }

    total_score = regex_score + syntax_score + learning_score;

    return total_score;
}

static int contains_or_always_true(const char *sql) {
    const char *or_patterns[] = {
        "' OR '1'='1'",
        "' OR 'a'='a'",
        "\" OR \"1\"=\"1\"",
        "' OR 1=1",
        "\" OR 1=1",
        "' OR 'x'='x'",
        " OR 1=1--",
        " OR 1=1#",
        " OR '1'='1'--",
        "' OR \"\"='"
    };

    size_t sql_len = strlen(sql);
    for (size_t i = 0; i < sql_len; i++) {
        char c = tolower((unsigned char)sql[i]);
        if (c == '\'') {
            for (size_t j = 0; j < sizeof(or_patterns)/sizeof(or_patterns[0]); j++) {
                size_t pattern_len = strlen(or_patterns[j]);
                if (i + pattern_len <= sql_len) {
                    char *tmp = malloc(pattern_len + 1);
                    if (!tmp) continue;
                    for (size_t k = 0; k < pattern_len; k++) {
                        tmp[k] = tolower((unsigned char)sql[i + k]);
                    }
                    tmp[pattern_len] = '\0';
                    if (strcmp(tmp, or_patterns[j]) == 0) {
                        free(tmp);
                        return 1;
                    }
                    free(tmp);
                }
            }
        }
    }
    return 0;
}

static int count_sql_keywords(const char *sql) {
    int count = 0;
    const char *keywords[] = {
        "UNION", "SELECT", "FROM", "WHERE", "AND", "OR", "INSERT",
        "UPDATE", "DELETE", "DROP", "CREATE", "ALTER", "EXEC",
        "EXECUTE", "UNION", "JOIN", "SUBSELECT", "SLEEP", "BENCHMARK"
    };
    int num_keywords = sizeof(keywords) / sizeof(keywords[0]);

    size_t sql_len = strlen(sql);
    char *sql_lower = malloc(sql_len + 1);
    if (!sql_lower) return 0;

    for (size_t i = 0; i < sql_len; i++) {
        sql_lower[i] = tolower((unsigned char)sql[i]);
    }
    sql_lower[sql_len] = '\0';

    for (int i = 0; i < num_keywords; i++) {
        const char *keyword = keywords[i];
        size_t keyword_len = strlen(keyword);

        for (size_t j = 0; j <= sql_len - keyword_len; j++) {
            if (strncmp(sql_lower + j, keyword, keyword_len) == 0) {
                if (j == 0 || !isalnum((unsigned char)sql_lower[j-1])) {
                    if (j + keyword_len >= sql_len || !isalnum((unsigned char)sql_lower[j + keyword_len])) {
                        count++;
                    }
                }
            }
        }
    }

    free(sql_lower);
    return count;
}

static int detect_encoded_chars(const char *sql) {
    int count = 0;
    size_t sql_len = strlen(sql);

    for (size_t i = 0; i < sql_len; i++) {
        if (sql[i] == '%' && i + 2 < sql_len) {
            if (isxdigit((unsigned char)sql[i+1]) && isxdigit((unsigned char)sql[i+2])) {
                count++;
                i += 2;
            }
        }
    }

    return count;
}

static int detect_suspicious_comments(const char *sql) {
    const char *patterns[] = {
        "--",
        "#",
        "/*",
        "*/",
        "/ *",
        "* /"
    };

    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        if (strstr(sql, patterns[i])) {
            return 1;
        }
    }
    return 0;
}

injection_result_t detect_injection(const char *sql) {
    injection_result_t result = {0};

    if (!sql || strlen(sql) == 0) {
        return result;
    }

    int score = calculate_injection_score(sql);
    result.score = score;
    result.regex_score = score;
    result.syntax_score = score;
    result.learning_score = 0;

    if (g_learning_ctx && g_learning_ctx->enabled) {
        result.learning_score = (int)learning_calculate_score(g_learning_ctx, sql);
        result.score += result.learning_score;
    }

    pthread_mutex_lock(&g_rules_mutex);

    int ovector[30];
    for (int i = 0; i < g_regex_count && !result.detected; i++) {
        if (check_pattern_match(g_regex_patterns[i], g_regex_extra[i], sql, (char*)ovector, 30)) {
            if (result.score >= DETECTION_THRESHOLD_BLOCK) {
                result.detected = 1;
            }
            if (result.score >= DETECTION_THRESHOLD_SUSPICIOUS) {
                result.suspicious = 1;
            }
            result.match_type = MATCH_REGEX;
            strncpy(result.pattern, g_regex_names[i], sizeof(result.pattern) - 1);
            result.pattern[sizeof(result.pattern) - 1] = '\0';
            snprintf(result.details, sizeof(result.details), "Regex pattern matched at position %d", ovector[0]);
            break;
        }
    }

    pthread_mutex_unlock(&g_rules_mutex);

    if (!result.detected) {
        if (contains_or_always_true(sql)) {
            if (result.score >= DETECTION_THRESHOLD_BLOCK) {
                result.detected = 1;
            }
            if (result.score >= DETECTION_THRESHOLD_SUSPICIOUS) {
                result.suspicious = 1;
            }
            result.match_type = MATCH_SYNTAX;
            strncpy(result.pattern, "OR_ALWAYS_TRUE", sizeof(result.pattern) - 1);
            snprintf(result.details, sizeof(result.details), "OR with always-true condition detected");
        }
    }

    if (!result.detected) {
        int syntax_idx = check_sql_syntax_patterns(sql);
        if (syntax_idx >= 0) {
            if (result.score >= DETECTION_THRESHOLD_BLOCK) {
                result.detected = 1;
            }
            if (result.score >= DETECTION_THRESHOLD_SUSPICIOUS) {
                result.suspicious = 1;
            }
            result.match_type = MATCH_SYNTAX;
            strncpy(result.pattern, g_syntax_patterns[syntax_idx], sizeof(result.pattern) - 1);
            snprintf(result.details, sizeof(result.details), "Syntax pattern %d matched", syntax_idx);
        }
    }

    if (!result.detected) {
        int keyword_count = count_sql_keywords(sql);
        int encoded_count = detect_encoded_chars(sql);
        int has_comments = detect_suspicious_comments(sql);

        if (keyword_count >= 6 && encoded_count >= 2) {
            if (result.score >= DETECTION_THRESHOLD_BLOCK) {
                result.detected = 1;
            }
            if (result.score >= DETECTION_THRESHOLD_SUSPICIOUS) {
                result.suspicious = 1;
            }
            result.match_type = MATCH_SYNTAX;
            strncpy(result.pattern, "HIGH_KEYWORD_WITH_ENCODED", sizeof(result.pattern) - 1);
            snprintf(result.details, sizeof(result.details),
                    "High keyword count (%d) with URL encoding (%d chars)", keyword_count, encoded_count);
        }
        else if (has_comments && keyword_count >= 4) {
            if (result.score >= DETECTION_THRESHOLD_BLOCK) {
                result.detected = 1;
            }
            if (result.score >= DETECTION_THRESHOLD_SUSPICIOUS) {
                result.suspicious = 1;
            }
            result.match_type = MATCH_SYNTAX;
            strncpy(result.pattern, "COMMENTS_WITH_KEYWORDS", sizeof(result.pattern) - 1);
            snprintf(result.details, sizeof(result.details),
                    "SQL comments present with multiple keywords (%d)", keyword_count);
        }
    }

    if (!result.detected && result.score >= DETECTION_THRESHOLD_BLOCK) {
        result.detected = 1;
        result.match_type = MATCH_LEARNING;
        strncpy(result.pattern, "LEARNING_WEIGHTED_SCORE", sizeof(result.pattern) - 1);
        snprintf(result.details, sizeof(result.details), "Weighted score %d exceeds threshold", result.score);
    }

    if (!result.detected && result.score >= DETECTION_THRESHOLD_SUSPICIOUS) {
        result.suspicious = 1;
    }

    return result;
}
