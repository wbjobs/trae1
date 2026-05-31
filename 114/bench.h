#ifndef BENCH_H
#define BENCH_H

#include "common.h"
#include <time.h>

typedef struct {
    double seq_read_mbps;
    double seq_write_mbps;
    double rand_read_iops;
    double rand_write_iops;
    double seq_read_latency_ms;
    double seq_write_latency_ms;
    double rand_read_latency_ms;
    double rand_write_latency_ms;
} BenchmarkResult;

int run_benchmark(BenchmarkResult *result, const char *test_path, bool encrypt);
void print_benchmark_result(BenchmarkResult *result);

#endif
