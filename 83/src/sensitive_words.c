#include "sensitive_words.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <ctype.h>

static void trim(char *s)
{
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' ||
                       s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
    char *start = s;
    while (*start && (*start == ' ' || *start == '\t')) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
}

bool sensitive_words_init(SensitiveWords *sw, const char *file_path)
{
    memset(sw, 0, sizeof(*sw));
    pthread_mutex_init(&sw->mutex, NULL);
    if (file_path)
        strncpy(sw->file_path, file_path, sizeof(sw->file_path) - 1);
    else
        strncpy(sw->file_path, SENSITIVE_WORDS_DEFAULT_FILE, sizeof(sw->file_path) - 1);
    sw->initialized = true;
    return sensitive_words_load(sw);
}

void sensitive_words_cleanup(SensitiveWords *sw)
{
    if (!sw->initialized) return;
    pthread_mutex_lock(&sw->mutex);
    sw->count = 0;
    memset(sw->words, 0, sizeof(sw->words));
    pthread_mutex_unlock(&sw->mutex);
    pthread_mutex_destroy(&sw->mutex);
    sw->initialized = false;
}

bool sensitive_words_load(SensitiveWords *sw)
{
    if (!sw->initialized) return false;

    pthread_mutex_lock(&sw->mutex);
    sw->count = 0;

    FILE *f = fopen(sw->file_path, "r");
    if (!f) {
        fprintf(stderr, "[SensitiveWords] Warning: Cannot open %s, using built-in defaults\n", sw->file_path);
        const char *defaults[] = {
            "财务数据",
            "客户信息",
            "源代码",
            "password",
            "secret",
            "机密",
            "绝密",
            "confidential",
            "credit card",
            "身份证",
            "银行卡",
            NULL
        };
        for (int i = 0; defaults[i] && sw->count < SENSITIVE_WORDS_MAX; i++) {
            strncpy(sw->words[sw->count], defaults[i], SENSITIVE_WORD_MAX_LEN - 1);
            sw->count++;
        }
        sw->last_modified = 0;
        pthread_mutex_unlock(&sw->mutex);
        return true;
    }

    struct stat st;
    if (stat(sw->file_path, &st) == 0) {
        sw->last_modified = st.st_mtime;
    }

    char line[SENSITIVE_WORD_MAX_LEN + 4];
    while (fgets(line, sizeof(line), f) && sw->count < SENSITIVE_WORDS_MAX) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;
        strncpy(sw->words[sw->count], line, SENSITIVE_WORD_MAX_LEN - 1);
        sw->count++;
    }

    fclose(f);
    pthread_mutex_unlock(&sw->mutex);

    fprintf(stderr, "[SensitiveWords] Loaded %d sensitive words from %s\n", sw->count, sw->file_path);
    return true;
}

bool sensitive_words_hot_reload(SensitiveWords *sw)
{
    if (!sw->initialized) return false;

    struct stat st;
    if (stat(sw->file_path, &st) != 0) return false;

    if (st.st_mtime <= sw->last_modified) {
        return false;
    }

    fprintf(stderr, "[SensitiveWords] Hot-reloading: %s modified\n", sw->file_path);
    return sensitive_words_load(sw);
}

bool sensitive_words_match(SensitiveWords *sw, const char *text, char *matched_word, size_t matched_word_len)
{
    if (!sw->initialized || !text) return false;

    bool found = false;

    pthread_mutex_lock(&sw->mutex);
    for (int i = 0; i < sw->count; i++) {
        if (strstr(text, sw->words[i]) != NULL) {
            if (matched_word && matched_word_len > 0) {
                strncpy(matched_word, sw->words[i], matched_word_len - 1);
                matched_word[matched_word_len - 1] = '\0';
            }
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&sw->mutex);

    return found;
}

int sensitive_words_get_count(SensitiveWords *sw)
{
    if (!sw->initialized) return 0;
    pthread_mutex_lock(&sw->mutex);
    int count = sw->count;
    pthread_mutex_unlock(&sw->mutex);
    return count;
}
