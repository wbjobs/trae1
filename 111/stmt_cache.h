#ifndef STMT_CACHE_H
#define STMT_CACHE_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>

#define MAX_STATEMENT_CACHE 1000

typedef struct stmt_cache_entry {
    uint32_t stmt_id;
    char *sql_template;
    uint16_t num_params;
    time_t last_access;
    struct stmt_cache_entry *prev;
    struct stmt_cache_entry *next;
} stmt_cache_entry_t;

typedef struct stmt_cache {
    stmt_cache_entry_t *head;
    stmt_cache_entry_t *tail;
    stmt_cache_entry_t *entries[MAX_STATEMENT_CACHE];
    pthread_mutex_t lock;
    size_t count;
} stmt_cache_t;

stmt_cache_t *stmt_cache_create(void);
void stmt_cache_destroy(stmt_cache_t *cache);
int stmt_cache_put(stmt_cache_t *cache, uint32_t stmt_id, const char *sql_template, uint16_t num_params);
stmt_cache_entry_t *stmt_cache_get(stmt_cache_t *cache, uint32_t stmt_id);
void stmt_cache_remove(stmt_cache_t *cache, uint32_t stmt_id);
void stmt_cache_clear(stmt_cache_t *cache);

#endif
