#ifndef SENSITIVE_WORDS_H
#define SENSITIVE_WORDS_H

#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

#define SENSITIVE_WORDS_MAX 256
#define SENSITIVE_WORD_MAX_LEN 128
#define SENSITIVE_WORDS_DEFAULT_FILE "data/sensitive_words.txt"

typedef struct {
    char words[SENSITIVE_WORDS_MAX][SENSITIVE_WORD_MAX_LEN];
    int count;
    char file_path[512];
    time_t last_modified;
    pthread_mutex_t mutex;
    bool initialized;
} SensitiveWords;

bool sensitive_words_init(SensitiveWords *sw, const char *file_path);
void sensitive_words_cleanup(SensitiveWords *sw);
bool sensitive_words_load(SensitiveWords *sw);
bool sensitive_words_hot_reload(SensitiveWords *sw);
bool sensitive_words_match(SensitiveWords *sw, const char *text, char *matched_word, size_t matched_word_len);
int sensitive_words_get_count(SensitiveWords *sw);

#endif
