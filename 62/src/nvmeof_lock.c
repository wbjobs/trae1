#include "nvmeof_lock.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void nvmeof_lock_mgr_init(struct nvmeof_lock_manager *mgr)
{
    memset(mgr, 0, sizeof(*mgr));
    pthread_mutex_init(&mgr->mu, NULL);
}

void nvmeof_lock_mgr_fini(struct nvmeof_lock_manager *mgr)
{
    for (uint32_t i = 0; i < mgr->table_count; i++)
        pthread_mutex_destroy(&mgr->tables[i].mu);
    pthread_mutex_destroy(&mgr->mu);
}

static struct nvmeof_lock_table *find_table(struct nvmeof_lock_manager *mgr,
                                            uint32_t nsid)
{
    for (uint32_t i = 0; i < mgr->table_count; i++)
        if (mgr->tables[i].nsid == nsid) return &mgr->tables[i];
    return NULL;
}

int nvmeof_lock_table_add(struct nvmeof_lock_manager *mgr, uint32_t nsid)
{
    pthread_mutex_lock(&mgr->mu);
    if (find_table(mgr, nsid)) {
        pthread_mutex_unlock(&mgr->mu);
        return 0;
    }
    if (mgr->table_count >= NVME_LOCK_MAX_NAMESPACES) {
        pthread_mutex_unlock(&mgr->mu);
        return -ENOMEM;
    }
    struct nvmeof_lock_table *t = &mgr->tables[mgr->table_count++];
    memset(t, 0, sizeof(*t));
    t->nsid = nsid;
    pthread_mutex_init(&t->mu, NULL);
    pthread_mutex_unlock(&mgr->mu);
    return 0;
}

void nvmeof_lock_table_remove(struct nvmeof_lock_manager *mgr, uint32_t nsid)
{
    pthread_mutex_lock(&mgr->mu);
    for (uint32_t i = 0; i < mgr->table_count; i++) {
        if (mgr->tables[i].nsid == nsid) {
            pthread_mutex_destroy(&mgr->tables[i].mu);
            memmove(&mgr->tables[i], &mgr->tables[i + 1],
                    (mgr->table_count - i - 1) * sizeof(mgr->tables[0]));
            mgr->table_count--;
            break;
        }
    }
    pthread_mutex_unlock(&mgr->mu);
}

static bool ranges_overlap(uint64_t s1, uint64_t c1, uint64_t s2, uint64_t c2)
{
    if (c1 == 0 || c2 == 0) return false;
    uint64_t e1 = s1 + c1 - 1;
    uint64_t e2 = s2 + c2 - 1;
    return !(e1 < s2 || e2 < s1);
}

static int find_expired_slot(struct nvmeof_lock_table *t)
{
    for (uint32_t i = 0; i < t->count; i++)
        if (!t->regions[i].active) return (int)i;
    if (t->count < NVME_LOCK_MAX_REGIONS) return (int)t->count++;
    return -1;
}

int nvmeof_lock_acquire(struct nvmeof_lock_manager *mgr, uint32_t nsid,
                        nvmeof_lock_owner_t owner,
                        uint64_t lba_start, uint64_t lba_count,
                        uint32_t flags, uint32_t timeout_ms)
{
    struct nvmeof_lock_table *t = find_table(mgr, nsid);
    if (!t) return -ENOENT;

    pthread_mutex_lock(&t->mu);

    uint64_t now = now_ns();
    uint64_t timeout_ns = (timeout_ms > 0 ? timeout_ms : NVME_LOCK_TIMEOUT_SEC * 1000) * 1000000ULL;

    for (uint32_t i = 0; i < t->count; i++) {
        struct nvmeof_region_lock *r = &t->regions[i];
        if (!r->active) continue;
        if (now - r->acquired_at_ns > r->timeout_ms * 1000000ULL) {
            r->active = false;
            continue;
        }
        if (r->owner == owner) continue;
        if (ranges_overlap(r->lba_start, r->lba_count, lba_start, lba_count)) {
            if (flags & NVME_LOCK_FLAG_WRITE) {
                pthread_mutex_unlock(&t->mu);
                return -EBUSY;
            }
        }
    }

    int slot = find_expired_slot(t);
    if (slot < 0) {
        pthread_mutex_unlock(&t->mu);
        return -ENOMEM;
    }

    struct nvmeof_region_lock *nr = &t->regions[slot];
    nr->owner = owner;
    nr->lba_start = lba_start;
    nr->lba_count = lba_count;
    nr->flags = flags;
    nr->acquired_at_ns = now;
    nr->timeout_ms = timeout_ms > 0 ? timeout_ms : NVME_LOCK_TIMEOUT_SEC * 1000;
    nr->active = true;

    pthread_mutex_unlock(&t->mu);
    return 0;
}

int nvmeof_lock_release(struct nvmeof_lock_manager *mgr, uint32_t nsid,
                        nvmeof_lock_owner_t owner,
                        uint64_t lba_start, uint64_t lba_count)
{
    struct nvmeof_lock_table *t = find_table(mgr, nsid);
    if (!t) return -ENOENT;

    pthread_mutex_lock(&t->mu);
    for (uint32_t i = 0; i < t->count; i++) {
        struct nvmeof_region_lock *r = &t->regions[i];
        if (!r->active) continue;
        if (r->owner != owner) continue;
        if (r->lba_start == lba_start && r->lba_count == lba_count) {
            r->active = false;
            pthread_mutex_unlock(&t->mu);
            return 0;
        }
    }
    pthread_mutex_unlock(&t->mu);
    return -ENOENT;
}

void nvmeof_lock_release_all(struct nvmeof_lock_manager *mgr, uint32_t nsid,
                             nvmeof_lock_owner_t owner)
{
    struct nvmeof_lock_table *t = find_table(mgr, nsid);
    if (!t) return;
    pthread_mutex_lock(&t->mu);
    for (uint32_t i = 0; i < t->count; i++) {
        if (t->regions[i].active && t->regions[i].owner == owner)
            t->regions[i].active = false;
    }
    pthread_mutex_unlock(&t->mu);
}

void nvmeof_lock_cleanup_expired(struct nvmeof_lock_manager *mgr)
{
    uint64_t now = now_ns();
    pthread_mutex_lock(&mgr->mu);
    for (uint32_t ti = 0; ti < mgr->table_count; ti++) {
        struct nvmeof_lock_table *t = &mgr->tables[ti];
        pthread_mutex_lock(&t->mu);
        for (uint32_t i = 0; i < t->count; i++) {
            struct nvmeof_region_lock *r = &t->regions[i];
            if (!r->active) continue;
            if (now - r->acquired_at_ns > r->timeout_ms * 1000000ULL)
                r->active = false;
        }
        pthread_mutex_unlock(&t->mu);
    }
    pthread_mutex_unlock(&mgr->mu);
}

void nvmeof_lock_foreach(struct nvmeof_lock_manager *mgr,
                         nvmeof_lock_visitor_fn fn, void *ctx)
{
    uint64_t now = now_ns();
    pthread_mutex_lock(&mgr->mu);
    for (uint32_t ti = 0; ti < mgr->table_count; ti++) {
        struct nvmeof_lock_table *t = &mgr->tables[ti];
        pthread_mutex_lock(&t->mu);
        for (uint32_t i = 0; i < t->count; i++) {
            struct nvmeof_region_lock *r = &t->regions[i];
            if (!r->active) continue;
            uint64_t age_ms = (now - r->acquired_at_ns) / 1000000ULL;
            fn(t->nsid, r->owner, r->lba_start, r->lba_count,
               r->flags, age_ms, ctx);
        }
        pthread_mutex_unlock(&t->mu);
    }
    pthread_mutex_unlock(&mgr->mu);
}
