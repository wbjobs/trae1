#ifndef SPI_H
#define SPI_H

#include <stdint.h>
#include <stddef.h>
#include "config.h"

typedef struct {
    int      fd;
    char     device_path[MAX_PATH_LEN];
    uint8_t  mode;
    uint8_t  bits_per_word;
    uint32_t speed_hz;
    uint16_t delay_usecs;
} spi_device_t;

typedef struct {
    char     device_path[MAX_PATH_LEN];
    uint32_t bus_num;
    uint32_t chip_select;
} spi_device_info_t;

int spi_open(spi_device_t *dev, const char *device_path);
int spi_close(spi_device_t *dev);

int spi_configure(spi_device_t *dev, uint8_t mode, uint8_t bits, uint32_t speed);
int spi_get_config(spi_device_t *dev, uint8_t *mode, uint8_t *bits, uint32_t *speed);

int spi_transfer(spi_device_t *dev, const uint8_t *tx_buf, uint8_t *rx_buf, size_t len);
int spi_read(spi_device_t *dev, uint8_t *buf, size_t len);
int spi_write(spi_device_t *dev, const uint8_t *buf, size_t len);

int spi_discover_devices(spi_device_info_t *devices, int max_devices);
int spi_is_device_available(const char *device_path);

#endif
