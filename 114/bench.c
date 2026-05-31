#include "bench.h"
#include "crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define TEST_FILE_SIZE (100 * 1024 * 1024)
#define BLOCK_SIZE (64 * 1024)
#define RAND_IO_COUNT 1000
#define RAND_BLOCK_SIZE (4 * 1024)

static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static int prepare_test_file(const char *path, size_t size) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("Failed to create test file");
        return -1;
    }

    unsigned char *buffer = malloc(BLOCK_SIZE);
    if (!buffer) {
        close(fd);
        return -1;
    }

    for (size_t i = 0; i < BLOCK_SIZE; i++) {
        buffer[i] = (unsigned char)(rand() & 0xFF);
    }

    size_t remaining = size;
    while (remaining > 0) {
        size_t to_write = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
        ssize_t written = write(fd, buffer, to_write);
        if (written < 0) {
            perror("Write failed");
            free(buffer);
            close(fd);
            return -1;
        }
        remaining -= (size_t)written;
    }

    free(buffer);
    close(fd);
    return 0;
}

static double test_sequential_read(const char *path, size_t size, CryptoContext *crypto, double *avg_latency) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open test file for read");
        return -1.0;
    }

    unsigned char *buffer = malloc(BLOCK_SIZE);
    unsigned char *cipher_buffer = malloc(BLOCK_SIZE + GCM_IV_SIZE + GCM_TAG_SIZE);
    unsigned char tag[GCM_TAG_SIZE];
    if (!buffer || !cipher_buffer) {
        close(fd);
        free(buffer);
        free(cipher_buffer);
        return -1.0;
    }

    double start = get_time_ms();
    double total_latency = 0.0;
    size_t total_read = 0;
    size_t op_count = 0;

    while (total_read < size) {
        double op_start = get_time_ms();
        size_t to_read = (size - total_read > BLOCK_SIZE) ? BLOCK_SIZE : (size - total_read);
        ssize_t bytes_read = read(fd, buffer, to_read);
        if (bytes_read < 0) {
            perror("Read failed");
            free(buffer);
            free(cipher_buffer);
            close(fd);
            return -1.0;
        }

        if (crypto && bytes_read > 0) {
            int encrypted_len = crypto_encrypt(crypto, buffer, (int)bytes_read, cipher_buffer, tag);
            if (encrypted_len < 0) {
                fprintf(stderr, "Encryption failed\n");
                free(buffer);
                free(cipher_buffer);
                close(fd);
                return -1.0;
            }
        }

        total_latency += get_time_ms() - op_start;
        op_count++;
        total_read += (size_t)bytes_read;
        if (bytes_read == 0) break;
    }

    double elapsed = get_time_ms() - start;
    free(buffer);
    free(cipher_buffer);
    close(fd);

    *avg_latency = op_count > 0 ? total_latency / op_count : 0.0;
    double mb = (double)total_read / (1024.0 * 1024.0);
    double seconds = elapsed / 1000.0;
    return seconds > 0 ? mb / seconds : 0.0;
}

static double test_sequential_write(const char *path, size_t size, CryptoContext *crypto, double *avg_latency) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("Failed to open test file for write");
        return -1.0;
    }

    unsigned char *buffer = malloc(BLOCK_SIZE);
    unsigned char *cipher_buffer = malloc(BLOCK_SIZE + GCM_IV_SIZE + GCM_TAG_SIZE);
    unsigned char tag[GCM_TAG_SIZE];
    if (!buffer || !cipher_buffer) {
        close(fd);
        free(buffer);
        free(cipher_buffer);
        return -1.0;
    }

    for (size_t i = 0; i < BLOCK_SIZE; i++) {
        buffer[i] = (unsigned char)(rand() & 0xFF);
    }

    double start = get_time_ms();
    double total_latency = 0.0;
    size_t total_written = 0;
    size_t op_count = 0;

    while (total_written < size) {
        double op_start = get_time_ms();
        size_t to_write = (size - total_written > BLOCK_SIZE) ? BLOCK_SIZE : (size - total_written);
        
        unsigned char *data_to_write = buffer;
        size_t data_len = to_write;

        if (crypto) {
            int encrypted_len = crypto_encrypt(crypto, buffer, (int)to_write, cipher_buffer, tag);
            if (encrypted_len < 0) {
                fprintf(stderr, "Encryption failed\n");
                free(buffer);
                free(cipher_buffer);
                close(fd);
                return -1.0;
            }
            data_to_write = cipher_buffer;
            data_len = (size_t)encrypted_len;
        }

        ssize_t written = write(fd, data_to_write, data_len);
        if (written < 0) {
            perror("Write failed");
            free(buffer);
            free(cipher_buffer);
            close(fd);
            return -1.0;
        }

        total_latency += get_time_ms() - op_start;
        op_count++;
        total_written += to_write;
    }

    double elapsed = get_time_ms() - start;
    free(buffer);
    free(cipher_buffer);
    close(fd);

    *avg_latency = op_count > 0 ? total_latency / op_count : 0.0;
    double mb = (double)total_written / (1024.0 * 1024.0);
    double seconds = elapsed / 1000.0;
    return seconds > 0 ? mb / seconds : 0.0;
}

static double test_random_read(const char *path, size_t file_size, CryptoContext *crypto, double *avg_latency) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open test file for random read");
        return -1.0;
    }

    unsigned char *buffer = malloc(RAND_BLOCK_SIZE);
    unsigned char *cipher_buffer = malloc(RAND_BLOCK_SIZE + GCM_IV_SIZE + GCM_TAG_SIZE);
    unsigned char tag[GCM_TAG_SIZE];
    if (!buffer || !cipher_buffer) {
        close(fd);
        free(buffer);
        free(cipher_buffer);
        return -1.0;
    }

    double start = get_time_ms();
    double total_latency = 0.0;
    int success_count = 0;

    for (int i = 0; i < RAND_IO_COUNT; i++) {
        double op_start = get_time_ms();
        off_t offset = (off_t)(((unsigned long long)rand() * RAND_BLOCK_SIZE) % (file_size - RAND_BLOCK_SIZE));
        if (lseek(fd, offset, SEEK_SET) < 0) {
            continue;
        }

        ssize_t bytes_read = read(fd, buffer, RAND_BLOCK_SIZE);
        if (bytes_read < 0) {
            continue;
        }

        if (crypto && bytes_read > 0) {
            crypto_encrypt(crypto, buffer, (int)bytes_read, cipher_buffer, tag);
        }

        total_latency += get_time_ms() - op_start;
        success_count++;
    }

    double elapsed = get_time_ms() - start;
    free(buffer);
    free(cipher_buffer);
    close(fd);

    *avg_latency = success_count > 0 ? total_latency / success_count : 0.0;
    double seconds = elapsed / 1000.0;
    return seconds > 0 ? (double)success_count / seconds : 0.0;
}

static double test_random_write(const char *path, size_t file_size, CryptoContext *crypto, double *avg_latency) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open test file for random write");
        return -1.0;
    }

    unsigned char *buffer = malloc(RAND_BLOCK_SIZE);
    unsigned char *cipher_buffer = malloc(RAND_BLOCK_SIZE + GCM_IV_SIZE + GCM_TAG_SIZE);
    unsigned char tag[GCM_TAG_SIZE];
    if (!buffer || !cipher_buffer) {
        close(fd);
        free(buffer);
        free(cipher_buffer);
        return -1.0;
    }

    for (size_t i = 0; i < RAND_BLOCK_SIZE; i++) {
        buffer[i] = (unsigned char)(rand() & 0xFF);
    }

    double start = get_time_ms();
    double total_latency = 0.0;
    int success_count = 0;

    for (int i = 0; i < RAND_IO_COUNT; i++) {
        double op_start = get_time_ms();
        off_t offset = (off_t)(((unsigned long long)rand() * RAND_BLOCK_SIZE) % (file_size - RAND_BLOCK_SIZE));
        if (lseek(fd, offset, SEEK_SET) < 0) {
            continue;
        }

        unsigned char *data_to_write = buffer;
        size_t data_len = RAND_BLOCK_SIZE;

        if (crypto) {
            int encrypted_len = crypto_encrypt(crypto, buffer, (int)RAND_BLOCK_SIZE, cipher_buffer, tag);
            if (encrypted_len > 0) {
                data_to_write = cipher_buffer;
                data_len = (size_t)encrypted_len;
            }
        }

        ssize_t written = write(fd, data_to_write, data_len);
        if (written < 0) {
            continue;
        }

        total_latency += get_time_ms() - op_start;
        success_count++;
    }

    double elapsed = get_time_ms() - start;
    free(buffer);
    free(cipher_buffer);
    close(fd);

    *avg_latency = success_count > 0 ? total_latency / success_count : 0.0;
    double seconds = elapsed / 1000.0;
    return seconds > 0 ? (double)success_count / seconds : 0.0;
}

int run_benchmark(BenchmarkResult *result, const char *test_path, bool encrypt) {
    memset(result, 0, sizeof(BenchmarkResult));
    srand((unsigned int)time(NULL));

    char test_file[MAX_PATH_LENGTH];
    snprintf(test_file, sizeof(test_file), "%s/bench_test.tmp", 
             test_path && strlen(test_path) > 0 ? test_path : ".");

    printf("Preparing test file (%d MB)...\n", TEST_FILE_SIZE / (1024 * 1024));
    if (prepare_test_file(test_file, TEST_FILE_SIZE) != 0) {
        return -1;
    }

    CryptoContext crypto_ctx;
    CryptoContext *crypto_ptr = NULL;
    if (encrypt) {
        printf("Initializing AES-128-GCM encryption...\n");
        if (crypto_init(&crypto_ctx) != 0) {
            unlink(test_file);
            return -1;
        }
        crypto_ptr = &crypto_ctx;
    }

    printf("\nRunning Sequential Read Test...\n");
    result->seq_read_mbps = test_sequential_read(test_file, TEST_FILE_SIZE, crypto_ptr, &result->seq_read_latency_ms);
    printf("Sequential Read: %.2f MB/s, Avg Latency: %.3f ms\n", 
           result->seq_read_mbps, result->seq_read_latency_ms);

    printf("\nRunning Sequential Write Test...\n");
    result->seq_write_mbps = test_sequential_write(test_file, TEST_FILE_SIZE, crypto_ptr, &result->seq_write_latency_ms);
    printf("Sequential Write: %.2f MB/s, Avg Latency: %.3f ms\n", 
           result->seq_write_mbps, result->seq_write_latency_ms);

    printf("\nRunning Random Read Test (%d operations)...\n", RAND_IO_COUNT);
    result->rand_read_iops = test_random_read(test_file, TEST_FILE_SIZE, crypto_ptr, &result->rand_read_latency_ms);
    printf("Random Read: %.2f IOPS, Avg Latency: %.3f ms\n", 
           result->rand_read_iops, result->rand_read_latency_ms);

    printf("\nRunning Random Write Test (%d operations)...\n", RAND_IO_COUNT);
    result->rand_write_iops = test_random_write(test_file, TEST_FILE_SIZE, crypto_ptr, &result->rand_write_latency_ms);
    printf("Random Write: %.2f IOPS, Avg Latency: %.3f ms\n", 
           result->rand_write_iops, result->rand_write_latency_ms);

    if (encrypt) {
        crypto_cleanup(&crypto_ctx);
    }

    unlink(test_file);
    return 0;
}

void print_benchmark_result(BenchmarkResult *result) {
    printf("\n");
    printf("========================================\n");
    printf("   SMB Encrypt Proxy Benchmark Result   \n");
    printf("========================================\n");
    printf("\n");
    printf("Sequential Read:  %.2f MB/s (Avg: %.3f ms)\n", 
           result->seq_read_mbps, result->seq_read_latency_ms);
    printf("Sequential Write: %.2f MB/s (Avg: %.3f ms)\n", 
           result->seq_write_mbps, result->seq_write_latency_ms);
    printf("Random Read:      %.2f IOPS (Avg: %.3f ms)\n", 
           result->rand_read_iops, result->rand_read_latency_ms);
    printf("Random Write:     %.2f IOPS (Avg: %.3f ms)\n", 
           result->rand_write_iops, result->rand_write_latency_ms);
    printf("\n");
}
