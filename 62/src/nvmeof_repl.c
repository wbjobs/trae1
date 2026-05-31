#include "nvmeof_repl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void nvmeof_repl_mgr_init(struct nvmeof_repl_mgr *mgr, enum nvmeof_repl_role role)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->role = role;
    pthread_mutex_init(&mgr->mu, NULL);
}

void nvmeof_repl_mgr_fini(struct nvmeof_repl_mgr *mgr)
{
    mgr->running = false;
    if (mgr->health_thread) {
        pthread_join(mgr->health_thread, NULL);
        mgr->health_thread = 0;
    }
    for (size_t i = 0; i < mgr->ns_count; i++) {
        for (size_t j = 0; j < mgr->nses[i].backup_count; j++) {
            struct nvmeof_repl_node *n = &mgr->nses[i].backups[j];
            if (n->connected && n->ctrlr) {
                spdk_nvme_detach((struct spdk_nvme_ctrlr *)n->ctrlr);
                n->ctrlr = NULL;
                n->connected = false;
            }
        }
        pthread_mutex_destroy(&mgr->nses[i].mu);
    }
    pthread_mutex_destroy(&mgr->mu);
}

int nvmeof_repl_ns_add(struct nvmeof_repl_mgr *mgr, uint32_t local_nsid,
                       const char *bdev_name)
{
    pthread_mutex_lock(&mgr->mu);
    if (mgr->ns_count >= NVME_REPL_MAX_NS) {
        pthread_mutex_unlock(&mgr->mu);
        return -ENOMEM;
    }
    for (size_t i = 0; i < mgr->ns_count; i++) {
        if (mgr->nses[i].local_nsid == local_nsid) {
            pthread_mutex_unlock(&mgr->mu);
            return 0;
        }
    }
    struct nvmeof_repl_ns *rns = &mgr->nses[mgr->ns_count++];
    memset(rns, 0, sizeof(*rns));
    rns->local_nsid = local_nsid;
    snprintf(rns->bdev_name, sizeof(rns->bdev_name), "%s", bdev_name);
    pthread_mutex_init(&rns->mu, NULL);
    pthread_mutex_unlock(&mgr->mu);
    return 0;
}

int nvmeof_repl_backup_add(struct nvmeof_repl_mgr *mgr, uint32_t local_nsid,
                           const char *traddr, const char *trsvcid,
                           const char *nqn, uint32_t remote_nsid)
{
    struct nvmeof_repl_ns *rns = NULL;
    pthread_mutex_lock(&mgr->mu);
    for (size_t i = 0; i < mgr->ns_count; i++) {
        if (mgr->nses[i].local_nsid == local_nsid) { rns = &mgr->nses[i]; break; }
    }
    if (!rns) { pthread_mutex_unlock(&mgr->mu); return -ENOENT; }

    pthread_mutex_lock(&rns->mu);
    if (rns->backup_count >= NVME_REPL_MAX_BACKUPS) {
        pthread_mutex_unlock(&rns->mu);
        pthread_mutex_unlock(&mgr->mu);
        return -ENOMEM;
    }
    struct nvmeof_repl_node *node = &rns->backups[rns->backup_count++];
    memset(node, 0, sizeof(*node));
    snprintf(node->traddr, sizeof(node->traddr), "%s", traddr);
    snprintf(node->trsvcid, sizeof(node->trsvcid), "%s", trsvcid);
    snprintf(node->nqn, sizeof(node->nqn), "%s", nqn);
    node->remote_nsid = remote_nsid;
    node->state = NVME_REPL_STATE_OFFLINE;
    pthread_mutex_unlock(&rns->mu);
    pthread_mutex_unlock(&mgr->mu);
    return 0;
}

static void *repl_connect_worker(void *arg)
{
    struct nvmeof_repl_node *node = arg;
    struct spdk_nvme_transport_id trid = {};
    trid.trtype = SPDK_NVME_TRANSPORT_RDMA;
    snprintf(trid.traddr, sizeof(trid.traddr), "%s", node->traddr);
    snprintf(trid.trsvcid, sizeof(trid.trsvcid), "%s", node->trsvcid);
    snprintf(trid.subnqn, sizeof(trid.subnqn), "%s", node->nqn);

    node->state = NVME_REPL_STATE_CONNECTING;

    struct spdk_nvme_ctrlr *ctrlr = spdk_nvme_connect(&trid, NULL, 0);
    if (!ctrlr) {
        node->state = NVME_REPL_STATE_OFFLINE;
        node->last_fail_ts_ns = now_ns();
        node->consecutive_fails++;
        return NULL;
    }

    struct spdk_nvme_ns *ns = spdk_nvme_ctrlr_get_ns(ctrlr, node->remote_nsid);
    if (!ns) {
        spdk_nvme_detach(ctrlr);
        node->state = NVME_REPL_STATE_OFFLINE;
        node->last_fail_ts_ns = now_ns();
        node->consecutive_fails++;
        return NULL;
    }

    node->ctrlr = ctrlr;
    node->ns = ns;
    node->connected = true;
    node->state = NVME_REPL_STATE_HEALTHY;
    node->last_healthy_ts_ns = now_ns();
    node->consecutive_fails = 0;
    return NULL;
}

int nvmeof_repl_backup_connect(struct nvmeof_repl_mgr *mgr, uint32_t local_nsid,
                               size_t backup_idx)
{
    struct nvmeof_repl_ns *rns = NULL;
    pthread_mutex_lock(&mgr->mu);
    for (size_t i = 0; i < mgr->ns_count; i++) {
        if (mgr->nses[i].local_nsid == local_nsid) { rns = &mgr->nses[i]; break; }
    }
    if (!rns || backup_idx >= rns->backup_count) {
        pthread_mutex_unlock(&mgr->mu);
        return -ENOENT;
    }
    struct nvmeof_repl_node *node = &rns->backups[backup_idx];
    pthread_mutex_unlock(&mgr->mu);

    if (node->connected && node->state == NVME_REPL_STATE_HEALTHY)
        return 0;

    pthread_t tid;
    if (pthread_create(&tid, NULL, repl_connect_worker, node) != 0)
        return -EIO;
    pthread_detach(tid);
    return 0;
}

struct repl_write_ctx {
    struct nvmeof_repl_node *node;
    uint64_t *completed_mask;
    size_t bit;
    bool *failed;
};

static void repl_write_complete(void *cb_arg,
                                const struct spdk_nvme_cpl *cpl)
{
    struct repl_write_ctx *ctx = cb_arg;
    if (spdk_nvme_cpl_is_error(cpl)) {
        *ctx->failed = true;
        ctx->node->consecutive_fails++;
        ctx->node->last_fail_ts_ns = now_ns();
        if (ctx->node->consecutive_fails >= 3)
            ctx->node->state = NVME_REPL_STATE_DEGRADED;
    } else {
        __atomic_or_fetch(ctx->completed_mask, 1ULL << ctx->bit, __ATOMIC_SEQ_CST);
        ctx->node->consecutive_fails = 0;
        ctx->node->last_healthy_ts_ns = now_ns();
        ctx->node->state = NVME_REPL_STATE_HEALTHY;
    }
    free(ctx);
}

int nvmeof_repl_write(struct nvmeof_repl_mgr *mgr, uint32_t local_nsid,
                      uint64_t lba, uint64_t lba_count, const void *data,
                      uint64_t *completed_mask)
{
    struct nvmeof_repl_ns *rns = NULL;
    pthread_mutex_lock(&mgr->mu);
    for (size_t i = 0; i < mgr->ns_count; i++) {
        if (mgr->nses[i].local_nsid == local_nsid) { rns = &mgr->nses[i]; break; }
    }
    if (!rns) { pthread_mutex_unlock(&mgr->mu); return -ENOENT; }
    pthread_mutex_unlock(&mgr->mu);

    pthread_mutex_lock(&rns->mu);
    rns->total_writes++;
    rns->total_write_bytes += lba_count * 512;
    uint64_t start_ns = now_ns();
    uint64_t mask = 0;
    bool any_failed = false;

    for (size_t i = 0; i < rns->backup_count; i++) {
        struct nvmeof_repl_node *node = &rns->backups[i];
        if (!node->connected || !node->ns ||
            node->state == NVME_REPL_STATE_OFFLINE) {
            any_failed = true;
            continue;
        }
        struct repl_write_ctx *ctx = malloc(sizeof(*ctx));
        if (!ctx) { any_failed = true; continue; }
        ctx->node = node;
        ctx->completed_mask = &mask;
        ctx->bit = i;
        ctx->failed = &any_failed;

        struct spdk_nvme_qpair *qpair =
            spdk_nvme_ctrlr_alloc_io_qpair((struct spdk_nvme_ctrlr *)node->ctrlr, NULL, 0);
        if (!qpair) { free(ctx); any_failed = true; continue; }

        int rc = spdk_nvme_ns_cmd_writev((struct spdk_nvme_ns *)node->ns,
                                         qpair, (void *)data, lba, lba_count,
                                         repl_write_complete, ctx, 0);
        if (rc != 0) {
            free(ctx);
            spdk_nvme_ctrlr_free_io_qpair(qpair);
            any_failed = true;
            continue;
        }
    }

    uint64_t elapsed_ns = now_ns() - start_ns;
    if (rns->avg_replication_latency_ns == 0)
        rns->avg_replication_latency_ns = elapsed_ns;
    else
        rns->avg_replication_latency_ns =
            (rns->avg_replication_latency_ns * 7 + elapsed_ns) / 8;

    rns->last_replication_ts_ns = now_ns();
    if (any_failed) rns->failed_writes++;
    else rns->replicated_bytes += lba_count * 512;

    *completed_mask = mask;
    pthread_mutex_unlock(&rns->mu);
    return any_failed ? -EIO : 0;
}

void nvmeof_repl_health_check(struct nvmeof_repl_mgr *mgr)
{
    uint64_t now = now_ns();
    pthread_mutex_lock(&mgr->mu);
    for (size_t i = 0; i < mgr->ns_count; i++) {
        struct nvmeof_repl_ns *rns = &mgr->nses[i];
        pthread_mutex_lock(&rns->mu);
        for (size_t j = 0; j < rns->backup_count; j++) {
            struct nvmeof_repl_node *n = &rns->backups[j];
            if (!n->connected) {
                if (n->state == NVME_REPL_STATE_OFFLINE &&
                    n->last_fail_ts_ns > 0 &&
                    now - n->last_fail_ts_ns > 5 * 1000000000ULL) {
                    n->last_fail_ts_ns = now;
                }
                continue;
            }
            if (n->last_healthy_ts_ns > 0 &&
                now - n->last_healthy_ts_ns > NVME_REPL_FAILOVER_MS * 1000000ULL) {
                n->state = NVME_REPL_STATE_DEGRADED;
            }
        }
        pthread_mutex_unlock(&rns->mu);
    }
    pthread_mutex_unlock(&mgr->mu);
}

static void *repl_health_thread(void *arg)
{
    struct nvmeof_repl_mgr *mgr = arg;
    while (mgr->running) {
        nvmeof_repl_health_check(mgr);
        usleep(NVME_REPL_HEALTH_MS * 1000);
    }
    return NULL;
}

void *nvmeof_repl_health_thread_fn(void *arg)
{
    return repl_health_thread(arg);
}

void nvmeof_repl_foreach(struct nvmeof_repl_mgr *mgr,
                         nvmeof_repl_status_fn fn, void *ctx)
{
    pthread_mutex_lock(&mgr->mu);
    for (size_t i = 0; i < mgr->ns_count; i++) {
        struct nvmeof_repl_ns *rns = &mgr->nses[i];
        pthread_mutex_lock(&rns->mu);
        for (size_t j = 0; j < rns->backup_count; j++) {
            fn(rns->local_nsid, rns->bdev_name, j, &rns->backups[j],
               rns->total_writes, rns->failed_writes,
               rns->avg_replication_latency_ns, ctx);
        }
        pthread_mutex_unlock(&rns->mu);
    }
    pthread_mutex_unlock(&mgr->mu);
}
