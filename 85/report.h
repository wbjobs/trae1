#ifndef REPORT_H
#define REPORT_H

#include "nist_tests.h"
#include "config.h"

void report_print_header(const char *device_path, uint64_t total_bytes,
                         double elapsed_sec, int adaptive_mode,
                         int quick_mode, double speed_bytes_sec);
void report_print_results(const nist_test_report_t *nist_report);
void report_print_summary(const nist_test_report_t *nist_report);
void report_print_full(const char *device_path, uint64_t total_bytes,
                       double elapsed_sec, const nist_test_report_t *nist_report,
                       int adaptive_mode, int quick_mode, double speed_bytes_sec);

#endif
