#ifndef NIST_TESTS_H
#define NIST_TESTS_H

#include <stdint.h>
#include <stddef.h>

#include "config.h"

typedef enum {
    NIST_TEST_MONOBIT = 0,
    NIST_TEST_BLOCK_FREQ,
    NIST_TEST_CUSUM,
    NIST_TEST_RUNS,
    NIST_TEST_LONGEST_RUN,
    NIST_TEST_RANK,
    NIST_TEST_FFT,
    NIST_TEST_NONOVERLAPPING,
    NIST_TEST_OVERLAPPING,
    NIST_TEST_UNIVERSAL,
    NIST_TEST_LINEAR_COMPLEXITY,
    NIST_TEST_SERIAL,
    NIST_TEST_APPROX_ENTROPY,
    NIST_TEST_CUMULATIVE_SUMS,
    NIST_TEST_RANDOM_EXCURSIONS
} nist_test_id_t;

typedef struct {
    nist_test_id_t  id;
    const char     *name;
    const char     *description;
    double          p_value;
    int             passed;
    int             applicable;
} nist_test_result_t;

typedef struct {
    nist_test_result_t results[NIST_NUM_TESTS];
    int                num_tests;
    int                num_passed;
    double             overall_score;
    size_t             bit_count;
    time_t             timestamp;
} nist_test_report_t;

typedef struct {
    uint8_t *data;
    size_t   byte_len;
    int     *bits;
    size_t   bit_len;
} nist_bitstream_t;

int  nist_bitstream_init(nist_bitstream_t *bs, const uint8_t *data, size_t len);
void nist_bitstream_free(nist_bitstream_t *bs);

int  nist_run_all_tests(const uint8_t *data, size_t len, nist_test_report_t *report);

int  nist_test_monobit(const int *bits, size_t n, double *p_value);
int  nist_test_block_freq(const int *bits, size_t n, int m, double *p_value);
int  nist_test_cusum(const int *bits, size_t n, double *p_value);
int  nist_test_runs(const int *bits, size_t n, double *p_value);
int  nist_test_longest_run(const int *bits, size_t n, double *p_value);
int  nist_test_rank(const uint8_t *data, size_t len, double *p_value);
int  nist_test_fft(const int *bits, size_t n, double *p_value);
int  nist_test_nonoverlapping(const int *bits, size_t n, double *p_value);
int  nist_test_overlapping(const int *bits, size_t n, double *p_value);
int  nist_test_universal(const int *bits, size_t n, double *p_value);
int  nist_test_linear_complexity(const int *bits, size_t n, int m, double *p_value);
int  nist_test_serial(const int *bits, size_t n, int m, double *p_value);
int  nist_test_approx_entropy(const int *bits, size_t n, int m, double *p_value);
int  nist_test_cumulative_sums(const int *bits, size_t n, double *p_value);
int  nist_test_random_excursions(const int *bits, size_t n, double *p_value);

void nist_report_init(nist_test_report_t *report, size_t bit_count);
int  nist_is_test_passable(double p_value);

size_t nist_test_min_bytes(nist_test_id_t test_id);
int    nist_test_is_quick(nist_test_id_t test_id);
size_t nist_test_estimate_time_ms(nist_test_id_t test_id, size_t byte_len);
int    nist_run_quick_tests(const uint8_t *data, size_t len, nist_test_report_t *report);
int    nist_run_adaptive_tests(const uint8_t *data, size_t len, nist_test_report_t *report,
                                int quick_mode);

#endif
