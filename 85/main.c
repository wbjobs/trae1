#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>

#include "config.h"
#include "spi.h"
#include "rng_reader.h"
#include "nist_tests.h"
#include "report.h"
#include "csv_export.h"

static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_export_requested = 0;

typedef struct {
    char    device_path[MAX_PATH_LEN];
    char    csv_path[MAX_PATH_LEN];
    uint8_t spi_mode;
    uint8_t spi_bits;
    uint32_t spi_speed;

    int     monitor_mode;
    int     monitor_interval;
    uint64_t total_bytes;
    int     total_bytes_set;
    size_t  sample_size;
    int     sample_size_set;

    int     quick_mode;
    int     adaptive_mode;

    int     list_devices;
    int     export_csv;
    int     show_help;
    int     show_version;
    int     verbose;
    int     no_color;
} cli_options_t;

static void print_usage(const char *prog_name)
{
    printf("Usage: %s [OPTIONS]\n\n", prog_name);
    printf("Hardware Random Number Generator Evaluation Tool\n");
    printf("NIST SP 800-22 Compliance Testing Suite\n\n");
    printf("Options:\n");
    printf("  -d, --device <path>       SPI device path (default: %s)\n",
           DEFAULT_SPI_DEVICE);
    printf("  -m, --mode <mode>         SPI mode (0-3, default: %d)\n",
           DEFAULT_SPI_MODE);
    printf("  -b, --bits <bits>         SPI bits per word (default: %d)\n",
           DEFAULT_SPI_BITS);
    printf("  -s, --speed <hz>          SPI speed in Hz (default: %u)\n",
           DEFAULT_SPI_SPEED);
    printf("\n");
    printf("  -t, --total <bytes>       Total bytes to read (default: adaptive)\n");
    printf("  -z, --sample <bytes>      Sample size per test (default: adaptive)\n");
    printf("  --sample-size <bytes>     Alias for --sample\n");
    printf("  --quick                   Quick mode: only 5 fast core tests\n");
    printf("  --no-adaptive             Disable adaptive sampling (use fixed size)\n");
    printf("\n");
    printf("  --monitor                 Enable continuous monitoring mode\n");
    printf("  --interval <seconds>      Monitor interval (default: 300s/5min)\n");
    printf("\n");
    printf("  --list-devices            List available SPI devices\n");
    printf("  --export-csv [path]       Export test history to CSV\n");
    printf("  --csv-path <path>         CSV file path for history (default: %s)\n",
           CSV_FILE);
    printf("\n");
    printf("  -v, --verbose             Enable verbose output\n");
    printf("  --no-color                Disable colored output\n");
    printf("  -V, --version             Show version information\n");
    printf("  -h, --help                Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s --device /dev/spidev0.0 --total 1073741824\n", prog_name);
    printf("  %s --monitor --interval 300 --export-csv\n", prog_name);
    printf("  %s --list-devices\n", prog_name);
}

static void print_version(void)
{
    printf("HRNG Evaluation Tool v%d.%d.%d\n",
           HRNG_VERSION_MAJOR,
           HRNG_VERSION_MINOR,
           HRNG_VERSION_PATCH);
    printf("NIST SP 800-22 Randomness Test Suite\n");
    printf("Build: %s %s\n", __DATE__, __TIME__);
}

static void list_spi_devices(void)
{
    spi_device_info_t devices[MAX_SPI_DEVICES];
    int count = spi_discover_devices(devices, MAX_SPI_DEVICES);

    if (count == 0) {
        printf("No SPI devices found.\n");
        return;
    }

    printf("Found %d SPI device(s):\n\n", count);
    printf("%-25s %-8s %-8s %-10s\n",
           "Device Path", "Bus", "CS", "Available");
    printf("---------------------------------------------------------\n");

    for (int i = 0; i < count; i++) {
        printf("%-25s %-8u %-8u %-10s\n",
               devices[i].device_path,
               devices[i].bus_num,
               devices[i].chip_select,
               spi_is_device_available(devices[i].device_path)
                   ? "Yes" : "No");
    }
}

static int parse_options(int argc, char *argv[], cli_options_t *opts)
{
    memset(opts, 0, sizeof(*opts));

    strncpy(opts->device_path, DEFAULT_SPI_DEVICE, MAX_PATH_LEN - 1);
    strncpy(opts->csv_path, CSV_FILE, MAX_PATH_LEN - 1);
    opts->spi_mode     = DEFAULT_SPI_MODE;
    opts->spi_bits     = DEFAULT_SPI_BITS;
    opts->spi_speed    = DEFAULT_SPI_SPEED;
    opts->total_bytes  = RNG_1GB_SIZE;
    opts->total_bytes_set = 0;
    opts->sample_size  = RNG_1MB_SIZE;
    opts->sample_size_set = 0;
    opts->monitor_interval = MONITOR_INTERVAL;
    opts->adaptive_mode = 1;

    static struct option long_options[] = {
        {"device",      required_argument, 0, 'd'},
        {"mode",        required_argument, 0, 'm'},
        {"bits",        required_argument, 0, 'b'},
        {"speed",       required_argument, 0, 's'},
        {"total",       required_argument, 0, 't'},
        {"sample",      required_argument, 0, 'z'},
        {"sample-size", required_argument, 0, 'z'},
        {"quick",       no_argument,       0, 1000},
        {"no-adaptive", no_argument,       0, 1001},
        {"monitor",     no_argument,       0, 1002},
        {"interval",    required_argument, 0, 1003},
        {"list-devices", no_argument,      0, 1004},
        {"export-csv",  optional_argument, 0, 1005},
        {"csv-path",    required_argument, 0, 1006},
        {"verbose",     no_argument,       0, 'v'},
        {"no-color",    no_argument,       0, 1007},
        {"version",     no_argument,       0, 'V'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index;

    while ((opt = getopt_long(argc, argv, "d:m:b:s:t:z:vVh",
                              long_options, &option_index)) != -1) {
        switch (opt) {
        case 'd':
            strncpy(opts->device_path, optarg, MAX_PATH_LEN - 1);
            break;
        case 'm':
            opts->spi_mode = (uint8_t)atoi(optarg);
            break;
        case 'b':
            opts->spi_bits = (uint8_t)atoi(optarg);
            break;
        case 's':
            opts->spi_speed = (uint32_t)strtoul(optarg, NULL, 10);
            break;
        case 't':
            opts->total_bytes = strtoull(optarg, NULL, 10);
            opts->total_bytes_set = 1;
            break;
        case 'z':
            opts->sample_size = (size_t)strtoul(optarg, NULL, 10);
            opts->sample_size_set = 1;
            break;
        case 'v':
            opts->verbose = 1;
            break;
        case 'V':
            opts->show_version = 1;
            break;
        case 'h':
            opts->show_help = 1;
            break;
        case 1000:
            opts->quick_mode = 1;
            break;
        case 1001:
            opts->adaptive_mode = 0;
            break;
        case 1002:
            opts->monitor_mode = 1;
            break;
        case 1003:
            opts->monitor_interval = atoi(optarg);
            break;
        case 1004:
            opts->list_devices = 1;
            break;
        case 1005:
            opts->export_csv = 1;
            if (optarg)
                strncpy(opts->csv_path, optarg, MAX_PATH_LEN - 1);
            break;
        case 1006:
            strncpy(opts->csv_path, optarg, MAX_PATH_LEN - 1);
            break;
        case 1007:
            opts->no_color = 1;
            break;
        default:
            return -1;
        }
    }

    return 0;
}

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static void usr1_handler(int sig)
{
    (void)sig;
    g_export_requested = 1;
}

static double get_timestamp(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static int run_batch_test(const cli_options_t *opts)
{
    spi_device_t spi_dev;
    rng_reader_t reader;
    nist_test_report_t nist_report;
    csv_store_t csv_store;
    int ret = -1;

    if (opts->no_color)
        setenv("NO_COLOR", "1", 1);

    printf("\n");
    printf("  Initializing HRNG Evaluation...\n");
    printf("  SPI Device: %s\n", opts->device_path);
    printf("  Mode: %s\n", opts->quick_mode ? "Quick (5 core tests)" : "Full (15 tests)");
    printf("  Adaptive Sampling: %s\n", opts->adaptive_mode ? "Enabled" : "Disabled");

    if (spi_open(&spi_dev, opts->device_path) < 0) {
        fprintf(stderr, "  ERROR: Failed to open SPI device %s\n",
                opts->device_path);
        return -1;
    }

    if (spi_configure(&spi_dev, opts->spi_mode,
                      opts->spi_bits, opts->spi_speed) < 0) {
        fprintf(stderr, "  ERROR: Failed to configure SPI\n");
        spi_close(&spi_dev);
        return -1;
    }

    printf("  SPI Configuration: mode=%u, bits=%u, speed=%u Hz\n",
           spi_dev.mode, spi_dev.bits_per_word, spi_dev.speed_hz);

    if (rng_reader_init(&reader, &spi_dev) < 0) {
        fprintf(stderr, "  ERROR: Failed to initialize RNG reader\n");
        spi_close(&spi_dev);
        return -1;
    }

    double measured_speed = 0.0;
    size_t optimal_sample = RNG_1MB_SIZE;
    uint64_t target_total = opts->total_bytes;

    if (opts->adaptive_mode && !opts->sample_size_set) {
        printf("\n");
        printf("  Measuring HRNG throughput...\n");
        measured_speed = rng_reader_measure_speed(&reader, RNG_SPEED_TEST_SIZE);
        optimal_sample = rng_reader_get_sample_for_speed(measured_speed);

        if (optimal_sample < RNG_MIN_SAMPLE_SIZE)
            optimal_sample = RNG_MIN_SAMPLE_SIZE;

        printf("  Measured Speed: %.2f KB/s (%s)\n",
               measured_speed / 1024.0,
               rng_reader_speed_category(measured_speed));
        printf("  Optimal Sample: %zu bytes (%.2f MB)\n",
               optimal_sample,
               (double)optimal_sample / (1024.0 * 1024.0));

        double estimated_time = (double)optimal_sample / measured_speed;
        printf("  Estimated Read Time: %.1f seconds\n", estimated_time);

        if (!opts->total_bytes_set) {
            if (measured_speed < RNG_SPEED_THRESHOLD_LOW) {
                target_total = optimal_sample;
                printf("  Auto-adjusted total bytes: %zu bytes (low speed)\n",
                       optimal_sample);
            }
        }
    } else {
        optimal_sample = opts->sample_size;
        if (optimal_sample < RNG_MIN_SAMPLE_SIZE) {
            printf("  Warning: sample size below minimum, using %zu bytes\n",
                   (size_t)RNG_MIN_SAMPLE_SIZE);
            optimal_sample = RNG_MIN_SAMPLE_SIZE;
        }
    }

    if (target_total < optimal_sample)
        target_total = optimal_sample;

    printf("  Final Sample Size: %zu bytes (%.2f MB)\n",
           optimal_sample,
           (double)optimal_sample / (1024.0 * 1024.0));
    printf("  Final Target Total: %llu bytes (%.2f MB)\n",
           (unsigned long long)target_total,
           (double)target_total / (1024.0 * 1024.0));

    csv_store_init(&csv_store, opts->csv_path);
    csv_store_load(&csv_store);

    double start_time = get_timestamp();

    uint64_t bytes_processed = 0;
    int sample_num = 0;

    while (g_running && bytes_processed < target_total) {
        size_t chunk = (target_total - bytes_processed > optimal_sample)
                           ? optimal_sample
                           : (size_t)(target_total - bytes_processed);

        rng_sample_t sample;
        if (rng_reader_read_sample(&reader, &sample, chunk) < 0) {
            fprintf(stderr, "  ERROR: Failed to read RNG sample\n");
            break;
        }

        if (nist_run_adaptive_tests(sample.data, sample.len,
                                     &nist_report, opts->quick_mode) < 0) {
            fprintf(stderr, "  ERROR: NIST tests failed\n");
            rng_sample_free(&sample);
            break;
        }

        bytes_processed += chunk;
        sample_num++;

        double elapsed = get_timestamp() - start_time;
        double rate = (double)bytes_processed / elapsed / (1024.0 * 1024.0);

        printf("\n");
        printf("  Sample #%d - Processed: %llu / %llu bytes (%.1f%%)\n",
               sample_num,
               (unsigned long long)bytes_processed,
               (unsigned long long)target_total,
               (double)bytes_processed / target_total * 100.0);
        printf("  Throughput: %.2f MB/s | Elapsed: %.1fs\n",
               rate, elapsed);

        if (opts->verbose) {
            report_print_results(&nist_report);
            report_print_summary(&nist_report);
        }

        if (opts->export_csv) {
            csv_store_add_record(&csv_store, opts->device_path,
                                 bytes_processed, &nist_report);
            csv_store_save(&csv_store);
        }

        rng_sample_free(&sample);
    }

    double total_elapsed = get_timestamp() - start_time;

    if (bytes_processed > 0) {
        size_t final_size = (bytes_processed > reader.buffer_size)
                               ? reader.buffer_size : (size_t)bytes_processed;
        if (final_size > optimal_sample)
            final_size = optimal_sample;

        if (nist_run_adaptive_tests(reader.buffer, final_size,
                                     &nist_report, opts->quick_mode) == 0) {
            report_print_full(opts->device_path,
                              bytes_processed,
                              total_elapsed,
                              &nist_report,
                              opts->adaptive_mode,
                              opts->quick_mode,
                              measured_speed);
        }
    }

    if (opts->export_csv) {
        if (csv_store_save(&csv_store) == 0) {
            printf("  Test history saved to: %s\n", opts->csv_path);
            csv_store_print_summary(&csv_store);
        }
    }

    ret = 0;

    csv_store_cleanup(&csv_store);
    rng_reader_cleanup(&reader);
    spi_close(&spi_dev);

    return ret;
}

static int run_monitor_mode(const cli_options_t *opts)
{
    spi_device_t spi_dev;
    rng_reader_t reader;
    nist_test_report_t nist_report;
    csv_store_t csv_store;
    int ret = -1;

    if (opts->no_color)
        setenv("NO_COLOR", "1", 1);

    printf("\n");
    printf("  HRNG Monitor Mode Started\n");
    printf("  Mode: %s\n", opts->quick_mode ? "Quick (5 core tests)" : "Full (15 tests)");
    printf("  Adaptive Sampling: %s\n", opts->adaptive_mode ? "Enabled" : "Disabled");

    if (spi_open(&spi_dev, opts->device_path) < 0) {
        fprintf(stderr, "  ERROR: Failed to open SPI device\n");
        return -1;
    }

    if (spi_configure(&spi_dev, opts->spi_mode,
                      opts->spi_bits, opts->spi_speed) < 0) {
        fprintf(stderr, "  ERROR: Failed to configure SPI\n");
        spi_close(&spi_dev);
        return -1;
    }

    if (rng_reader_init(&reader, &spi_dev) < 0) {
        fprintf(stderr, "  ERROR: Failed to initialize RNG reader\n");
        spi_close(&spi_dev);
        return -1;
    }

    double measured_speed = 0.0;
    size_t monitor_sample = RNG_1MB_SIZE;

    if (opts->adaptive_mode && !opts->sample_size_set) {
        printf("\n");
        printf("  Measuring HRNG throughput...\n");
        measured_speed = rng_reader_measure_speed(&reader, RNG_SPEED_TEST_SIZE);
        monitor_sample = rng_reader_get_sample_for_speed(measured_speed);

        if (monitor_sample < RNG_MIN_SAMPLE_SIZE)
            monitor_sample = RNG_MIN_SAMPLE_SIZE;
        if (monitor_sample > RNG_10MB_SIZE)
            monitor_sample = RNG_10MB_SIZE;

        printf("  Measured Speed: %.2f KB/s (%s)\n",
               measured_speed / 1024.0,
               rng_reader_speed_category(measured_speed));
    } else {
        monitor_sample = opts->sample_size;
        if (monitor_sample < RNG_MIN_SAMPLE_SIZE) {
            printf("  Warning: sample size below minimum, using %zu bytes\n",
                   (size_t)RNG_MIN_SAMPLE_SIZE);
            monitor_sample = RNG_MIN_SAMPLE_SIZE;
        }
    }

    printf("  Sample size: %zu bytes (%.2f MB)\n",
           monitor_sample,
           (double)monitor_sample / (1024.0 * 1024.0));
    printf("  Test interval: %d seconds\n", opts->monitor_interval);
    printf("  Press Ctrl+C to stop\n\n");

    csv_store_init(&csv_store, opts->csv_path);
    csv_store_load(&csv_store);

    int test_count = 0;

    while (g_running) {
        double start_time = get_timestamp();

        rng_sample_t sample;
        if (rng_reader_read_sample(&reader, &sample, monitor_sample) < 0) {
            fprintf(stderr, "  ERROR: Failed to read sample\n");
            sleep(1);
            continue;
        }

        if (nist_run_adaptive_tests(sample.data, sample.len,
                                     &nist_report, opts->quick_mode) < 0) {
            fprintf(stderr, "  ERROR: NIST tests failed\n");
            rng_sample_free(&sample);
            sleep(1);
            continue;
        }

        double elapsed = get_timestamp() - start_time;
        test_count++;

        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

        printf("  [%s] Test #%d: %d/%d tests passed | Score: %.4f | Time: %.2fs\n",
               time_str,
               test_count,
               nist_report.num_passed,
               nist_report.num_tests,
               nist_report.overall_score,
               elapsed);

        if (opts->verbose || (nist_report.num_tests > 0 &&
            nist_report.num_passed < nist_report.num_tests - 1)) {
            report_print_results(&nist_report);
        }

        if (opts->export_csv || g_export_requested) {
            csv_store_add_record(&csv_store, opts->device_path,
                                 reader.total_bytes_read, &nist_report);
            csv_store_save(&csv_store);

            if (g_export_requested) {
                printf("\n  CSV export requested - saving history...\n");
                csv_export_to_file(opts->csv_path, &csv_store);
                printf("  History exported to: %s\n", opts->csv_path);
                g_export_requested = 0;
            }
        }

        rng_sample_free(&sample);

        int wait_time = opts->monitor_interval;
        while (g_running && wait_time > 0) {
            sleep(1);
            wait_time--;
            if (g_export_requested) {
                csv_store_save(&csv_store);
                printf("\n  CSV export requested - saving...\n");
                csv_export_to_file(opts->csv_path, &csv_store);
                g_export_requested = 0;
            }
        }
    }

    printf("\n\n  Monitor stopped. %d tests completed.\n", test_count);

    if (opts->export_csv) {
        csv_store_save(&csv_store);
        csv_store_print_summary(&csv_store);
        printf("  Test history saved to: %s\n", opts->csv_path);
    }

    ret = 0;

    csv_store_cleanup(&csv_store);
    rng_reader_cleanup(&reader);
    spi_close(&spi_dev);

    return ret;
}

int main(int argc, char *argv[])
{
    cli_options_t opts;

    if (parse_options(argc, argv, &opts) < 0) {
        print_usage(argv[0]);
        return 1;
    }

    if (opts.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    if (opts.show_version) {
        print_version();
        return 0;
    }

    if (opts.list_devices) {
        list_spi_devices();
        return 0;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGUSR1, usr1_handler);

    if (!spi_is_device_available(opts.device_path)) {
        fprintf(stderr, "Warning: SPI device %s is not accessible.\n",
                opts.device_path);
        fprintf(stderr, "Please ensure:\n");
        fprintf(stderr, "  1. The SPI interface is enabled in kernel\n");
        fprintf(stderr, "  2. You have appropriate permissions (root/group)\n");
        fprintf(stderr, "  3. Use --list-devices to see available devices\n\n");
    }

    if (opts.export_csv && !opts.monitor_mode && !opts.total_bytes_set) {
        csv_store_t store;
        csv_store_init(&store, opts.csv_path);
        int loaded = csv_store_load(&store);
        printf("Loaded %d historical records from %s\n",
               loaded, opts.csv_path);
        csv_store_print_summary(&store);

        char export_path[MAX_PATH_LEN];
        snprintf(export_path, sizeof(export_path), "export_%s",
                 opts.csv_path);
        if (csv_export_to_file(export_path, &store) == 0) {
            printf("CSV exported to: %s\n", export_path);
        }
        csv_store_cleanup(&store);
        return 0;
    }

    int ret;

    if (opts.monitor_mode) {
        ret = run_monitor_mode(&opts);
    } else {
        ret = run_batch_test(&opts);
    }

    return ret;
}
