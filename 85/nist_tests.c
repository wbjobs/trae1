#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <float.h>

#include "nist_tests.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double erfc_custom(double x)
{
    double t = 1.0 / (1.0 + 0.2316419 * fabs(x));
    double y = 1.0 - (((((1.061405429 * t
                      - 1.453152027) * t)
                      + 1.421413741) * t
                      - 0.284496736) * t
                      + 0.254829592) * t
                      * exp(-x * x);
    return x < 0 ? 2.0 - y : y;
}

static double igamc(double a, double x)
{
    if (x < 0.0 || a <= 0.0)
        return 1.0;
    if (x == 0.0)
        return 1.0;

    double gln = lgamma(a);

    if (x < a + 1.0) {
        double ap = a;
        double sum = 1.0 / a;
        double term = sum;
        for (int n = 0; n < 200; n++) {
            ap += 1.0;
            term *= x / ap;
            sum += term;
            if (fabs(term) < fabs(sum) * 1e-10)
                break;
        }
        return 1.0 - sum * exp(-x + a * log(x) - gln);
    } else {
        double b = x + 1.0 - a;
        double c = 1.0 / DBL_MIN;
        double d = 1.0 / b;
        double h = d;
        for (int i = 1; i <= 200; i++) {
            int an = -i * (i - a);
            b += 2.0;
            d = an * d + b;
            if (fabs(d) < DBL_MIN) d = DBL_MIN;
            c = b + an / c;
            if (fabs(c) < DBL_MIN) c = DBL_MIN;
            d = 1.0 / d;
            double delta = d * c;
            h *= delta;
            if (fabs(delta - 1.0) < 1e-10)
                break;
        }
        return exp(-x + a * log(x) - gln) * h;
    }
}

int nist_bitstream_init(nist_bitstream_t *bs, const uint8_t *data, size_t len)
{
    if (!bs || !data || len == 0)
        return -1;

    bs->data = (uint8_t *)malloc(len);
    if (!bs->data)
        return -1;
    memcpy(bs->data, data, len);
    bs->byte_len = len;
    bs->bit_len = len * 8;

    bs->bits = (int *)malloc(bs->bit_len * sizeof(int));
    if (!bs->bits) {
        free(bs->data);
        return -1;
    }

    for (size_t i = 0; i < len; i++) {
        for (int j = 7; j >= 0; j--) {
            bs->bits[i * 8 + (7 - j)] = (data[i] >> j) & 1;
        }
    }

    return 0;
}

void nist_bitstream_free(nist_bitstream_t *bs)
{
    if (!bs)
        return;
    if (bs->data) free(bs->data);
    if (bs->bits) free(bs->bits);
    bs->data = NULL;
    bs->bits = NULL;
    bs->byte_len = 0;
    bs->bit_len = 0;
}

void nist_report_init(nist_test_report_t *report, size_t bit_count)
{
    if (!report)
        return;

    memset(report, 0, sizeof(*report));
    report->num_tests = NIST_NUM_TESTS;
    report->bit_count = bit_count;
    report->timestamp = time(NULL);

    const char *names[NIST_NUM_TESTS] = {
        "Frequency (Monobit)",
        "Frequency Within Block",
        "Cumulative Sums (Forward)",
        "Runs",
        "Longest Run of Ones",
        "Binary Matrix Rank",
        "Discrete Fourier Transform",
        "Non-Overlapping Template Matching",
        "Overlapping Template Matching",
        "Universal Statistical",
        "Linear Complexity",
        "Serial",
        "Approximate Entropy",
        "Cumulative Sums (Random)",
        "Random Excursions"
    };

    const char *descs[NIST_NUM_TESTS] = {
        "Test 1: Monobit frequency test",
        "Test 2: Block frequency test (M=20)",
        "Test 3: Cumulative sums test",
        "Test 4: Runs test",
        "Test 5: Longest run of ones in block",
        "Test 6: Binary matrix rank test",
        "Test 7: Discrete Fourier transform test",
        "Test 8: Non-overlapping template matching",
        "Test 9: Overlapping template matching",
        "Test 10: Maurer's universal statistical test",
        "Test 11: Linear complexity (M=500)",
        "Test 12: Serial test (m=2)",
        "Test 13: Approximate entropy (m=2)",
        "Test 14: Cumulative sums test (random)",
        "Test 15: Random excursions test"
    };

    for (int i = 0; i < NIST_NUM_TESTS; i++) {
        report->results[i].id          = (nist_test_id_t)i;
        report->results[i].name        = names[i];
        report->results[i].description = descs[i];
        report->results[i].p_value     = 0.0;
        report->results[i].passed      = 0;
        report->results[i].applicable  = 1;
    }
}

int nist_is_test_passable(double p_value)
{
    return (p_value >= NIST_P_VALUE_THRESHOLD) ? 1 : 0;
}

int nist_test_monobit(const int *bits, size_t n, double *p_value)
{
    if (!bits || !p_value || n < NIST_MIN_BIT_COUNT)
        return -1;

    int sum = 0;
    for (size_t i = 0; i < n; i++)
        sum += 2 * bits[i] - 1;

    double s_obs = fabs((double)sum) / sqrt((double)n);
    *p_value = erfc_custom(s_obs / sqrt(2.0));

    return 0;
}

int nist_test_block_freq(const int *bits, size_t n, int m, double *p_value)
{
    if (!bits || !p_value || n < NIST_MIN_BIT_COUNT || m <= 0)
        return -1;

    int N = (int)(n / m);
    if (N == 0)
        return -1;

    double chi2 = 0.0;
    for (int i = 0; i < N; i++) {
        int block_sum = 0;
        for (int j = 0; j < m; j++)
            block_sum += bits[i * m + j];
        double pi = (double)block_sum / m;
        chi2 += (pi - 0.5) * (pi - 0.5);
    }
    chi2 *= 4.0 * m;

    *p_value = igamc((double)N / 2.0, chi2 / 2.0);
    return 0;
}

int nist_test_cusum(const int *bits, size_t n, double *p_value)
{
    if (!bits || !p_value || n < NIST_MIN_BIT_COUNT)
        return -1;

    int *X = (int *)malloc(n * sizeof(int));
    if (!X) return -1;

    X[0] = 2 * bits[0] - 1;
    for (size_t i = 1; i < n; i++)
        X[i] = X[i - 1] + 2 * bits[i] - 1;

    int max_val = 0;
    for (size_t i = 0; i < n; i++) {
        if (abs(X[i]) > max_val)
            max_val = abs(X[i]);
    }

    int z = max_val;
    double sum1 = 0.0, sum2 = 0.0;
    double sqrt_n = sqrt((double)n);

    for (int k = (int)(-(double)n / (double)z + 1) / 4;
         k <= (int)((double)n / (double)z - 1) / 4; k++) {
        double arg1 = (4.0 * k + 1) * z / sqrt_n;
        double arg2 = (4.0 * k - 1) * z / sqrt_n;
        sum1 += erfc_custom(arg1) - erfc_custom(arg2);
    }

    for (int k = (int)(-(double)n / (double)z - 3) / 4;
         k <= (int)((double)n / (double)z - 1) / 4; k++) {
        double arg1 = (4.0 * k + 3) * z / sqrt_n;
        double arg2 = (4.0 * k + 1) * z / sqrt_n;
        sum2 += erfc_custom(arg1) - erfc_custom(arg2);
    }

    *p_value = 1.0 - sum1 + sum2;

    free(X);
    return 0;
}

int nist_test_runs(const int *bits, size_t n, double *p_value)
{
    if (!bits || !p_value || n < NIST_MIN_BIT_COUNT)
        return -1;

    double pi = 0.0;
    for (size_t i = 0; i < n; i++)
        pi += bits[i];
    pi /= n;

    double tau = 2.0 / sqrt((double)n);
    if (fabs(pi - 0.5) >= tau) {
        *p_value = 0.0;
        return 0;
    }

    size_t v_obs = 0;
    for (size_t i = 0; i < n - 1; i++) {
        if (bits[i] != bits[i + 1])
            v_obs++;
    }
    v_obs++;

    double denom = pi * (1.0 - pi);
    double arg = fabs((double)v_obs - 2.0 * n * pi * (1.0 - pi))
                 / (2.0 * sqrt(2.0 * (double)n) * denom);

    *p_value = erfc_custom(arg);
    return 0;
}

int nist_test_longest_run(const int *bits, size_t n, double *p_value)
{
    if (!bits || !p_value || n < NIST_MIN_BIT_COUNT)
        return -1;

    int M;
    double pi[7];
    int K;

    if (n >= 1000000) {
        M = 10000;
        K = 6;
        pi[0] = 0.0882; pi[1] = 0.2092; pi[2] = 0.2483;
        pi[3] = 0.1933; pi[4] = 0.1208; pi[5] = 0.0675;
        pi[6] = 0.0727;
    } else if (n >= 12800) {
        M = 128;
        K = 5;
        pi[0] = 0.1174; pi[1] = 0.2430; pi[2] = 0.2493;
        pi[3] = 0.1752; pi[4] = 0.1027; pi[5] = 0.1124;
    } else if (n >= 6272) {
        M = 128;
        K = 5;
        pi[0] = 0.1174; pi[1] = 0.2430; pi[2] = 0.2493;
        pi[3] = 0.1752; pi[4] = 0.1027; pi[5] = 0.1124;
    } else {
        M = 8;
        K = 3;
        pi[0] = 0.2148; pi[1] = 0.3672; pi[2] = 0.2305;
        pi[3] = 0.1875;
    }

    int N = (int)(n / M);
    if (N == 0)
        return -1;

    int *v = (int *)calloc(K + 1, sizeof(int));
    if (!v) return -1;

    for (int i = 0; i < N; i++) {
        int longest = 0;
        int current = 0;
        for (int j = 0; j < M; j++) {
            if (bits[i * M + j] == 1) {
                current++;
                if (current > longest)
                    longest = current;
            } else {
                current = 0;
            }
        }

        int idx;
        if (M == 10000) {
            if (longest <= 10) idx = 0;
            else if (longest == 11) idx = 1;
            else if (longest == 12) idx = 2;
            else if (longest == 13) idx = 3;
            else if (longest == 14) idx = 4;
            else if (longest == 15) idx = 5;
            else idx = 6;
        } else if (M == 128) {
            if (longest <= 4) idx = 0;
            else if (longest == 5) idx = 1;
            else if (longest == 6) idx = 2;
            else if (longest == 7) idx = 3;
            else if (longest == 8) idx = 4;
            else idx = 5;
        } else {
            if (longest <= 1) idx = 0;
            else if (longest == 2) idx = 1;
            else if (longest == 3) idx = 2;
            else idx = 3;
        }

        if (idx <= K)
            v[idx]++;
    }

    double chi2 = 0.0;
    for (int i = 0; i <= K; i++) {
        double expected = N * pi[i];
        chi2 += (v[i] - expected) * (v[i] - expected) / expected;
    }

    *p_value = igamc((double)K / 2.0, chi2 / 2.0);
    free(v);
    return 0;
}

int nist_test_rank(const uint8_t *data, size_t len, double *p_value)
{
    if (!data || !p_value || len < 38 * 38 / 8)
        return -1;

    const int M = 32;
    const int Q = 32;
    int N = (int)(len * 8 / (M * Q));
    if (N < 1)
        return -1;

    int F_M = 0, F_M_1 = 0, F_rem = 0;

    for (int block = 0; block < N; block++) {
        int matrix[32][32] = {0};
        for (int row = 0; row < M; row++) {
            for (int col = 0; col < Q; col++) {
                size_t bit_idx = block * M * Q + row * Q + col;
                size_t byte_idx = bit_idx / 8;
                int bit_offset = 7 - (bit_idx % 8);
                matrix[row][col] = (data[byte_idx] >> bit_offset) & 1;
            }
        }

        int rank = M;
        for (int i = 0; i < M && i < Q; i++) {
            int pivot_row = -1;
            for (int r = i; r < M; r++) {
                if (matrix[r][i] == 1) {
                    pivot_row = r;
                    break;
                }
            }
            if (pivot_row == -1) {
                rank = i;
                break;
            }
            if (pivot_row != i) {
                for (int c = 0; c < Q; c++) {
                    int tmp = matrix[i][c];
                    matrix[i][c] = matrix[pivot_row][c];
                    matrix[pivot_row][c] = tmp;
                }
            }
            for (int r = 0; r < M; r++) {
                if (r != i && matrix[r][i] == 1) {
                    for (int c = 0; c < Q; c++)
                        matrix[r][c] ^= matrix[i][c];
                }
            }
        }

        if (rank == M)
            F_M++;
        else if (rank == M - 1)
            F_M_1++;
        else
            F_rem++;
    }

    double chi2 = (F_M - 0.2888 * N) * (F_M - 0.2888 * N) / (0.2888 * N)
                + (F_M_1 - 0.5776 * N) * (F_M_1 - 0.5776 * N) / (0.5776 * N)
                + (F_rem - 0.1336 * N) * (F_rem - 0.1336 * N) / (0.1336 * N);

    *p_value = exp(-chi2 / 2.0);
    return 0;
}

int nist_test_fft(const int *bits, size_t n, double *p_value)
{
    if (!bits || !p_value || n < NIST_MIN_BIT_COUNT)
        return -1;

    double *X = (double *)malloc(n * sizeof(double));
    if (!X) return -1;

    for (size_t i = 0; i < n; i++)
        X[i] = 2.0 * bits[i] - 1.0;

    double *real = (double *)malloc(n * sizeof(double));
    double *imag = (double *)malloc(n * sizeof(double));
    if (!real || !imag) {
        free(X);
        if (real) free(real);
        if (imag) free(imag);
        return -1;
    }

    for (size_t k = 0; k < n / 2 + 1; k++) {
        real[k] = 0.0;
        imag[k] = 0.0;
        for (size_t t = 0; t < n; t++) {
            double angle = -2.0 * M_PI * k * t / n;
            real[k] += X[t] * cos(angle);
            imag[k] += X[t] * sin(angle);
        }
    }

    int N_0 = (int)(0.95 * n / 2.0);
    int N_1 = 0;
    double T = sqrt(log(1.0 / 0.05) * n);

    for (size_t k = 0; k < n / 2; k++) {
        double modulus = sqrt(real[k] * real[k] + imag[k] * imag[k]);
        if (modulus < T)
            N_1++;
    }

    double d = (N_1 - N_0) / sqrt(n * 0.95 * 0.05 / 4.0);
    *p_value = erfc_custom(fabs(d) / sqrt(2.0));

    free(X);
    free(real);
    free(imag);
    return 0;
}

int nist_test_nonoverlapping(const int *bits, size_t n, double *p_value)
{
    if (!bits || !p_value || n < NIST_MIN_BIT_COUNT)
        return -1;

    int m = NIST_TEMPLATE_LEN;
    int template_pattern[9] = {0, 0, 0, 0, 0, 0, 0, 0, 1};

    int M = 1032;
    int N = (int)(n / M);
    if (N == 0)
        return -1;

    double mu = (double)(M - m + 1) / pow(2.0, (double)m);
    double sigma2 = M * (1.0 / pow(2.0, (double)m)
                 - (2.0 * m - 1.0) / pow(2.0, 2.0 * (double)m));

    double chi2 = 0.0;
    for (int i = 0; i < N; i++) {
        int W_obs = 0;
        for (int j = 0; j <= M - m; j++) {
            int match = 1;
            for (int k = 0; k < m; k++) {
                if (bits[i * M + j + k] != template_pattern[k]) {
                    match = 0;
                    break;
                }
            }
            if (match)
                W_obs++;
        }
        chi2 += (W_obs - mu) * (W_obs - mu) / sigma2;
    }

    *p_value = igamc((double)N / 2.0, chi2 / 2.0);
    return 0;
}

int nist_test_overlapping(const int *bits, size_t n, double *p_value)
{
    if (!bits || !p_value || n < NIST_MIN_BIT_COUNT)
        return -1;

    int m = 9;
    int M = 1062;
    int N = (int)(n / M);
    if (N == 0)
        return -1;

    double *pi_table = (double *)malloc(6 * sizeof(double));
    if (!pi_table) return -1;

    pi_table[0] = 0.364091;
    pi_table[1] = 0.185659;
    pi_table[2] = 0.139381;
    pi_table[3] = 0.100571;
    pi_table[4] = 0.0704323;
    pi_table[5] = 0.139865;

    int *v = (int *)calloc(6, sizeof(int));
    if (!v) {
        free(pi_table);
        return -1;
    }

    for (int i = 0; i < N; i++) {
        int v_obs = 0;
        for (int j = 0; j <= M - m; j++) {
            int match = 1;
            for (int k = 0; k < m; k++) {
                if (bits[i * M + j + k] != 1) {
                    match = 0;
                    break;
                }
            }
            if (match)
                v_obs++;
        }

        if (v_obs <= 4)
            v[v_obs]++;
        else
            v[5]++;
    }

    double chi2 = 0.0;
    for (int i = 0; i < 6; i++) {
        double expected = N * pi_table[i];
        chi2 += (v[i] - expected) * (v[i] - expected) / expected;
    }

    *p_value = igamc(5.0 / 2.0, chi2 / 2.0);
    free(pi_table);
    free(v);
    return 0;
}

int nist_test_universal(const int *bits, size_t n, double *p_value)
{
    if (!bits || !p_value || n < 387840)
        return -1;

    int L = 7;
    int Q = 1280;
    int K = (int)(n / L) - Q;

    if (K <= 0)
        return -1;

    int *T = (int *)calloc(1 << L, sizeof(int));
    if (!T) return -1;

    for (int i = 0; i < Q; i++) {
        int idx = 0;
        for (int j = 0; j < L; j++)
            idx = (idx << 1) | bits[i * L + j];
        T[idx] = i + 1;
    }

    double sum = 0.0;
    for (int i = Q; i < Q + K; i++) {
        int idx = 0;
        for (int j = 0; j < L; j++)
            idx = (idx << 1) | bits[i * L + j];
        sum += log((double)(i + 1 - T[idx])) / log(2.0);
        T[idx] = i + 1;
    }

    double fn = sum / K;

    double expected_value[17] = {0, 0, 0, 0, 0, 0,
        5.2177052, 6.1962507, 7.1836656, 8.1764248,
        9.1723243, 10.170032, 11.168765, 12.168070,
        13.167693, 14.167488, 15.167379};

    double variance[17] = {0, 0, 0, 0, 0, 0,
        2.954, 3.125, 3.238, 3.311,
        3.356, 3.384, 3.401, 3.410,
        3.416, 3.419, 3.421};

    double c = 0.7 - 0.8 / (double)L + (4.0 + 32.0 / (double)L)
               * pow((double)K, -3.0 / (double)L) / 15.0;
    double sigma = c * sqrt(variance[L] / (double)K);

    *p_value = erfc_custom(fabs(fn - expected_value[L]) / (sqrt(2.0) * sigma));
    free(T);
    return 0;
}

int nist_test_linear_complexity(const int *bits, size_t n, int m, double *p_value)
{
    if (!bits || !p_value || n < NIST_MIN_BIT_COUNT || m <= 0)
        return -1;

    int M = 500;
    int N = (int)(n / M);
    if (N == 0)
        return -1;

    double mu = (double)M / 2.0
                + (9.0 + pow(-1.0, (double)(M + 1))) / 36.0
                - (double)(M / 3 + 2.0 / 9.0) / pow(2.0, (double)M);

    double *pi_table = (double *)malloc(7 * sizeof(double));
    if (!pi_table) return -1;

    pi_table[0] = 0.01047;
    pi_table[1] = 0.03125;
    pi_table[2] = 0.125;
    pi_table[3] = 0.5;
    pi_table[4] = 0.25;
    pi_table[5] = 0.0625;
    pi_table[6] = 0.020833;

    int *v = (int *)calloc(7, sizeof(int));
    if (!v) {
        free(pi_table);
        return -1;
    }

    for (int i = 0; i < N; i++) {
        int *s = (int *)malloc(M * sizeof(int));
        if (!s) {
            free(pi_table);
            free(v);
            return -1;
        }

        for (int j = 0; j < M; j++)
            s[j] = bits[i * M + j];

        int *C = (int *)calloc(M, sizeof(int));
        int *B = (int *)calloc(M, sizeof(int));
        int *T = (int *)calloc(M, sizeof(int));
        if (!C || !B || !T) {
            free(s); free(C); free(B); free(T);
            free(pi_table); free(v);
            return -1;
        }

        C[0] = 1;
        B[0] = 1;
        int L = 0;
        int m_bm = -1;
        int d;

        for (int j = 0; j < M; j++) {
            d = s[j];
            for (int k = 1; k <= L; k++)
                d ^= C[k] * s[j - k];

            if (d != 0) {
                for (int k = 0; k <= j; k++) {
                    T[k] = C[k];
                    C[k + j - m_bm] ^= B[k];
                }
                if (L <= j / 2) {
                    L = j + 1 - L;
                    m_bm = j;
                    for (int k = 0; k <= L; k++)
                        B[k] = T[k];
                }
            }
        }

        double T_val = pow(-1.0, (double)M) * (L - mu) + 2.0 / 9.0;
        int bin_idx;
        if (T_val <= -2.5) bin_idx = 0;
        else if (T_val <= -1.5) bin_idx = 1;
        else if (T_val <= -0.5) bin_idx = 2;
        else if (T_val <= 0.5) bin_idx = 3;
        else if (T_val <= 1.5) bin_idx = 4;
        else if (T_val <= 2.5) bin_idx = 5;
        else bin_idx = 6;

        v[bin_idx]++;

        free(s); free(C); free(B); free(T);
    }

    double chi2 = 0.0;
    for (int i = 0; i < 7; i++) {
        double expected = N * pi_table[i];
        chi2 += (v[i] - expected) * (v[i] - expected) / expected;
    }

    *p_value = igamc(3.0, chi2 / 2.0);
    free(pi_table);
    free(v);
    return 0;
}

int nist_test_serial(const int *bits, size_t n, int m, double *p_value)
{
    if (!bits || !p_value || n < NIST_MIN_BIT_COUNT || m <= 0)
        return -1;

    int p = m;
    size_t *P_m = (size_t *)calloc(1 << p, sizeof(size_t));
    size_t *P_m_1 = (size_t *)calloc(1 << (p + 1), sizeof(size_t));
    size_t *P_m_2 = (size_t *)calloc(1 << (p + 2), sizeof(size_t));

    if (!P_m || !P_m_1 || !P_m_2) {
        if (P_m) free(P_m);
        if (P_m_1) free(P_m_1);
        if (P_m_2) free(P_m_2);
        return -1;
    }

    for (size_t i = 0; i < n; i++) {
        int idx_m = 0, idx_m1 = 0, idx_m2 = 0;
        for (int j = 0; j < p; j++) {
            size_t pos = (i + j) % n;
            idx_m  = (idx_m << 1)  | bits[pos];
            idx_m1 = (idx_m1 << 1) | bits[pos];
            idx_m2 = (idx_m2 << 1) | bits[pos];
        }
        P_m[idx_m]++;

        size_t next_pos = (i + p) % n;
        idx_m1 = (idx_m1 << 1) | bits[next_pos];
        P_m_1[idx_m1]++;

        next_pos = (i + p + 1) % n;
        idx_m2 = (idx_m2 << 1) | bits[next_pos];
        P_m_2[idx_m2]++;
    }

    double psi2_m = 0.0, psi2_m_1 = 0.0, psi2_m_2 = 0.0;

    for (int i = 0; i < (1 << p); i++)
        psi2_m += (double)P_m[i] * P_m[i];
    psi2_m = (double)(1 << p) / n * psi2_m - n;

    for (int i = 0; i < (1 << (p + 1)); i++)
        psi2_m_1 += (double)P_m_1[i] * P_m_1[i];
    psi2_m_1 = (double)(1 << (p + 1)) / n * psi2_m_1 - n;

    for (int i = 0; i < (1 << (p + 2)); i++)
        psi2_m_2 += (double)P_m_2[i] * P_m_2[i];
    psi2_m_2 = (double)(1 << (p + 2)) / n * psi2_m_2 - n;

    double del1 = psi2_m - psi2_m_1;
    double del2 = psi2_m - 2.0 * psi2_m_1 + psi2_m_2;

    double p1 = igamc(pow(2.0, p - 1) / 2.0, del1 / 2.0);
    double p2 = igamc(pow(2.0, p - 2) / 2.0, del2 / 2.0);

    *p_value = (p1 < p2) ? p1 : p2;

    free(P_m);
    free(P_m_1);
    free(P_m_2);
    return 0;
}

int nist_test_approx_entropy(const int *bits, size_t n, int m, double *p_value)
{
    if (!bits || !p_value || n < NIST_MIN_BIT_COUNT || m <= 0)
        return -1;

    double phi_m = 0.0, phi_m_1 = 0.0;

    for (int k = 0; k < 2; k++) {
        int p = m + k;
        size_t *C = (size_t *)calloc(1 << p, sizeof(size_t));
        if (!C) return -1;

        for (size_t i = 0; i < n; i++) {
            int idx = 0;
            for (int j = 0; j < p; j++)
                idx = (idx << 1) | bits[(i + j) % n];
            C[idx]++;
        }

        double sum = 0.0;
        for (int i = 0; i < (1 << p); i++) {
            if (C[i] > 0) {
                double Ci = (double)C[i] / n;
                sum += Ci * log(Ci);
            }
        }

        if (k == 0)
            phi_m = sum;
        else
            phi_m_1 = sum;

        free(C);
    }

    double apen = phi_m - phi_m_1;
    double chi2 = 2.0 * n * (log(2.0) - apen);

    *p_value = igamc(pow(2.0, m - 1), chi2 / 2.0);
    return 0;
}

int nist_test_cumulative_sums(const int *bits, size_t n, double *p_value)
{
    if (!bits || !p_value || n < NIST_MIN_BIT_COUNT)
        return -1;

    int *X = (int *)malloc(n * sizeof(int));
    if (!X) return -1;

    X[0] = 2 * bits[0] - 1;
    for (size_t i = 1; i < n; i++)
        X[i] = X[i - 1] + 2 * bits[i] - 1;

    int max_val = 0;
    for (size_t i = 0; i < n; i++) {
        if (abs(X[i]) > max_val)
            max_val = abs(X[i]);
    }

    int z = max_val;
    double sum1 = 0.0, sum2 = 0.0;
    double sqrt_n = sqrt((double)n);

    for (int k = (int)(-n / z + 1) / 4; k <= (int)(n / z - 1) / 4; k++) {
        sum1 += erfc_custom(((4 * k + 1) * z) / sqrt_n)
               - erfc_custom(((4 * k - 1) * z) / sqrt_n);
    }

    for (int k = (int)(-n / z - 3) / 4; k <= (int)(n / z - 1) / 4; k++) {
        sum2 += erfc_custom(((4 * k + 3) * z) / sqrt_n)
               - erfc_custom(((4 * k + 1) * z) / sqrt_n);
    }

    *p_value = 1.0 - sum1 + sum2;

    free(X);
    return 0;
}

int nist_test_random_excursions(const int *bits, size_t n, double *p_value)
{
    if (!bits || !p_value || n < NIST_MIN_BIT_COUNT)
        return -1;

    int *S = (int *)malloc((n + 1) * sizeof(int));
    if (!S) return -1;

    S[0] = 0;
    for (size_t i = 1; i <= n; i++)
        S[i] = S[i - 1] + 2 * bits[i - 1] - 1;

    int J = 0;
    for (size_t i = 1; i <= n; i++) {
        if (S[i] == 0)
            J++;
    }

    if (J < 500) {
        *p_value = 0.0;
        free(S);
        return 0;
    }

    int *cycle = (int *)malloc(n * sizeof(int));
    if (!cycle) {
        free(S);
        return -1;
    }

    int cycle_count = 0;
    int pos = 0;
    for (size_t i = 1; i <= n; i++) {
        if (S[i] == 0) {
            for (int j = pos; j < (int)i; j++)
                cycle[cycle_count++] = S[j + 1];
            pos = i;
        }
    }

    double min_p = 1.0;
    int x_vals[8] = {-4, -3, -2, -1, 1, 2, 3, 4};

    for (int xi = 0; xi < 8; xi++) {
        int x = x_vals[xi];
        int *v = (int *)calloc(6, sizeof(int));
        if (!v) {
            free(cycle); free(S);
            return -1;
        }

        int idx = 0;
        for (int i = 0; i < cycle_count; i++) {
            int count = 0;
            if (cycle[i] == x)
                count = 1;
            if (count <= 5)
                v[count]++;
            else
                v[5]++;
            idx++;
        }

        double pi_table[6] = {0.5, 0.25, 0.125, 0.0625, 0.03125, 0.03125};

        double chi2 = 0.0;
        for (int i = 0; i < 6; i++) {
            double expected = (double)J * pi_table[i];
            chi2 += (v[i] - expected) * (v[i] - expected) / expected;
        }

        double p_val = igamc(5.0 / 2.0, chi2 / 2.0);
        if (p_val < min_p)
            min_p = p_val;

        free(v);
    }

    *p_value = min_p;

    free(cycle);
    free(S);
    return 0;
}

int nist_run_all_tests(const uint8_t *data, size_t len, nist_test_report_t *report)
{
    if (!data || !report || len == 0)
        return -1;

    nist_bitstream_t bs;
    if (nist_bitstream_init(&bs, data, len) < 0) {
        fprintf(stderr, "nist_run_all_tests: bitstream init failed\n");
        return -1;
    }

    nist_report_init(report, bs.bit_len);

    int m_freq = NIST_BLOCK_FREQ_M;
    int m_serial = 2;
    int m_approx = 2;
    int m_lc = 500;

    int ret;
    double p;

    ret = nist_test_monobit(bs.bits, bs.bit_len, &p);
    report->results[NIST_TEST_MONOBIT].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_MONOBIT].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_MONOBIT].applicable = (ret == 0) ? 1 : 0;

    ret = nist_test_block_freq(bs.bits, bs.bit_len, m_freq, &p);
    report->results[NIST_TEST_BLOCK_FREQ].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_BLOCK_FREQ].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_BLOCK_FREQ].applicable = (ret == 0) ? 1 : 0;

    ret = nist_test_cusum(bs.bits, bs.bit_len, &p);
    report->results[NIST_TEST_CUSUM].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_CUSUM].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_CUSUM].applicable = (ret == 0) ? 1 : 0;

    ret = nist_test_runs(bs.bits, bs.bit_len, &p);
    report->results[NIST_TEST_RUNS].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_RUNS].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_RUNS].applicable = (ret == 0) ? 1 : 0;

    ret = nist_test_longest_run(bs.bits, bs.bit_len, &p);
    report->results[NIST_TEST_LONGEST_RUN].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_LONGEST_RUN].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_LONGEST_RUN].applicable = (ret == 0) ? 1 : 0;

    ret = nist_test_rank(data, len, &p);
    report->results[NIST_TEST_RANK].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_RANK].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_RANK].applicable = (ret == 0) ? 1 : 0;

    ret = nist_test_fft(bs.bits, bs.bit_len, &p);
    report->results[NIST_TEST_FFT].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_FFT].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_FFT].applicable = (ret == 0) ? 1 : 0;

    ret = nist_test_nonoverlapping(bs.bits, bs.bit_len, &p);
    report->results[NIST_TEST_NONOVERLAPPING].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_NONOVERLAPPING].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_NONOVERLAPPING].applicable = (ret == 0) ? 1 : 0;

    ret = nist_test_overlapping(bs.bits, bs.bit_len, &p);
    report->results[NIST_TEST_OVERLAPPING].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_OVERLAPPING].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_OVERLAPPING].applicable = (ret == 0) ? 1 : 0;

    ret = nist_test_universal(bs.bits, bs.bit_len, &p);
    report->results[NIST_TEST_UNIVERSAL].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_UNIVERSAL].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_UNIVERSAL].applicable = (ret == 0) ? 1 : 0;

    ret = nist_test_linear_complexity(bs.bits, bs.bit_len, m_lc, &p);
    report->results[NIST_TEST_LINEAR_COMPLEXITY].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_LINEAR_COMPLEXITY].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_LINEAR_COMPLEXITY].applicable = (ret == 0) ? 1 : 0;

    ret = nist_test_serial(bs.bits, bs.bit_len, m_serial, &p);
    report->results[NIST_TEST_SERIAL].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_SERIAL].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_SERIAL].applicable = (ret == 0) ? 1 : 0;

    ret = nist_test_approx_entropy(bs.bits, bs.bit_len, m_approx, &p);
    report->results[NIST_TEST_APPROX_ENTROPY].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_APPROX_ENTROPY].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_APPROX_ENTROPY].applicable = (ret == 0) ? 1 : 0;

    ret = nist_test_cumulative_sums(bs.bits, bs.bit_len, &p);
    report->results[NIST_TEST_CUMULATIVE_SUMS].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_CUMULATIVE_SUMS].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_CUMULATIVE_SUMS].applicable = (ret == 0) ? 1 : 0;

    ret = nist_test_random_excursions(bs.bits, bs.bit_len, &p);
    report->results[NIST_TEST_RANDOM_EXCURSIONS].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_RANDOM_EXCURSIONS].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_RANDOM_EXCURSIONS].applicable = (ret == 0) ? 1 : 0;

    report->num_passed = 0;
    double p_sum = 0.0;
    for (int i = 0; i < NIST_NUM_TESTS; i++) {
        if (report->results[i].passed)
            report->num_passed++;
        p_sum += report->results[i].p_value;
    }
    report->overall_score = p_sum / NIST_NUM_TESTS;

    nist_bitstream_free(&bs);
    return 0;
}

size_t nist_test_min_bytes(nist_test_id_t test_id)
{
    switch (test_id) {
    case NIST_TEST_MONOBIT:
        return 128;
    case NIST_TEST_BLOCK_FREQ:
        return 100;
    case NIST_TEST_CUSUM:
        return 100;
    case NIST_TEST_RUNS:
        return 100;
    case NIST_TEST_LONGEST_RUN:
        return 128;
    case NIST_TEST_RANK:
        return 152;
    case NIST_TEST_FFT:
        return 1024;
    case NIST_TEST_NONOVERLAPPING:
        return 1048576;
    case NIST_TEST_OVERLAPPING:
        return 1048576;
    case NIST_TEST_UNIVERSAL:
        return 387840;
    case NIST_TEST_LINEAR_COMPLEXITY:
        return 1000000;
    case NIST_TEST_SERIAL:
        return 128;
    case NIST_TEST_APPROX_ENTROPY:
        return 128;
    case NIST_TEST_CUMULATIVE_SUMS:
        return 100;
    case NIST_TEST_RANDOM_EXCURSIONS:
        return 1000000;
    default:
        return 1000000;
    }
}

int nist_test_is_quick(nist_test_id_t test_id)
{
    switch (test_id) {
    case NIST_TEST_MONOBIT:
    case NIST_TEST_BLOCK_FREQ:
    case NIST_TEST_RUNS:
    case NIST_TEST_LONGEST_RUN:
    case NIST_TEST_CUSUM:
        return 1;
    default:
        return 0;
    }
}

size_t nist_test_estimate_time_ms(nist_test_id_t test_id, size_t byte_len)
{
    double bytes_per_ms;
    switch (test_id) {
    case NIST_TEST_MONOBIT:
    case NIST_TEST_BLOCK_FREQ:
    case NIST_TEST_CUSUM:
    case NIST_TEST_RUNS:
    case NIST_TEST_CUMULATIVE_SUMS:
        bytes_per_ms = 1000000.0;
        break;
    case NIST_TEST_LONGEST_RUN:
    case NIST_TEST_SERIAL:
    case NIST_TEST_APPROX_ENTROPY:
        bytes_per_ms = 500000.0;
        break;
    case NIST_TEST_RANK:
        bytes_per_ms = 100000.0;
        break;
    case NIST_TEST_FFT:
        bytes_per_ms = 10000.0;
        break;
    case NIST_TEST_NONOVERLAPPING:
    case NIST_TEST_OVERLAPPING:
        bytes_per_ms = 5000.0;
        break;
    case NIST_TEST_UNIVERSAL:
        bytes_per_ms = 50000.0;
        break;
    case NIST_TEST_LINEAR_COMPLEXITY:
        bytes_per_ms = 20000.0;
        break;
    case NIST_TEST_RANDOM_EXCURSIONS:
        bytes_per_ms = 30000.0;
        break;
    default:
        bytes_per_ms = 10000.0;
        break;
    }
    return (size_t)((double)byte_len / bytes_per_ms) + 1;
}

int nist_run_quick_tests(const uint8_t *data, size_t len, nist_test_report_t *report)
{
    nist_bitstream_t bs;
    if (nist_bitstream_init(&bs, data, len) < 0) {
        fprintf(stderr, "nist_run_quick_tests: bitstream init failed\n");
        return -1;
    }

    nist_report_init(report, bs.bit_len);

    int m_freq = NIST_BLOCK_FREQ_M;
    int ret;
    double p;
    int count = 0;

    for (int i = 0; i < NIST_NUM_TESTS; i++) {
        report->results[i].applicable = 0;
    }

    ret = nist_test_monobit(bs.bits, bs.bit_len, &p);
    report->results[NIST_TEST_MONOBIT].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_MONOBIT].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_MONOBIT].applicable = (ret == 0) ? 1 : 0;
    if (ret == 0) count++;

    ret = nist_test_block_freq(bs.bits, bs.bit_len, m_freq, &p);
    report->results[NIST_TEST_BLOCK_FREQ].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_BLOCK_FREQ].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_BLOCK_FREQ].applicable = (ret == 0) ? 1 : 0;
    if (ret == 0) count++;

    ret = nist_test_cusum(bs.bits, bs.bit_len, &p);
    report->results[NIST_TEST_CUSUM].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_CUSUM].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_CUSUM].applicable = (ret == 0) ? 1 : 0;
    if (ret == 0) count++;

    ret = nist_test_runs(bs.bits, bs.bit_len, &p);
    report->results[NIST_TEST_RUNS].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_RUNS].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_RUNS].applicable = (ret == 0) ? 1 : 0;
    if (ret == 0) count++;

    ret = nist_test_longest_run(bs.bits, bs.bit_len, &p);
    report->results[NIST_TEST_LONGEST_RUN].p_value = (ret == 0) ? p : 0.0;
    report->results[NIST_TEST_LONGEST_RUN].passed  = nist_is_test_passable(p);
    report->results[NIST_TEST_LONGEST_RUN].applicable = (ret == 0) ? 1 : 0;
    if (ret == 0) count++;

    report->num_tests = count;
    report->num_passed = 0;
    double p_sum = 0.0;
    for (int i = 0; i < NIST_NUM_TESTS; i++) {
        if (report->results[i].applicable) {
            if (report->results[i].passed)
                report->num_passed++;
            p_sum += report->results[i].p_value;
        }
    }
    report->overall_score = (count > 0) ? p_sum / count : 0.0;

    nist_bitstream_free(&bs);
    return 0;
}

int nist_run_adaptive_tests(const uint8_t *data, size_t len, nist_test_report_t *report,
                            int quick_mode)
{
    if (quick_mode) {
        return nist_run_quick_tests(data, len, report);
    }

    nist_bitstream_t bs;
    if (nist_bitstream_init(&bs, data, len) < 0) {
        fprintf(stderr, "nist_run_adaptive_tests: bitstream init failed\n");
        return -1;
    }

    nist_report_init(report, bs.bit_len);

    int m_freq = NIST_BLOCK_FREQ_M;
    int m_serial = 2;
    int m_approx = 2;
    int m_lc = 500;

    int ret;
    double p;

    for (int i = 0; i < NIST_NUM_TESTS; i++) {
        size_t min_bytes = nist_test_min_bytes((nist_test_id_t)i);
        if (len < min_bytes) {
            report->results[i].applicable = 0;
            continue;
        }
        report->results[i].applicable = 1;
    }

    if (report->results[NIST_TEST_MONOBIT].applicable) {
        ret = nist_test_monobit(bs.bits, bs.bit_len, &p);
        report->results[NIST_TEST_MONOBIT].p_value = (ret == 0) ? p : 0.0;
        report->results[NIST_TEST_MONOBIT].passed  = nist_is_test_passable(p);
    }

    if (report->results[NIST_TEST_BLOCK_FREQ].applicable) {
        ret = nist_test_block_freq(bs.bits, bs.bit_len, m_freq, &p);
        report->results[NIST_TEST_BLOCK_FREQ].p_value = (ret == 0) ? p : 0.0;
        report->results[NIST_TEST_BLOCK_FREQ].passed  = nist_is_test_passable(p);
    }

    if (report->results[NIST_TEST_CUSUM].applicable) {
        ret = nist_test_cusum(bs.bits, bs.bit_len, &p);
        report->results[NIST_TEST_CUSUM].p_value = (ret == 0) ? p : 0.0;
        report->results[NIST_TEST_CUSUM].passed  = nist_is_test_passable(p);
    }

    if (report->results[NIST_TEST_RUNS].applicable) {
        ret = nist_test_runs(bs.bits, bs.bit_len, &p);
        report->results[NIST_TEST_RUNS].p_value = (ret == 0) ? p : 0.0;
        report->results[NIST_TEST_RUNS].passed  = nist_is_test_passable(p);
    }

    if (report->results[NIST_TEST_LONGEST_RUN].applicable) {
        ret = nist_test_longest_run(bs.bits, bs.bit_len, &p);
        report->results[NIST_TEST_LONGEST_RUN].p_value = (ret == 0) ? p : 0.0;
        report->results[NIST_TEST_LONGEST_RUN].passed  = nist_is_test_passable(p);
    }

    if (report->results[NIST_TEST_RANK].applicable) {
        ret = nist_test_rank(data, len, &p);
        report->results[NIST_TEST_RANK].p_value = (ret == 0) ? p : 0.0;
        report->results[NIST_TEST_RANK].passed  = nist_is_test_passable(p);
    }

    if (report->results[NIST_TEST_FFT].applicable) {
        ret = nist_test_fft(bs.bits, bs.bit_len, &p);
        report->results[NIST_TEST_FFT].p_value = (ret == 0) ? p : 0.0;
        report->results[NIST_TEST_FFT].passed  = nist_is_test_passable(p);
    }

    if (report->results[NIST_TEST_NONOVERLAPPING].applicable) {
        ret = nist_test_nonoverlapping(bs.bits, bs.bit_len, &p);
        report->results[NIST_TEST_NONOVERLAPPING].p_value = (ret == 0) ? p : 0.0;
        report->results[NIST_TEST_NONOVERLAPPING].passed  = nist_is_test_passable(p);
    }

    if (report->results[NIST_TEST_OVERLAPPING].applicable) {
        ret = nist_test_overlapping(bs.bits, bs.bit_len, &p);
        report->results[NIST_TEST_OVERLAPPING].p_value = (ret == 0) ? p : 0.0;
        report->results[NIST_TEST_OVERLAPPING].passed  = nist_is_test_passable(p);
    }

    if (report->results[NIST_TEST_UNIVERSAL].applicable) {
        ret = nist_test_universal(bs.bits, bs.bit_len, &p);
        report->results[NIST_TEST_UNIVERSAL].p_value = (ret == 0) ? p : 0.0;
        report->results[NIST_TEST_UNIVERSAL].passed  = nist_is_test_passable(p);
    }

    if (report->results[NIST_TEST_LINEAR_COMPLEXITY].applicable) {
        ret = nist_test_linear_complexity(bs.bits, bs.bit_len, m_lc, &p);
        report->results[NIST_TEST_LINEAR_COMPLEXITY].p_value = (ret == 0) ? p : 0.0;
        report->results[NIST_TEST_LINEAR_COMPLEXITY].passed  = nist_is_test_passable(p);
    }

    if (report->results[NIST_TEST_SERIAL].applicable) {
        ret = nist_test_serial(bs.bits, bs.bit_len, m_serial, &p);
        report->results[NIST_TEST_SERIAL].p_value = (ret == 0) ? p : 0.0;
        report->results[NIST_TEST_SERIAL].passed  = nist_is_test_passable(p);
    }

    if (report->results[NIST_TEST_APPROX_ENTROPY].applicable) {
        ret = nist_test_approx_entropy(bs.bits, bs.bit_len, m_approx, &p);
        report->results[NIST_TEST_APPROX_ENTROPY].p_value = (ret == 0) ? p : 0.0;
        report->results[NIST_TEST_APPROX_ENTROPY].passed  = nist_is_test_passable(p);
    }

    if (report->results[NIST_TEST_CUMULATIVE_SUMS].applicable) {
        ret = nist_test_cumulative_sums(bs.bits, bs.bit_len, &p);
        report->results[NIST_TEST_CUMULATIVE_SUMS].p_value = (ret == 0) ? p : 0.0;
        report->results[NIST_TEST_CUMULATIVE_SUMS].passed  = nist_is_test_passable(p);
    }

    if (report->results[NIST_TEST_RANDOM_EXCURSIONS].applicable) {
        ret = nist_test_random_excursions(bs.bits, bs.bit_len, &p);
        report->results[NIST_TEST_RANDOM_EXCURSIONS].p_value = (ret == 0) ? p : 0.0;
        report->results[NIST_TEST_RANDOM_EXCURSIONS].passed  = nist_is_test_passable(p);
    }

    report->num_tests = 0;
    report->num_passed = 0;
    double p_sum = 0.0;
    for (int i = 0; i < NIST_NUM_TESTS; i++) {
        if (report->results[i].applicable) {
            report->num_tests++;
            if (report->results[i].passed)
                report->num_passed++;
            p_sum += report->results[i].p_value;
        }
    }
    report->overall_score = (report->num_tests > 0) ? p_sum / report->num_tests : 0.0;

    nist_bitstream_free(&bs);
    return 0;
}
