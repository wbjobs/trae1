#ifndef RNG_READER_H
#define RNG_READER_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#include "config.h"
#include "spi.h"

typedef struct {
    spi_device_t   *spi_dev;
    uint8_t        *buffer;
    size_t          buffer_size;
    uint64_t        total_bytes_read;
    int             initialized;
    time_t          start_time;
} rng_reader_t;

typedef struct {
    uint8_t *data;
    size_t   len;
    time_t   timestamp;
    uint64_t bytes_read;
} rng_sample_t;

int  rng_reader_init(rng_reader_t *reader, spi_device_t *spi);
void rng_reader_cleanup(rng_reader_t *reader);

int  rng_reader_read_bytes(rng_reader_t *reader, uint8_t *buf, size_t len);
int  rng_reader_read_sample(rng_reader_t *reader, rng_sample_t *sample, size_t len);
int  rng_reader_read_continuous(rng_reader_t *reader, uint64_t total_bytes,
                                int (*callback)(const rng_sample_t *sample, void *user_data),
                                void *user_data);

int  rng_reader_fill_buffer(rng_reader_t *reader, size_t len);
void rng_sample_free(rng_sample_t *sample);

double rng_reader_measure_speed(rng_reader_t *reader, size_t test_size);
size_t rng_reader_calculate_optimal_sample_size(double speed_bytes_sec);
size_t rng_reader_get_sample_for_speed(double speed_bytes_sec);
const char *rng_reader_speed_category(double speed_bytes_sec);

#endif
