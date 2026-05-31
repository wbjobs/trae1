/* SPDX-License-Identifier: BSD-3-Clause
 * Flow Table - 5-tuple hash table with per-flow aggregation
 */
#include "te_header.h"

/*
 * rte_hash maps key -> pointer to struct te_flow allocated from
 * a per-lcore mempool (or rte_malloc here). For line rate we rely
 * on rte_hash's RCU-free style lookup (we use rte_hash_lookup_data
 * which is O(1) average).
 *
 * For simplicity and to avoid per-lcore complexity, we use a single
 * global hash table. On a production system you'd shard by lcore;
 * here we add a spinlock around mutations to stay correct.
 */

static struct rte_hash *g_ht = NULL;
rte_spinlock_t g_ht_lock = RTE_SPINLOCK_INITIALIZER;

/* Export lock for ML classifier's te_ml_set_true_label */

int te_flow_table_init(void)
{
    char name[RTE_HASH_NAMESIZE];
    struct rte_hash_parameters params;

    snprintf(name, sizeof(name), "te_flow_ht");

    memset(&params, 0, sizeof(params));
    params.name            = name;
    params.entries         = TE_FLOW_TABLE_SIZE;
    params.key_len         = sizeof(struct te_5tuple);
    params.hash_func       = rte_jhash;
    params.hash_func_init_val = 0;
    params.socket_id       = rte_socket_id();
    params.extra_flag      = 0;

    g_ht = rte_hash_create(&params);
    if (g_ht == NULL) {
        RTE_LOG(ERR, USER1, "Failed to create flow hash table\n");
        return -1;
    }

    return 0;
}

void te_flow_table_fini(void)
{
    if (g_ht == NULL)
        return;

    /* Free all flow records before destroying the table */
    void *next_key;
    void *data;
    uint32_t next = 0;
    while (rte_hash_iterate(g_ht, &next_key, &data, &next) >= 0) {
        if (data) {
            struct te_flow *f = (struct te_flow *)data;
            if (f->features) {
                if (g_te.feature_pool)
                    rte_mempool_put(g_te.feature_pool, f->features);
                else
                    rte_free(f->features);
            }
            rte_free(data);
        }
    }

    rte_hash_free(g_ht);
    g_ht = NULL;
}

struct te_flow * te_flow_get_or_create(const struct te_5tuple *key,
                                        uint32_t pkt_len, uint64_t tsc,
                                        bool *is_new)
{
    struct te_flow *flow = NULL;
    int ret;

    /* Fast path: lookup without lock if value is stable enough.
     * We still take lock for insert. */
    rte_spinlock_lock(&g_ht_lock);

    ret = rte_hash_lookup_data(g_ht, key, (void **)&flow);
    if (ret >= 0 && flow != NULL) {
        /* update */
        flow->pkts       += 1;
        flow->bytes      += pkt_len;
        flow->last_tsc    = tsc;
        if (is_new) *is_new = false;
        rte_spinlock_unlock(&g_ht_lock);
        return flow;
    }

    /* Miss: create a new flow record */
    flow = (struct te_flow *)rte_malloc("te_flow",
                                         sizeof(struct te_flow),
                                         RTE_CACHE_LINE_SIZE);
    if (unlikely(flow == NULL)) {
        rte_spinlock_unlock(&g_ht_lock);
        return NULL;
    }
    memset(flow, 0, sizeof(*flow));
    flow->key        = *key;
    flow->pkts       = 1;
    flow->bytes      = pkt_len;
    flow->first_tsc  = tsc;
    flow->last_tsc   = tsc;
    flow->category   = TE_CAT_UNKNOWN;
    flow->flags      = 0;
    flow->dpi_category = TE_CAT_UNKNOWN;
    flow->ml_category  = TE_CAT_UNKNOWN;

    /* Allocate ML feature collector from pre-allocated pool or rte_malloc */
    if (g_te.feature_pool) {
        flow->features = (struct te_flow_features *)
            rte_mempool_get(g_te.feature_pool);
    }
    if (flow->features == NULL) {
        /* fallback to rte_malloc if pool empty */
        flow->features = (struct te_flow_features *)rte_malloc(
            "te_flow_features",
            sizeof(struct te_flow_features),
            RTE_CACHE_LINE_SIZE);
    }
    if (flow->features != NULL)
        memset(flow->features, 0, sizeof(*flow->features));

    ret = rte_hash_add_key_data(g_ht, key, flow);
    if (unlikely(ret < 0)) {
        if (flow->features != NULL) {
            if (g_te.feature_pool)
                rte_mempool_put(g_te.feature_pool, flow->features);
            else
                rte_free(flow->features);
        }
        rte_free(flow);
        rte_spinlock_unlock(&g_ht_lock);
        return NULL;
    }

    if (is_new) *is_new = true;
    rte_spinlock_unlock(&g_ht_lock);
    return flow;
}

/*
 * Expire flows whose last_tsc is older than timeout_cycles.
 * Called periodically from a dedicated lcore (or main thread).
 */
void te_flow_expire(uint64_t now_tsc, uint64_t timeout_cycles)
{
    void *next_key;
    void *data;
    uint32_t next = 0;

    rte_spinlock_lock(&g_ht_lock);
    while (rte_hash_iterate(g_ht, &next_key, &data, &next) >= 0) {
        struct te_flow *f = (struct te_flow *)data;
        if (f && (now_tsc - f->last_tsc) > timeout_cycles) {
            /* Finalize feature vector if ML enabled */
            if (f->features && g_te.ml_enabled &&
                !f->features->feature_vec_ready) {
                te_ml_feature_extract(f->features, &f->key,
                                       f->features->feature_vec);
            }

            /* Send final aggregate result before removal */
            if (f->category != TE_CAT_UNKNOWN)
                te_result_enqueue_flow_aggregate(f);

            /* Enqueue training sample if true label available */
            if (f->has_true_label && f->features &&
                f->features->feature_vec_ready) {
                te_ml_enqueue_training_sample(f);
            }

            /* Free features */
            if (f->features) {
                if (g_te.feature_pool)
                    rte_mempool_put(g_te.feature_pool, f->features);
                else
                    rte_free(f->features);
            }
            rte_hash_del_key(g_ht, next_key);
            rte_free(f);
        }
    }
    rte_spinlock_unlock(&g_ht_lock);
}

uint32_t te_flow_count(void)
{
    if (g_ht == NULL)
        return 0;
    return (uint32_t)rte_hash_count(g_ht);
}
