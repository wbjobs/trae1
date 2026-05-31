#include "sctp_transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESUME_MAGIC  0x5245534D

typedef struct {
    uint32_t    magic;
    uint8_t     version;
    uint8_t     reserved[3];
    char        filename[MAX_FILENAME_LEN];
    uint64_t    file_size;
    uint32_t    chunk_size;
    uint32_t    total_chunks;
    uint32_t    file_crc32c;
    uint64_t    total_received;
    uint32_t    bitmap_bytes;
} __attribute__((packed)) resume_header_t;

int resume_save_state(const char *resume_file,
                       const file_context_t *ctx)
{
    FILE *fp = fopen(resume_file, "wb");
    if (!fp) {
        perror("fopen resume save");
        return -1;
    }

    resume_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = RESUME_MAGIC;
    hdr.version = 1;
    strncpy(hdr.filename, ctx->filename, MAX_FILENAME_LEN - 1);
    hdr.file_size = ctx->file_size;
    hdr.chunk_size = ctx->chunk_size;
    hdr.total_chunks = ctx->total_chunks;
    hdr.file_crc32c = ctx->file_crc32c;
    hdr.total_received = ctx->total_received;
    hdr.bitmap_bytes = (ctx->total_chunks + 7) / 8;

    if (fwrite(&hdr, sizeof(hdr), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }

    if (ctx->chunk_map && hdr.bitmap_bytes > 0) {
        if (fwrite(ctx->chunk_map, 1, hdr.bitmap_bytes, fp) !=
            hdr.bitmap_bytes) {
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

int resume_load_state(const char *resume_file,
                       file_context_t *ctx)
{
    FILE *fp = fopen(resume_file, "rb");
    if (!fp) {
        return 0;
    }

    resume_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }

    if (hdr.magic != RESUME_MAGIC) {
        fclose(fp);
        return 0;
    }

    if (hdr.file_size != ctx->file_size ||
        hdr.chunk_size != ctx->chunk_size ||
        strcmp(hdr.filename, ctx->filename) != 0) {
        fclose(fp);
        return 0;
    }

    if (hdr.bitmap_bytes > 0 && ctx->chunk_map) {
        if (fread(ctx->chunk_map, 1, hdr.bitmap_bytes, fp) !=
            hdr.bitmap_bytes) {
            fclose(fp);
            return 0;
        }
    }

    ctx->total_received = hdr.total_received;
    ctx->file_crc32c = hdr.file_crc32c;

    fclose(fp);
    return 1;
}
