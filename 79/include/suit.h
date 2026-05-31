#ifndef SUIT_H
#define SUIT_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#define SUIT_MAGIC              0xD8DCD28B
#define SUIT_VERSION           1
#define SUIT_MANIFEST_MAX_SIZE  4096

#define SUIT_ALG_ED25519       1
#define SUIT_ALG_SHA256        2

#define SUIT_CMD_SET_DEPS       1
#define SUIT_CMD_SET_PARAMS     2
#define SUIT_CMD_FETCH          3
#define SUIT_CMD_COPY           4
#define SUIT_CMD_RUN            5
#define SUIT_CMD_SWAP           6

#define SUIT_OK                 0
#define SUIT_ERR_GENERIC       -1
#define SUIT_ERR_FORMAT        -2
#define SUIT_ERR_VERSION       -3
#define SUIT_ERR_ALGORITHM     -4
#define SUIT_ERR_SIGNATURE     -5
#define SUIT_ERR_EXPIRED       -6
#define SUIT_ERR_UNAUTHORIZED  -7
#define SUIT_ERR_IO            -8
#define SUIT_ERR_MEMORY        -9

typedef struct {
    uint32_t    magic;
    uint16_t    manifest_version;
    uint16_t    manifest_size;
    uint8_t     algorithm_id;
    uint8_t     reserved[3];
    uint8_t     signature[64];
    uint8_t     public_key[32];
    uint32_t    firmware_size;
    uint32_t    firmware_crc32;
    uint64_t    timestamp;
    uint64_t    expiration;
    uint16_t    vendor_id;
    uint16_t    class_id;
    uint8_t     image_digest[32];
    uint8_t     commands[256];
    uint16_t    commands_len;
    uint32_t    payload_crc32;
} __attribute__((packed)) suit_manifest_t;

typedef struct {
    uint8_t  key_id[16];
    char     vendor_name[64];
    uint16_t vendor_id;
    uint8_t  authorized;
    time_t   valid_from;
    time_t   valid_until;
} suit_vendor_t;

int  suit_init(void);
void suit_cleanup(void);

int  suit_create_manifest(suit_manifest_t *manifest,
                          const uint8_t *firmware, size_t fw_size,
                          const uint8_t *public_key, const uint8_t *secret_key,
                          uint16_t vendor_id, uint16_t class_id,
                          uint64_t expiration);
int  suit_verify_manifest(const suit_manifest_t *manifest,
                          const uint8_t *firmware, size_t fw_size,
                          const suit_vendor_t *trusted_vendors, int vendor_count);

int  suit_save_manifest(const suit_manifest_t *manifest, const char *path);
int  suit_load_manifest(suit_manifest_t *manifest, const char *path);

int  suit_add_vendor(suit_vendor_t **vendors, int *count, int *capacity,
                     const char *name, uint16_t vendor_id,
                     const uint8_t *public_key,
                     time_t valid_from, time_t valid_until);

const char *suit_strerror(int err);

#endif
