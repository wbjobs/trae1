#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "csv_export.h"

static const char *test_names[NIST_NUM_TESTS] = {
    "monobit", "block_freq", "cusum_forward", "runs", "longest_run",
    "rank", "fft", "nonoverlapping", "overlapping", "universal",
    "linear_complexity", "serial", "approx_entropy", "cusum_random",
    "random_excursions"
};

int csv_store_init(csv_store_t *store, const char *filename)
{
    if (!store)
        return -1;

    memset(store, 0, sizeof(*store));

    if (filename)
        strncpy(store->filename, filename, MAX_PATH_LEN - 1);
    else
        strncpy(store->filename, CSV_FILE, MAX_PATH_LEN - 1);

    store->count = 0;
    return 0;
}

void csv_store_cleanup(csv_store_t *store)
{
    if (!store)
        return;
    store->count = 0;
}

int csv_store_add_record(csv_store_t *store, const char *device_path,
                         uint64_t bytes_read, const nist_test_report_t *report)
{
    if (!store || !report)
        return -1;

    if (store->count >= MAX_CSV_RECORDS) {
        for (int i = 0; i < MAX_CSV_RECORDS - 1; i++)
            store->records[i] = store->records[i + 1];
        store->count = MAX_CSV_RECORDS - 1;
    }

    csv_record_t *rec = &store->records[store->count];
    memset(rec, 0, sizeof(*rec));

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(rec->timestamp, sizeof(rec->timestamp),
             "%Y-%m-%d %H:%M:%S", tm_info);

    if (device_path)
        strncpy(rec->device_path, device_path, MAX_PATH_LEN - 1);

    rec->bytes_read  = bytes_read;
    rec->bit_count   = report->bit_count;
    rec->num_passed  = report->num_passed;
    rec->num_tests   = report->num_tests;
    rec->overall_score = report->overall_score;

    for (int i = 0; i < NIST_NUM_TESTS; i++) {
        rec->p_values[i]     = report->results[i].p_value;
        rec->test_results[i] = report->results[i].passed;
    }

    store->count++;
    return 0;
}

int csv_store_save(const csv_store_t *store)
{
    if (!store)
        return -1;

    return csv_export_to_file(store->filename, store);
}

int csv_store_load(csv_store_t *store)
{
    if (!store)
        return -1;

    FILE *fp = fopen(store->filename, "r");
    if (!fp)
        return 0;

    char line[4096];
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return 0;
    }

    store->count = 0;

    while (fgets(line, sizeof(line), fp) != NULL &&
           store->count < MAX_CSV_RECORDS) {
        csv_record_t *rec = &store->records[store->count];
        memset(rec, 0, sizeof(*rec));

        char *token;
        char *rest = line;

        token = strtok_r(rest, ",", &rest);
        if (token) strncpy(rec->timestamp, token, sizeof(rec->timestamp) - 1);

        token = strtok_r(rest, ",", &rest);
        if (token) strncpy(rec->device_path, token, MAX_PATH_LEN - 1);

        token = strtok_r(rest, ",", &rest);
        if (token) rec->bytes_read = strtoull(token, NULL, 10);

        token = strtok_r(rest, ",", &rest);
        if (token) rec->bit_count = (size_t)strtoull(token, NULL, 10);

        token = strtok_r(rest, ",", &rest);
        if (token) rec->num_passed = atoi(token);

        token = strtok_r(rest, ",", &rest);
        if (token) rec->num_tests = atoi(token);

        token = strtok_r(rest, ",", &rest);
        if (token) rec->overall_score = atof(token);

        for (int i = 0; i < NIST_NUM_TESTS; i++) {
            token = strtok_r(rest, ",", &rest);
            if (token) rec->p_values[i] = atof(token);
        }

        for (int i = 0; i < NIST_NUM_TESTS; i++) {
            token = strtok_r(rest, ",", &rest);
            if (token) rec->test_results[i] = atoi(token);
        }

        store->count++;
    }

    fclose(fp);
    return store->count;
}

void csv_store_print_summary(const csv_store_t *store)
{
    if (!store || store->count == 0) {
        printf("  No test history records found.\n");
        return;
    }

    printf("\n  Test History Summary (%d records):\n", store->count);
    printf("  %-20s %-25s %-12s %-10s %-10s\n",
           "Timestamp", "Device", "Bytes", "Passed", "Score");
    printf("  ");
    for (int i = 0; i < 80; i++) printf("-");
    printf("\n");

    int start = (store->count > 10) ? store->count - 10 : 0;
    for (int i = start; i < store->count; i++) {
        const csv_record_t *rec = &store->records[i];
        printf("  %-20s %-25s %-12llu %d/%-7d %-10.4f\n",
               rec->timestamp,
               rec->device_path,
               (unsigned long long)rec->bytes_read,
               rec->num_passed,
               rec->num_tests,
               rec->overall_score);
    }
}

int csv_export_to_file(const char *filename, const csv_store_t *store)
{
    if (!filename || !store)
        return -1;

    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "csv_export_to_file: cannot open %s\n", filename);
        return -1;
    }

    fprintf(fp, "timestamp,device,bytes_read,bit_count,num_passed,num_tests,overall_score");
    for (int i = 0; i < NIST_NUM_TESTS; i++)
        fprintf(fp, ",p_value_%s", test_names[i]);
    for (int i = 0; i < NIST_NUM_TESTS; i++)
        fprintf(fp, ",result_%s", test_names[i]);
    fprintf(fp, "\n");

    for (int i = 0; i < store->count; i++) {
        const csv_record_t *rec = &store->records[i];

        fprintf(fp, "%s,%s,%llu,%zu,%d,%d,%.6f",
                rec->timestamp,
                rec->device_path,
                (unsigned long long)rec->bytes_read,
                rec->bit_count,
                rec->num_passed,
                rec->num_tests,
                rec->overall_score);

        for (int j = 0; j < NIST_NUM_TESTS; j++)
            fprintf(fp, ",%.6f", rec->p_values[j]);

        for (int j = 0; j < NIST_NUM_TESTS; j++)
            fprintf(fp, ",%d", rec->test_results[j]);

        fprintf(fp, "\n");
    }

    fclose(fp);
    return 0;
}
