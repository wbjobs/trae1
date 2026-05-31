#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stddef.h>

#define HRNG_VERSION_MAJOR 1
#define HRNG_VERSION_MINOR 0
#define HRNG_VERSION_PATCH 0

#define DEFAULT_SPI_DEVICE   "/dev/spidev0.0"
#define DEFAULT_SPI_MODE     0
#define DEFAULT_SPI_BITS     8
#define DEFAULT_SPI_SPEED    10000000
#define DEFAULT_SPI_DELAY    0

#define MAX_SPI_DEVICES      16
#define SPI_PATH_PREFIX      "/dev/spidev"
#define SYSFS_SPI_PATH       "/sys/class/spidev"

#define NIST_NUM_TESTS       15
#define NIST_P_VALUE_THRESHOLD  0.01
#define NIST_MIN_BIT_COUNT    100
#define NIST_BLOCK_SIZE       10000
#define NIST_TEMPLATE_LEN     9
#define NIST_BLOCK_FREQ_M     20
#define NIST_LONGEST_RUN_M    10000
#define NIST_DFT_N            1000

#define RNG_BUFFER_SIZE       (4096)
#define RNG_1KB_SIZE          (1024)
#define RNG_100KB_SIZE        (100 * 1024)
#define RNG_1MB_SIZE          (1024 * 1024)
#define RNG_10MB_SIZE         (10 * 1024 * 1024)
#define RNG_100MB_SIZE        (100 * 1024 * 1024)
#define RNG_1GB_SIZE          (1024ULL * 1024 * 1024)

#define RNG_MIN_SAMPLE_SIZE   RNG_100KB_SIZE
#define RNG_SPEED_TEST_SIZE   (16 * 1024)
#define RNG_SPEED_TEST_MS     500

#define RNG_SPEED_THRESHOLD_LOW      (100 * 1024)
#define RNG_SPEED_THRESHOLD_MEDIUM   (1 * 1024 * 1024)
#define RNG_SPEED_THRESHOLD_HIGH     (50 * 1024 * 1024)

#define NIST_QUICK_TEST_COUNT  5

#define MONITOR_INTERVAL      (5 * 60)

#define MAX_CSV_RECORDS       10000
#define CSV_FILE              "hrng_test_history.csv"

#define MAX_PATH_LEN          512
#define MAX_LOG_LEN           1024

#endif
