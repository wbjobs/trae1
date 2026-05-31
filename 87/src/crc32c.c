#include "sctp_transfer.h"
#include <stdlib.h>
#include <string.h>

static uint32_t crc32c_table[256];
static bool table_initialized = false;

static void crc32c_generate_table(void)
{
    const uint32_t poly = 0x82F63B78;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
        crc32c_table[i] = crc;
    }
    table_initialized = true;
}

int crc32c_init(void)
{
    if (!table_initialized)
        crc32c_generate_table();
    return 0;
}

uint32_t crc32c_compute(const void *data, size_t length)
{
    if (!table_initialized)
        crc32c_generate_table();

    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;

    while (length--) {
        crc = (crc >> 8) ^ crc32c_table[(crc ^ *p++) & 0xFF];
    }

    return ~crc;
}

uint32_t crc32c_update(uint32_t crc, const void *data, size_t length)
{
    if (!table_initialized)
        crc32c_generate_table();

    const uint8_t *p = (const uint8_t *)data;

    while (length--) {
        crc = (crc >> 8) ^ crc32c_table[(crc ^ *p++) & 0xFF];
    }

    return crc;
}
