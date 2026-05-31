#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>

#include "rng_reader.h"

int rng_reader_init(rng_reader_t *reader, spi_device_t *spi)
{
    if (!reader || !spi)
        return -1;

    memset(reader, 0, sizeof(*reader));

    reader->spi_dev     = spi;
    reader->buffer_size = RNG_BUFFER_SIZE;
    reader->buffer      = (uint8_t *)malloc(RNG_BUFFER_SIZE);

    if (!reader->buffer) {
        fprintf(stderr, "rng_reader_init: malloc failed\n");
        return -1;
    }

    reader->total_bytes_read = 0;
    reader->initialized      = 1;
    reader->start_time       = time(NULL);

    return 0;
}

void rng_reader_cleanup(rng_reader_t *reader)
{
    if (!reader)
        return;

    if (reader->buffer) {
        free(reader->buffer);
        reader->buffer = NULL;
    }

    reader->spi_dev          = NULL;
    reader->total_bytes_read = 0;
    reader->initialized      = 0;
}

int rng_reader_read_bytes(rng_reader_t *reader, uint8_t *buf, size_t len)
{
    if (!reader || !reader->initialized || !buf || len == 0)
        return -1;

    size_t total_read = 0;

    while (total_read < len) {
        size_t chunk = len - total_read;
        if (chunk > RNG_BUFFER_SIZE)
            chunk = RNG_BUFFER_SIZE;

        int ret = spi_read(reader->spi_dev, reader->buffer, chunk);
        if (ret < 0) {
            fprintf(stderr, "rng_reader_read_bytes: spi_read failed\n");
            return -1;
        }

        memcpy(buf + total_read, reader->buffer, ret);
        total_read += ret;
        reader->total_bytes_read += ret;
    }

    return (int)total_read;
}

int rng_reader_read_sample(rng_reader_t *reader, rng_sample_t *sample, size_t len)
{
    if (!reader || !reader->initialized || !sample || len == 0)
        return -1;

    sample->data = (uint8_t *)malloc(len);
    if (!sample->data) {
        fprintf(stderr, "rng_reader_read_sample: malloc failed\n");
        return -1;
    }

    int ret = rng_reader_read_bytes(reader, sample->data, len);
    if (ret < 0) {
        free(sample->data);
        sample->data = NULL;
        return -1;
    }

    sample->len        = len;
    sample->timestamp  = time(NULL);
    sample->bytes_read = reader->total_bytes_read;

    return 0;
}

int rng_reader_read_continuous(rng_reader_t *reader, uint64_t total_bytes,
                               int (*callback)(const rng_sample_t *sample, void *user_data),
                               void *user_data)
{
    if (!reader || !reader->initialized || total_bytes == 0 || !callback)
        return -1;

    uint64_t bytes_remaining = total_bytes;

    while (bytes_remaining > 0) {
        size_t chunk = (bytes_remaining > RNG_1MB_SIZE)
                           ? RNG_1MB_SIZE
                           : (size_t)bytes_remaining;

        rng_sample_t sample;
        if (rng_reader_read_sample(reader, &sample, chunk) < 0) {
            fprintf(stderr, "rng_reader_read_continuous: read sample failed\n");
            return -1;
        }

        int cb_ret = callback(&sample, user_data);
        rng_sample_free(&sample);

        if (cb_ret != 0) {
            fprintf(stderr, "rng_reader_read_continuous: callback returned error\n");
            return cb_ret;
        }

        bytes_remaining -= chunk;
    }

    return 0;
}

int rng_reader_fill_buffer(rng_reader_t *reader, size_t len)
{
    if (!reader || !reader->initialized || len == 0)
        return -1;

    if (len > reader->buffer_size) {
        uint8_t *new_buf = (uint8_t *)realloc(reader->buffer, len);
        if (!new_buf) {
            fprintf(stderr, "rng_reader_fill_buffer: realloc failed\n");
            return -1;
        }
        reader->buffer      = new_buf;
        reader->buffer_size = len;
    }

    return rng_reader_read_bytes(reader, reader->buffer, len);
}

void rng_sample_free(rng_sample_t *sample)
{
    if (!sample)
        return;

    if (sample->data) {
        free(sample->data);
        sample->data = NULL;
    }

    sample->len        = 0;
    sample->timestamp  = 0;
    sample->bytes_read = 0;
}

double rng_reader_measure_speed(rng_reader_t *reader, size_t test_size)
{
    if (!reader || !reader->initialized || test_size == 0)
        return 0.0;

    struct timeval start, end;

    if (test_size > reader->buffer_size) {
        uint8_t *new_buf = realloc(reader->buffer, test_size);
        if (!new_buf)
            return 0.0;
        reader->buffer = new_buf;
        reader->buffer_size = test_size;
    }

    gettimeofday(&start, NULL);

    if (spi_read(reader->spi_dev, reader->buffer, test_size) < 0)
        return 0.0;

    gettimeofday(&end, NULL);

    double elapsed = (double)(end.tv_sec - start.tv_sec)
                   + (double)(end.tv_usec - start.tv_usec) / 1000000.0;

    if (elapsed <= 0.001)
        elapsed = 0.001;

    reader->total_bytes_read += test_size;

    return (double)test_size / elapsed;
}

size_t rng_reader_calculate_optimal_sample_size(double speed_bytes_sec)
{
    if (speed_bytes_sec <= 0)
        return RNG_MIN_SAMPLE_SIZE;

    double max_wait_sec = 30.0;
    size_t ideal_size = (size_t)(speed_bytes_sec * max_wait_sec);

    if (ideal_size < RNG_MIN_SAMPLE_SIZE)
        return RNG_MIN_SAMPLE_SIZE;

    if (speed_bytes_sec >= RNG_SPEED_THRESHOLD_HIGH)
        return RNG_1GB_SIZE;
    if (speed_bytes_sec >= RNG_SPEED_THRESHOLD_MEDIUM)
        return RNG_100MB_SIZE;
    if (speed_bytes_sec >= RNG_SPEED_THRESHOLD_LOW)
        return RNG_10MB_SIZE;

    if (ideal_size > RNG_1GB_SIZE)
        return RNG_1GB_SIZE;

    return ideal_size;
}

size_t rng_reader_get_sample_for_speed(double speed_bytes_sec)
{
    if (speed_bytes_sec <= 0)
        return RNG_MIN_SAMPLE_SIZE;

    if (speed_bytes_sec >= RNG_SPEED_THRESHOLD_HIGH)
        return RNG_1GB_SIZE;
    if (speed_bytes_sec >= RNG_SPEED_THRESHOLD_MEDIUM)
        return RNG_100MB_SIZE;
    if (speed_bytes_sec >= RNG_SPEED_THRESHOLD_LOW)
        return RNG_10MB_SIZE;

    return RNG_1MB_SIZE;
}

const char *rng_reader_speed_category(double speed_bytes_sec)
{
    if (speed_bytes_sec <= 0)
        return "Unknown";
    if (speed_bytes_sec < RNG_SPEED_THRESHOLD_LOW)
        return "Low (< 100 KB/s)";
    if (speed_bytes_sec < RNG_SPEED_THRESHOLD_MEDIUM)
        return "Medium (100 KB/s - 1 MB/s)";
    if (speed_bytes_sec < RNG_SPEED_THRESHOLD_HIGH)
        return "High (1 MB/s - 50 MB/s)";
    return "Very High (> 50 MB/s)";
}
