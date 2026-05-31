#include "sctp_transfer.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define GF_SIZE 256
#define GF_POLY 0x11d

static uint8_t gf_log[GF_SIZE];
static uint8_t gf_exp[GF_SIZE * 2];
static bool gf_initialized = false;

static void gf_init(void)
{
    if (gf_initialized) return;

    uint8_t x = 1;
    for (int i = 0; i < GF_SIZE - 1; i++) {
        gf_exp[i] = x;
        gf_exp[i + GF_SIZE - 1] = x;
        gf_log[x] = (uint8_t)i;
        x <<= 1;
        if (x & GF_SIZE)
            x ^= GF_POLY;
    }
    gf_exp[GF_SIZE - 1] = gf_exp[0];
    gf_log[0] = 0;
    gf_initialized = true;
}

static inline uint8_t gf_add(uint8_t a, uint8_t b)
{
    return a ^ b;
}

static inline uint8_t gf_mul(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return gf_exp[gf_log[a] + gf_log[b]];
}

static inline uint8_t gf_div(uint8_t a, uint8_t b)
{
    if (a == 0) return 0;
    if (b == 0) return 0;
    return gf_exp[gf_log[a] + GF_SIZE - 1 - gf_log[b]];
}

static void gf_poly_mul(uint8_t *dst, const uint8_t *a, int len_a,
                         const uint8_t *b, int len_b)
{
    memset(dst, 0, len_a + len_b - 1);
    for (int i = 0; i < len_a; i++) {
        if (a[i] == 0) continue;
        uint8_t log_a = gf_log[a[i]];
        for (int j = 0; j < len_b; j++) {
            if (b[j] == 0) continue;
            dst[i + j] ^= gf_exp[log_a + gf_log[b[j]]];
        }
    }
}

static void gf_poly_eval(const uint8_t *poly, int len,
                          const uint8_t *x, uint8_t *y, int n)
{
    for (int i = 0; i < n; i++) {
        uint8_t result = poly[len - 1];
        for (int j = len - 2; j >= 0; j--) {
            result = gf_mul(result, x[i]) ^ poly[j];
        }
        y[i] = result;
    }
}

int fec_encode(const uint8_t **data, size_t data_len,
                uint8_t **parity, size_t *parity_len,
                int num_data, int num_parity)
{
    if (!data || !parity || !parity_len ||
        num_data <= 0 || num_parity <= 0)
        return -1;

    gf_init();

    uint8_t gen[num_parity + 1];
    gen[0] = 1;
    for (int i = 0; i < num_parity; i++) {
        uint8_t factor[] = { (uint8_t)i, 1 };
        uint8_t tmp[num_parity + 1];
        gf_poly_mul(tmp, gen, i + 1, factor, 2);
        memcpy(gen, tmp, i + 2);
    }

    uint8_t roots[num_parity];
    for (int i = 0; i < num_parity; i++)
        roots[i] = (uint8_t)i;

    for (int p = 0; p < num_parity; p++) {
        parity[p] = (uint8_t *)malloc(data_len);
        if (!parity[p]) {
            for (int j = 0; j < p; j++)
                free(parity[j]);
            return -1;
        }
        memset(parity[p], 0, data_len);
    }

    for (size_t byte = 0; byte < data_len; byte++) {
        uint8_t data_shards[num_data];
        for (int d = 0; d < num_data; d++) {
            data_shards[d] = data[d][byte];
        }

        uint8_t parity_vals[num_parity];
        gf_poly_eval(data_shards, num_data, roots, parity_vals, num_parity);

        for (int p = 0; p < num_parity; p++) {
            parity[p][byte] = parity_vals[p];
        }
    }

    *parity_len = data_len;
    return 0;
}

int fec_decode(uint8_t **data, int *erasures, int num_erasures,
                uint8_t **parity, size_t data_len,
                int num_data, int num_parity)
{
    if (!data || !erasures || !parity || num_erasures <= 0 ||
        num_erasures > num_parity)
        return -1;

    gf_init();

    for (int e = 0; e < num_erasures; e++) {
        int idx = erasures[e];
        if (idx < num_data) {
            if (!data[idx]) {
                data[idx] = (uint8_t *)malloc(data_len);
                if (!data[idx]) return -1;
            }
            memset(data[idx], 0, data_len);
        }
    }

    for (size_t byte = 0; byte < data_len; byte++) {
        uint8_t syndrome[num_parity];
        for (int p = 0; p < num_parity; p++) {
            syndrome[p] = parity[p][byte];
            for (int d = 0; d < num_data; d++) {
                syndrome[p] ^= gf_mul(data[d][byte], gf_exp[(p * d) % 255]);
            }
        }

        bool ok = true;
        for (int p = 0; p < num_parity; p++) {
            if (syndrome[p] != 0) { ok = false; break; }
        }
        if (ok) continue;

        uint8_t lambda[num_parity + 1];
        uint8_t b[num_parity + 1];
        uint8_t c[num_parity + 1];
        int r = 0;
        int l = 0;

        memset(lambda, 0, sizeof(lambda));
        memset(b, 0, sizeof(b));
        memset(c, 0, sizeof(c));
        lambda[0] = 1;
        b[0] = 1;

        for (int n = 0; n < num_parity; n++) {
            uint8_t delta = syndrome[n];
            for (int i = 1; i <= l; i++) {
                delta ^= gf_mul(lambda[i], syndrome[n - i]);
            }

            if (delta != 0) {
                memset(c, 0, sizeof(c));
                for (int i = 0; i <= l; i++) {
                    c[i] = lambda[i];
                }
                uint8_t log_delta = gf_log[delta];
                for (int i = 0; i <= r; i++) {
                    if (b[i] != 0) {
                        c[n - r + i] ^= gf_exp[log_delta + gf_log[b[i]]];
                    }
                }

                if (2 * l <= n + r) {
                    l = n + 1 - r;
                    r = n + 1 - l;
                    uint8_t inv_delta = gf_div(1, delta);
                    for (int i = 0; i <= r; i++) {
                        b[i] = gf_mul(lambda[i], inv_delta);
                    }
                }
                memcpy(lambda, c, sizeof(lambda));
            }
        }

        uint8_t omega[num_parity];
        memset(omega, 0, sizeof(omega));
        for (int i = 0; i < num_parity; i++) {
            for (int j = 0; j <= l && i - j >= 0; j++) {
                omega[i] ^= gf_mul(lambda[j], syndrome[i - j]);
            }
        }

        uint8_t lambda_deriv[num_parity];
        memset(lambda_deriv, 0, sizeof(lambda_deriv));
        for (int i = 0; i < l; i++) {
            lambda_deriv[i] = lambda[i + 1];
        }

        for (int e = 0; e < num_erasures; e++) {
            int idx = erasures[e];
            if (idx >= num_data) continue;

            uint8_t x = gf_exp[idx % 255];
            uint8_t x_inv = gf_div(1, x);

            uint8_t lambda_x = 0;
            uint8_t omega_x = 0;
            uint8_t lambda_deriv_x = 0;

            for (int i = l; i >= 0; i--) {
                lambda_x = gf_mul(lambda_x, x_inv) ^ lambda[i];
            }
            for (int i = l - 1; i >= 0; i--) {
                omega_x = gf_mul(omega_x, x_inv) ^ omega[i];
                lambda_deriv_x = gf_mul(lambda_deriv_x, x_inv) ^ lambda_deriv[i];
            }

            if (lambda_deriv_x != 0) {
                uint8_t correction = gf_div(omega_x, lambda_deriv_x);
                data[idx][byte] ^= correction;
            }
        }
    }

    return 0;
}

void fec_group_init(fec_group_t *group, uint32_t group_id,
                    int data_shards, int parity_shards)
{
    if (!group) return;

    memset(group, 0, sizeof(*group));
    group->group_id = group_id;
    group->data_shards = (uint8_t)data_shards;
    group->parity_shards = (uint8_t)parity_shards;
    group->received_count = 0;
    group->complete = false;

    for (int i = 0; i < data_shards; i++) {
        group->data[i] = NULL;
        group->data_len[i] = 0;
    }
    for (int i = 0; i < parity_shards; i++) {
        group->parity[i] = NULL;
    }
}

void fec_group_free(fec_group_t *group)
{
    if (!group) return;

    for (int i = 0; i < group->data_shards; i++) {
        if (group->data[i]) {
            free(group->data[i]);
            group->data[i] = NULL;
        }
    }
    for (int i = 0; i < group->parity_shards; i++) {
        if (group->parity[i]) {
            free(group->parity[i]);
            group->parity[i] = NULL;
        }
    }
    group->received_count = 0;
    group->complete = false;
}

int fec_group_add_data(fec_group_t *group, uint32_t chunk_id,
                        const uint8_t *data, size_t len)
{
    if (!group || !data || len == 0) return -1;

    for (int i = 0; i < group->data_shards; i++) {
        if (group->chunk_ids[i] == chunk_id && group->data[i]) {
            return 0;
        }
    }

    for (int i = 0; i < group->data_shards; i++) {
        if (group->data[i] == NULL) {
            group->data[i] = (uint8_t *)malloc(len);
            if (!group->data[i]) return -1;
            memcpy(group->data[i], data, len);
            group->data_len[i] = len;
            group->chunk_ids[i] = chunk_id;
            group->received_count++;
            return 0;
        }
    }

    return -1;
}

int fec_group_generate_parity(fec_group_t *group)
{
    if (!group || group->received_count < group->data_shards)
        return -1;

    size_t max_len = 0;
    for (int i = 0; i < group->data_shards; i++) {
        if (group->data_len[i] > max_len)
            max_len = group->data_len[i];
    }
    if (max_len == 0) return -1;

    const uint8_t *data_ptrs[FEC_DATA_SHARDS];
    uint8_t *parity_ptrs[FEC_PARITY_SHARDS];
    uint8_t *padding = NULL;

    padding = (uint8_t *)calloc(1, max_len);
    if (!padding) return -1;

    for (int i = 0; i < group->data_shards; i++) {
        if (group->data[i]) {
            if (group->data_len[i] < max_len) {
                uint8_t *tmp = realloc(group->data[i], max_len);
                if (!tmp) {
                    free(padding);
                    return -1;
                }
                group->data[i] = tmp;
                memset(group->data[i] + group->data_len[i], 0,
                       max_len - group->data_len[i]);
                group->data_len[i] = max_len;
            }
            data_ptrs[i] = group->data[i];
        } else {
            data_ptrs[i] = padding;
        }
    }

    size_t parity_len;
    int ret = fec_encode(data_ptrs, max_len, parity_ptrs, &parity_len,
                          group->data_shards, group->parity_shards);

    if (ret == 0) {
        for (int i = 0; i < group->parity_shards; i++) {
            group->parity[i] = parity_ptrs[i];
        }
    }

    free(padding);
    return ret;
}

int fec_group_restore(fec_group_t *group, uint32_t missing_idx)
{
    if (!group) return -1;

    int missing = 0;
    for (int i = 0; i < group->data_shards; i++) {
        if (group->data[i] == NULL) missing++;
    }

    if (missing == 0) {
        group->complete = true;
        return 0;
    }

    if (missing > group->parity_shards)
        return -1;

    int erasures[FEC_DATA_SHARDS];
    int num_erasures = 0;
    for (int i = 0; i < group->data_shards; i++) {
        if (group->data[i] == NULL) {
            erasures[num_erasures++] = i;
        }
    }

    size_t data_len = 0;
    for (int i = 0; i < group->data_shards; i++) {
        if (group->data_len[i] > data_len)
            data_len = group->data_len[i];
    }

    if (data_len == 0) return -1;

    uint8_t *data_ptrs[FEC_DATA_SHARDS];
    for (int i = 0; i < group->data_shards; i++) {
        data_ptrs[i] = group->data[i];
    }

    uint8_t *parity_ptrs[FEC_PARITY_SHARDS];
    for (int i = 0; i < group->parity_shards; i++) {
        parity_ptrs[i] = group->parity[i];
    }

    int ret = fec_decode(data_ptrs, erasures, num_erasures,
                          parity_ptrs, data_len,
                          group->data_shards, group->parity_shards);

    if (ret == 0) {
        for (int i = 0; i < group->data_shards; i++) {
            if (data_ptrs[i] && !group->data[i]) {
                group->data[i] = data_ptrs[i];
                group->data_len[i] = data_len;
                group->received_count++;
            }
        }
        group->complete = true;
    }

    return ret;
}
