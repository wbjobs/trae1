#ifndef CSV_EXPORT_H
#define CSV_EXPORT_H

#include <stdint.h>
#include "nist_tests.h"
#include "config.h"

typedef struct {
    char     timestamp[32];
    char     device_path[MAX_PATH_LEN];
    uint64_t bytes_read;
    size_t   bit_count;
    int      num_passed;
    int      num_tests;
    double   overall_score;
    double   p_values[NIST_NUM_TESTS];
    int      test_results[NIST_NUM_TESTS];
} csv_record_t;

typedef struct {
    csv_record_t records[MAX_CSV_RECORDS];
    int          count;
    char         filename[MAX_PATH_LEN];
} csv_store_t;

int  csv_store_init(csv_store_t *store, const char *filename);
void csv_store_cleanup(csv_store_t *store);

int  csv_store_add_record(csv_store_t *store, const char *device_path,
                          uint64_t bytes_read, const nist_test_report_t *report);
int  csv_store_save(const csv_store_t *store);
int  csv_store_load(csv_store_t *store);
void csv_store_print_summary(const csv_store_t *store);

int  csv_export_to_file(const char *filename, const csv_store_t *store);

#endif
