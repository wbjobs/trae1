#ifndef FIRMWARE_H
#define FIRMWARE_H

#include <stdint.h>
#include <stddef.h>

#define MAX_FIRMWARE_SIZE (16 * 1024 * 1024)

typedef struct {
    uint8_t *data;
    size_t   size;
    uint32_t base_addr;
} firmware_t;

firmware_t *firmware_load(const char *path);
void        firmware_free(firmware_t *fw);
int         firmware_save(const char *path, const uint8_t *data, size_t size);
uint32_t    firmware_crc32(const uint8_t *data, size_t size);

#endif
