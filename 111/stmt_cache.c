#include "stmt_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t hash_stmt_id(uint32_t stmt_id) {
    return stmt_id % MAX_STATEMENT_CACHE;
}

stmt_cache_t *stmt_cache_create(void) {
    stmt_cache_t *cache = (stmt_cache_t *)calloc(1, sizeof(stmt_cache_t));
    if (!cache) return NULL;

    pthread_mutex_init(&cache->lock, NULL);
    cache->count = 0;
    cache->head = NULL;
    cache->tail = NULL;
    memset(cache->entries, 0, sizeof(cache->entries));
    return cache;
}

void stmt_cache_destroy(stmt_cache_t *cache) {
    if (!cache) return;

    stmt_cache_clear(cache);
    pthread_mutex_destroy(&cache->lock);
    free(cache);
}

static void remove_from_list(stmt_cache_t *cache, stmt_cache_entry_t *entry) {
    if (!cache || !entry) return;

    if (entry->prev) {
        entry->prev->next = entry->next;
    } else {
        cache->head = entry->next;
    }

    if (entry->next) {
        entry->next->prev = entry->prev;
    } else {
        cache->tail = entry->prev;
    }
}

static void add_to_head(stmt_cache_t *cache, stmt_cache_entry_t *entry) {
    if (!cache || !entry) return;

    entry->next = cache->head;
    entry->prev = NULL;

    if (cache->head) {
        cache->head->prev = entry;
    }

    cache->head = entry;

    if (!cache->tail) {
        cache->tail = entry;
    }
}

static void evict_tail(stmt_cache_t *cache) {
    if (!cache || !cache->tail) return;

    stmt_cache_entry_t *tail_entry = cache->tail;
    remove_from_list(cache, tail_entry);

    uint32_t hash = hash_stmt_id(tail_entry->stmt_id);
    if (cache->entries[hash] == tail_entry) {
        cache->entries[hash] = tail_entry->next;
    }

    free(tail_entry->sql_template);
    free(tail_entry);
    cache->count--;
}

int stmt_cache_put(stmt_cache_t *cache, uint32_t stmt_id, const char *sql_template, uint16_t num_params) {
    if (!cache || !sql_template) return -1;

    pthread_mutex_lock(&cache->lock);

    uint32_t hash = hash_stmt_id(stmt_id);
    stmt_cache_entry_t *existing = cache->entries[hash];

    while (existing) {
        if (existing->stmt_id == stmt_id) {
            remove_from_list(cache, existing);
            free(existing->sql_template);
            existing->sql_template = strdup(sql_template);
            existing->num_params = num_params;
            existing->last_access = time(NULL);
            add_to_head(cache, existing);
            pthread_mutex_unlock(&cache->lock);
            return 0;
        }
        existing = existing->next;
    }

    if (cache->count >= MAX_STATEMENT_CACHE) {
        evict_tail(cache);
    }

    stmt_cache_entry_t *entry = (stmt_cache_entry_t *)calloc(1, sizeof(stmt_cache_entry_t));
    if (!entry) {
        pthread_mutex_unlock(&cache->lock);
        return -1;
    }

    entry->stmt_id = stmt_id;
    entry->sql_template = strdup(sql_template);
    entry->num_params = num_params;
    entry->last_access = time(NULL);

    add_to_head(cache, entry);
    cache->count++;

    entry->next = cache->entries[hash];
    cache->entries[hash] = entry;

    pthread_mutex_unlock(&cache->lock);
    return 0;
}

stmt_cache_entry_t *stmt_cache_get(stmt_cache_t *cache, uint32_t stmt_id) {
    if (!cache) return NULL;

    pthread_mutex_lock(&cache->lock);

    uint32_t hash = hash_stmt_id(stmt_id);
    stmt_cache_entry_t *entry = cache->entries[hash];

    while (entry) {
        if (entry->stmt_id == stmt_id) {
            remove_from_list(cache, entry);
            add_to_head(cache, entry);
            entry->last_access = time(NULL);
            pthread_mutex_unlock(&cache->lock);
            return entry;
        }
        entry = entry->next;
    }

    pthread_mutex_unlock(&cache->lock);
    return NULL;
}

void stmt_cache_remove(stmt_cache_t *cache, uint32_t stmt_id) {
    if (!cache) return;

    pthread_mutex_lock(&cache->lock);

    uint32_t hash = hash_stmt_id(stmt_id);
    stmt_cache_entry_t **prev_ptr = &cache->entries[hash];
    stmt_cache_entry_t *entry = cache->entries[hash];

    while (entry) {
        if (entry->stmt_id == stmt_id) {
            *prev_ptr = entry->next;
            remove_from_list(cache, entry);
            free(entry->sql_template);
            free(entry);
            cache->count--;
            break;
        }
        prev_ptr = &entry->next;
        entry = entry->next;
    }

    pthread_mutex_unlock(&cache->lock);
}

void stmt_cache_clear(stmt_cache_t *cache) {
    if (!cache) return;

    pthread_mutex_lock(&cache->lock);

    stmt_cache_entry_t *entry = cache->head;
    while (entry) {
        stmt_cache_entry_t *next = entry->next;
        free(entry->sql_template);
        free(entry);
        entry = next;
    }

    cache->head = NULL;
    cache->tail = NULL;
    cache->count = 0;
    memset(cache->entries, 0, sizeof(cache->entries));

    pthread_mutex_unlock(&cache->lock);
}
