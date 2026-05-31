#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "report.h"

static int use_color = 1;

static void color_init(void)
{
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    if (getenv("NO_COLOR") != NULL)
        use_color = 0;
    if (!isatty(fileno(stdout)))
        use_color = 0;
}

#define COLOR_RED    (use_color ? "\033[31m" : "")
#define COLOR_GREEN  (use_color ? "\033[32m" : "")
#define COLOR_YELLOW (use_color ? "\033[33m" : "")
#define COLOR_CYAN   (use_color ? "\033[36m" : "")
#define COLOR_BOLD   (use_color ? "\033[1m" : "")
#define COLOR_RESET  (use_color ? "\033[0m" : "")

static void print_separator(int width)
{
    for (int i = 0; i < width; i++)
        printf("─");
    printf("\n");
}

static void print_double_separator(int width)
{
    for (int i = 0; i < width; i++)
        printf("═");
    printf("\n");
}

void report_print_header(const char *device_path, uint64_t total_bytes,
                         double elapsed_sec, int adaptive_mode,
                         int quick_mode, double speed_bytes_sec)
{
    color_init();

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    printf("\n");
    print_double_separator(70);
    printf(COLOR_BOLD COLOR_CYAN
           "  HRNG Evaluation Tool v%d.%d.%d\n"
           COLOR_RESET,
           HRNG_VERSION_MAJOR, HRNG_VERSION_MINOR, HRNG_VERSION_PATCH);
    printf(COLOR_BOLD
           "  Hardware Random Number Generator Testing Suite\n"
           COLOR_RESET);
    print_double_separator(70);

    printf("  Test Time       : %s\n", time_str);
    printf("  SPI Device      : %s\n", device_path ? device_path : "N/A");
    printf("  Data Collected  : %llu bytes (%.2f MB)\n",
           (unsigned long long)total_bytes,
           (double)total_bytes / (1024.0 * 1024.0));
    printf("  Elapsed Time    : %.2f seconds\n", elapsed_sec);
    printf("  P-Value Thresh  : %.2f\n", NIST_P_VALUE_THRESHOLD);
    printf("  Test Standard   : NIST SP 800-22 Rev.1a\n");
    printf("  Test Mode       : %s\n", quick_mode ? "Quick (5 core tests)" : "Full (15 tests)");
    printf("  Adaptive Samp.  : %s\n", adaptive_mode ? "Enabled" : "Disabled");

    if (speed_bytes_sec > 0) {
        printf("  Device Speed    : %.2f KB/s", speed_bytes_sec / 1024.0);
        if (speed_bytes_sec < RNG_SPEED_THRESHOLD_LOW)
            printf(" (Low)");
        else if (speed_bytes_sec < RNG_SPEED_THRESHOLD_MEDIUM)
            printf(" (Medium)");
        else if (speed_bytes_sec < RNG_SPEED_THRESHOLD_HIGH)
            printf(" (High)");
        else
            printf(" (Very High)");
        printf("\n");
    }

    print_separator(70);
}

void report_print_results(const nist_test_report_t *nist_report)
{
    if (!nist_report)
        return;

    printf("\n");
    printf(COLOR_BOLD
           "  %-38s %-12s %-10s %s\n"
           COLOR_RESET,
           "Test Name", "P-Value", "Result", "Status");
    print_separator(70);

    for (int i = 0; i < NIST_NUM_TESTS; i++) {
        const nist_test_result_t *r = &nist_report->results[i];

        printf("  %-38s ", r->name);

        if (r->applicable) {
            if (r->p_value >= 0.0001)
                printf(COLOR_CYAN "%-12.6f" COLOR_RESET " ", r->p_value);
            else
                printf(COLOR_CYAN "%-12.2e" COLOR_RESET " ", r->p_value);

            if (r->passed)
                printf(COLOR_GREEN "%-10s %s" COLOR_RESET,
                       "PASS", "✓");
            else
                printf(COLOR_RED "%-10s %s" COLOR_RESET,
                       "FAIL", "✗");
        } else {
            printf(COLOR_YELLOW "%-12s %-10s %s" COLOR_RESET,
                   "N/A", "SKIPPED", "-");
        }

        printf("\n");
    }
}

void report_print_summary(const nist_test_report_t *nist_report)
{
    if (!nist_report)
        return;

    printf("\n");
    print_separator(70);

    printf(COLOR_BOLD
           "  Test Summary:\n"
           COLOR_RESET);

    printf("    Bits Tested     : %zu bits (%zu bytes)\n",
           nist_report->bit_count,
           nist_report->bit_count / 8);
    printf("    Tests Run       : %d / %d\n",
           nist_report->num_tests, NIST_NUM_TESTS);
    printf("    Tests Passed    : " COLOR_GREEN "%d" COLOR_RESET "\n",
           nist_report->num_passed);
    printf("    Tests Failed    : " COLOR_RED "%d" COLOR_RESET "\n",
           nist_report->num_tests - nist_report->num_passed);
    printf("    Overall Score   : %.4f\n", nist_report->overall_score);

    if (nist_report->num_passed == NIST_NUM_TESTS) {
        printf("\n" COLOR_GREEN COLOR_BOLD
               "  RESULT: ALL TESTS PASSED\n"
               COLOR_RESET);
    } else if (nist_report->num_passed >= NIST_NUM_TESTS - 2) {
        printf("\n" COLOR_YELLOW COLOR_BOLD
               "  RESULT: MOST TESTS PASSED (%d/%d)\n"
               COLOR_RESET,
               nist_report->num_passed, NIST_NUM_TESTS);
    } else {
        printf("\n" COLOR_RED COLOR_BOLD
               "  RESULT: TESTS FAILED (%d/%d passed)\n"
               COLOR_RESET,
               nist_report->num_passed, NIST_NUM_TESTS);
    }

    print_double_separator(70);
    printf("\n");
}

void report_print_full(const char *device_path, uint64_t total_bytes,
                       double elapsed_sec, const nist_test_report_t *nist_report,
                       int adaptive_mode, int quick_mode, double speed_bytes_sec)
{
    report_print_header(device_path, total_bytes, elapsed_sec,
                        adaptive_mode, quick_mode, speed_bytes_sec);
    report_print_results(nist_report);
    report_print_summary(nist_report);
}
