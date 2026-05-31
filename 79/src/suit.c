#include "suit.h"
#include "sign.h"
#include "logger.h"
#include "firmware.h"
#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int suit_init(void)
{
    return sign_init();
}

void suit_cleanup(void)
{
    sign_cleanup();
}

static void compute_image_digest(const uint8_t *firmware, size_t fw_size,
                                 uint8_t digest[32])
{
    uint32_t crc = firmware_crc32(firmware, fw_size);
    memset(digest, 0, 32);
    digest[0] = (uint8_t)(crc & 0xFF);
    digest[1] = (uint8_t)((crc >> 8) & 0xFF);
    digest[2] = (uint8_t)((crc >> 16) & 0xFF);
    digest[3] = (uint8_t)((crc >> 24) & 0xFF);

    for (size_t i = 4; i < 32 && i < fw_size; i++) {
        digest[i] = firmware[i];
    }
}

int suit_create_manifest(suit_manifest_t *manifest,
                         const uint8_t *firmware, size_t fw_size,
                         const uint8_t *public_key, const uint8_t *secret_key,
                         uint16_t vendor_id, uint16_t class_id,
                         uint64_t expiration)
{
    if (!manifest || !firmware || !public_key || !secret_key) {
        return SUIT_ERR_GENERIC;
    }

    memset(manifest, 0, sizeof(suit_manifest_t));

    manifest->magic = SUIT_MAGIC;
    manifest->manifest_version = SUIT_VERSION;
    manifest->manifest_size = sizeof(suit_manifest_t);
    manifest->algorithm_id = SUIT_ALG_ED25519;
    manifest->firmware_size = (uint32_t)fw_size;
    manifest->firmware_crc32 = firmware_crc32(firmware, fw_size);
    manifest->timestamp = (uint64_t)time(NULL);
    manifest->expiration = expiration;
    manifest->vendor_id = vendor_id;
    manifest->class_id = class_id;

    memcpy(manifest->public_key, public_key, 32);

    compute_image_digest(firmware, fw_size, manifest->image_digest);

    manifest->commands[0] = SUIT_CMD_SET_PARAMS;
    manifest->commands[1] = SUIT_CMD_FETCH;
    manifest->commands[2] = SUIT_CMD_COPY;
    manifest->commands[3] = SUIT_CMD_RUN;
    manifest->commands_len = 4;

    uint8_t data_to_sign[1024];
    size_t data_len = 0;

    memcpy(data_to_sign, &manifest->manifest_version, sizeof(manifest->manifest_version));
    data_len += sizeof(manifest->manifest_version);
    memcpy(data_to_sign + data_len, &manifest->algorithm_id, sizeof(manifest->algorithm_id));
    data_len += sizeof(manifest->algorithm_id);
    memcpy(data_to_sign + data_len, &manifest->firmware_size, sizeof(manifest->firmware_size));
    data_len += sizeof(manifest->firmware_size);
    memcpy(data_to_sign + data_len, &manifest->firmware_crc32, sizeof(manifest->firmware_crc32));
    data_len += sizeof(manifest->firmware_crc32);
    memcpy(data_to_sign + data_len, &manifest->timestamp, sizeof(manifest->timestamp));
    data_len += sizeof(manifest->timestamp);
    memcpy(data_to_sign + data_len, &manifest->expiration, sizeof(manifest->expiration));
    data_len += sizeof(manifest->expiration);
    memcpy(data_to_sign + data_len, &manifest->vendor_id, sizeof(manifest->vendor_id));
    data_len += sizeof(manifest->vendor_id);
    memcpy(data_to_sign + data_len, &manifest->class_id, sizeof(manifest->class_id));
    data_len += sizeof(manifest->class_id);
    memcpy(data_to_sign + data_len, manifest->image_digest, sizeof(manifest->image_digest));
    data_len += sizeof(manifest->image_digest);
    memcpy(data_to_sign + data_len, manifest->public_key, sizeof(manifest->public_key));
    data_len += sizeof(manifest->public_key);

    int ret = ed25519_sign(data_to_sign, data_len, secret_key, manifest->signature);
    if (ret != SIGN_OK) {
        LOG_ERROR("Failed to sign manifest: %s", sign_strerror(ret));
        return SUIT_ERR_SIGNATURE;
    }

    manifest->payload_crc32 = firmware_crc32(
        (const uint8_t *)manifest + sizeof(manifest->magic),
        sizeof(suit_manifest_t) - sizeof(manifest->magic) - sizeof(manifest->payload_crc32)
    );

    return SUIT_OK;
}

int suit_verify_manifest(const suit_manifest_t *manifest,
                         const uint8_t *firmware, size_t fw_size,
                         const suit_vendor_t *trusted_vendors, int vendor_count)
{
    if (!manifest || !firmware) {
        return SUIT_ERR_GENERIC;
    }

    if (manifest->magic != SUIT_MAGIC) {
        LOG_ERROR("Invalid SUIT manifest magic");
        return SUIT_ERR_FORMAT;
    }

    if (manifest->manifest_version != SUIT_VERSION) {
        LOG_ERROR("Unsupported SUIT manifest version: %d", manifest->manifest_version);
        return SUIT_ERR_VERSION;
    }

    if (manifest->algorithm_id != SUIT_ALG_ED25519) {
        LOG_ERROR("Unsupported algorithm: %d", manifest->algorithm_id);
        return SUIT_ERR_ALGORITHM;
    }

    time_t now = time(NULL);
    if (manifest->expiration > 0 && (uint64_t)now > manifest->expiration) {
        LOG_ERROR("Firmware manifest expired at %llu",
                  (unsigned long long)manifest->expiration);
        return SUIT_ERR_EXPIRED;
    }

    int vendor_authorized = 0;
    if (trusted_vendors && vendor_count > 0) {
        for (int i = 0; i < vendor_count; i++) {
            if (trusted_vendors[i].vendor_id == manifest->vendor_id) {
                if (!trusted_vendors[i].authorized) {
                    LOG_ERROR("Vendor %04X not authorized", manifest->vendor_id);
                    return SUIT_ERR_UNAUTHORIZED;
                }
                if (trusted_vendors[i].valid_until > 0 &&
                    now > trusted_vendors[i].valid_until) {
                    LOG_ERROR("Vendor certificate expired");
                    return SUIT_ERR_UNAUTHORIZED;
                }
                if (trusted_vendors[i].valid_from > 0 &&
                    now < trusted_vendors[i].valid_from) {
                    LOG_ERROR("Vendor certificate not yet valid");
                    return SUIT_ERR_UNAUTHORIZED;
                }
                vendor_authorized = 1;
                break;
            }
        }
        if (!vendor_authorized) {
            LOG_ERROR("Unknown vendor: %04X", manifest->vendor_id);
            return SUIT_ERR_UNAUTHORIZED;
        }
    }

    if (manifest->firmware_size != fw_size) {
        LOG_ERROR("Firmware size mismatch: manifest=%u actual=%zu",
                  manifest->firmware_size, fw_size);
        return SUIT_ERR_FORMAT;
    }

    uint32_t actual_crc = firmware_crc32(firmware, fw_size);
    if (manifest->firmware_crc32 != actual_crc) {
        LOG_ERROR("Firmware CRC mismatch: manifest=%08X actual=%08X",
                  manifest->firmware_crc32, actual_crc);
        return SUIT_ERR_FORMAT;
    }

    uint8_t expected_digest[32];
    compute_image_digest(firmware, fw_size, expected_digest);
    if (memcmp(manifest->image_digest, expected_digest, 32) != 0) {
        LOG_ERROR("Image digest mismatch");
        return SUIT_ERR_FORMAT;
    }

    uint8_t data_to_sign[1024];
    size_t data_len = 0;

    memcpy(data_to_sign, &manifest->manifest_version, sizeof(manifest->manifest_version));
    data_len += sizeof(manifest->manifest_version);
    memcpy(data_to_sign + data_len, &manifest->algorithm_id, sizeof(manifest->algorithm_id));
    data_len += sizeof(manifest->algorithm_id);
    memcpy(data_to_sign + data_len, &manifest->firmware_size, sizeof(manifest->firmware_size));
    data_len += sizeof(manifest->firmware_size);
    memcpy(data_to_sign + data_len, &manifest->firmware_crc32, sizeof(manifest->firmware_crc32));
    data_len += sizeof(manifest->firmware_crc32);
    memcpy(data_to_sign + data_len, &manifest->timestamp, sizeof(manifest->timestamp));
    data_len += sizeof(manifest->timestamp);
    memcpy(data_to_sign + data_len, &manifest->expiration, sizeof(manifest->expiration));
    data_len += sizeof(manifest->expiration);
    memcpy(data_to_sign + data_len, &manifest->vendor_id, sizeof(manifest->vendor_id));
    data_len += sizeof(manifest->vendor_id);
    memcpy(data_to_sign + data_len, &manifest->class_id, sizeof(manifest->class_id));
    data_len += sizeof(manifest->class_id);
    memcpy(data_to_sign + data_len, manifest->image_digest, sizeof(manifest->image_digest));
    data_len += sizeof(manifest->image_digest);
    memcpy(data_to_sign + data_len, manifest->public_key, sizeof(manifest->public_key));
    data_len += sizeof(manifest->public_key);

    int ret = ed25519_verify(data_to_sign, data_len,
                             manifest->signature, manifest->public_key);
    if (ret != SIGN_OK) {
        LOG_ERROR("Signature verification failed: %s", sign_strerror(ret));
        if (ret == SIGN_ERR_SIGNATURE_MISMATCH) {
            return SUIT_ERR_SIGNATURE;
        }
        return SUIT_ERR_GENERIC;
    }

    LOG_INFO("SUIT manifest verified successfully");
    LOG_INFO("  Vendor: %04X", manifest->vendor_id);
    LOG_INFO("  Class: %04X", manifest->class_id);
    LOG_INFO("  Timestamp: %llu", (unsigned long long)manifest->timestamp);
    if (manifest->expiration > 0) {
        LOG_INFO("  Expiration: %llu", (unsigned long long)manifest->expiration);
    }

    return SUIT_OK;
}

int suit_save_manifest(const suit_manifest_t *manifest, const char *path)
{
    if (!manifest || !path) return SUIT_ERR_GENERIC;

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        LOG_ERROR("Cannot create manifest file: %s", path);
        return SUIT_ERR_IO;
    }

    size_t written = fwrite(manifest, 1, sizeof(suit_manifest_t), fp);
    fclose(fp);

    if (written != sizeof(suit_manifest_t)) {
        LOG_ERROR("Failed to write manifest");
        return SUIT_ERR_IO;
    }

    LOG_INFO("SUIT manifest saved: %s", path);
    return SUIT_OK;
}

int suit_load_manifest(suit_manifest_t *manifest, const char *path)
{
    if (!manifest || !path) return SUIT_ERR_GENERIC;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        LOG_ERROR("Cannot open manifest file: %s", path);
        return SUIT_ERR_IO;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size != (long)sizeof(suit_manifest_t)) {
        LOG_ERROR("Invalid manifest size: %ld (expected %zu)",
                  size, sizeof(suit_manifest_t));
        fclose(fp);
        return SUIT_ERR_FORMAT;
    }

    size_t read = fread(manifest, 1, sizeof(suit_manifest_t), fp);
    fclose(fp);

    if (read != sizeof(suit_manifest_t)) {
        LOG_ERROR("Failed to read manifest");
        return SUIT_ERR_IO;
    }

    return SUIT_OK;
}

int suit_add_vendor(suit_vendor_t **vendors, int *count, int *capacity,
                    const char *name, uint16_t vendor_id,
                    const uint8_t *public_key,
                    time_t valid_from, time_t valid_until)
{
    (void)public_key;

    if (!vendors || !count || !capacity) return SUIT_ERR_GENERIC;

    if (*count >= *capacity) {
        int new_cap = *capacity * 2 + 4;
        suit_vendor_t *new_vendors = (suit_vendor_t *)realloc(
            *vendors, (size_t)new_cap * sizeof(suit_vendor_t));
        if (!new_vendors) return SUIT_ERR_MEMORY;
        *vendors = new_vendors;
        *capacity = new_cap;
    }

    suit_vendor_t *v = &(*vendors)[*count];
    memset(v, 0, sizeof(suit_vendor_t));

    if (name) strncpy(v->vendor_name, name, sizeof(v->vendor_name) - 1);
    v->vendor_id = vendor_id;
    v->authorized = 1;
    v->valid_from = valid_from;
    v->valid_until = valid_until;

    (*count)++;
    return SUIT_OK;
}

const char *suit_strerror(int err)
{
    switch (err) {
        case SUIT_OK:                 return "Success";
        case SUIT_ERR_GENERIC:        return "Generic error";
        case SUIT_ERR_FORMAT:         return "Invalid format";
        case SUIT_ERR_VERSION:        return "Unsupported version";
        case SUIT_ERR_ALGORITHM:      return "Unsupported algorithm";
        case SUIT_ERR_SIGNATURE:      return "Signature mismatch";
        case SUIT_ERR_EXPIRED:        return "Certificate expired";
        case SUIT_ERR_UNAUTHORIZED:   return "Unauthorized vendor";
        case SUIT_ERR_IO:             return "I/O error";
        case SUIT_ERR_MEMORY:         return "Memory allocation error";
        default:                       return "Unknown error";
    }
}
