#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <dirent.h>
#include <errno.h>

#include "spi.h"

static int spi_set_mode(int fd, uint8_t mode)
{
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0)
        return -1;
    return 0;
}

static int spi_set_bits(int fd, uint8_t bits)
{
    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0)
        return -1;
    return 0;
}

static int spi_set_speed(int fd, uint32_t speed)
{
    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
        return -1;
    return 0;
}

static int spi_get_mode(int fd, uint8_t *mode)
{
    if (ioctl(fd, SPI_IOC_RD_MODE, mode) < 0)
        return -1;
    return 0;
}

static int spi_get_bits(int fd, uint8_t *bits)
{
    if (ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, bits) < 0)
        return -1;
    return 0;
}

static int spi_get_speed(int fd, uint32_t *speed)
{
    if (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, speed) < 0)
        return -1;
    return 0;
}

int spi_open(spi_device_t *dev, const char *device_path)
{
    if (!dev || !device_path)
        return -1;

    memset(dev, 0, sizeof(*dev));
    strncpy(dev->device_path, device_path, MAX_PATH_LEN - 1);

    dev->fd = open(device_path, O_RDWR);
    if (dev->fd < 0) {
        fprintf(stderr, "spi_open: cannot open %s: %s\n",
                device_path, strerror(errno));
        return -1;
    }

    dev->mode          = DEFAULT_SPI_MODE;
    dev->bits_per_word = DEFAULT_SPI_BITS;
    dev->speed_hz      = DEFAULT_SPI_SPEED;
    dev->delay_usecs   = DEFAULT_SPI_DELAY;

    if (spi_set_mode(dev->fd, dev->mode) < 0) {
        close(dev->fd);
        dev->fd = -1;
        return -1;
    }
    if (spi_set_bits(dev->fd, dev->bits_per_word) < 0) {
        close(dev->fd);
        dev->fd = -1;
        return -1;
    }
    if (spi_set_speed(dev->fd, dev->speed_hz) < 0) {
        close(dev->fd);
        dev->fd = -1;
        return -1;
    }

    return 0;
}

int spi_close(spi_device_t *dev)
{
    if (!dev)
        return -1;

    if (dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
    }

    return 0;
}

int spi_configure(spi_device_t *dev, uint8_t mode, uint8_t bits, uint32_t speed)
{
    if (!dev || dev->fd < 0)
        return -1;

    if (spi_set_mode(dev->fd, mode) < 0)
        return -1;
    if (spi_set_bits(dev->fd, bits) < 0)
        return -1;
    if (spi_set_speed(dev->fd, speed) < 0)
        return -1;

    dev->mode          = mode;
    dev->bits_per_word = bits;
    dev->speed_hz      = speed;

    return 0;
}

int spi_get_config(spi_device_t *dev, uint8_t *mode, uint8_t *bits, uint32_t *speed)
{
    if (!dev || dev->fd < 0)
        return -1;

    if (mode && spi_get_mode(dev->fd, mode) < 0)
        return -1;
    if (bits && spi_get_bits(dev->fd, bits) < 0)
        return -1;
    if (speed && spi_get_speed(dev->fd, speed) < 0)
        return -1;

    return 0;
}

int spi_transfer(spi_device_t *dev, const uint8_t *tx_buf, uint8_t *rx_buf, size_t len)
{
    if (!dev || dev->fd < 0 || !tx_buf || !rx_buf || len == 0)
        return -1;

    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)tx_buf,
        .rx_buf        = (unsigned long)rx_buf,
        .len           = len,
        .delay_usecs   = dev->delay_usecs,
        .speed_hz      = dev->speed_hz,
        .bits_per_word = dev->bits_per_word,
    };

    if (ioctl(dev->fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        fprintf(stderr, "spi_transfer: ioctl failed: %s\n", strerror(errno));
        return -1;
    }

    return (int)len;
}

int spi_read(spi_device_t *dev, uint8_t *buf, size_t len)
{
    if (!dev || dev->fd < 0 || !buf || len == 0)
        return -1;

    uint8_t *tx_buf = calloc(len, sizeof(uint8_t));
    if (!tx_buf)
        return -1;

    int ret = spi_transfer(dev, tx_buf, buf, len);
    free(tx_buf);

    return ret;
}

int spi_write(spi_device_t *dev, const uint8_t *buf, size_t len)
{
    if (!dev || dev->fd < 0 || !buf || len == 0)
        return -1;

    uint8_t *rx_buf = malloc(len);
    if (!rx_buf)
        return -1;

    int ret = spi_transfer(dev, buf, rx_buf, len);
    free(rx_buf);

    return ret;
}

int spi_discover_devices(spi_device_info_t *devices, int max_devices)
{
    if (!devices || max_devices <= 0)
        return 0;

    int count = 0;
    DIR *dir = opendir(SYSFS_SPI_PATH);

    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL && count < max_devices) {
            if (strncmp(entry->d_name, "spidev", 6) == 0) {
                char path[MAX_PATH_LEN];
                snprintf(path, sizeof(path), "/dev/%s", entry->d_name);

                if (spi_is_device_available(path)) {
                    strncpy(devices[count].device_path, path, MAX_PATH_LEN - 1);

                    unsigned int bus, cs;
                    if (sscanf(entry->d_name, "spidev%u.%u", &bus, &cs) == 2) {
                        devices[count].bus_num     = bus;
                        devices[count].chip_select = cs;
                    }
                    count++;
                }
            }
        }
        closedir(dir);
    } else {
        char path[MAX_PATH_LEN];
        for (int bus = 0; bus < MAX_SPI_DEVICES && count < max_devices; bus++) {
            for (int cs = 0; cs < 2 && count < max_devices; cs++) {
                snprintf(path, sizeof(path), "%s%d.%d",
                         SPI_PATH_PREFIX, bus, cs);

                if (spi_is_device_available(path)) {
                    strncpy(devices[count].device_path, path, MAX_PATH_LEN - 1);
                    devices[count].bus_num     = bus;
                    devices[count].chip_select = cs;
                    count++;
                }
            }
        }
    }

    return count;
}

int spi_is_device_available(const char *device_path)
{
    if (!device_path)
        return 0;

    int fd = open(device_path, O_RDWR);
    if (fd < 0)
        return 0;

    close(fd);
    return 1;
}
