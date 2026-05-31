#include "sctp_transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

static uint64_t get_file_size(const char *filename)
{
    struct stat st;
    if (stat(filename, &st) < 0)
        return 0;
    return (uint64_t)st.st_size;
}

static const char *get_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (slash)
        return slash + 1;
    slash = strrchr(path, '\\');
    if (slash)
        return slash + 1;
    return path;
}

int file_ctx_open_send(file_context_t *ctx, const char *filename)
{
    memset(ctx, 0, sizeof(*ctx));

    ctx->file_size = get_file_size(filename);
    if (ctx->file_size == 0) {
        fprintf(stderr, "File is empty or not found: %s\n", filename);
        return -1;
    }

    ctx->chunk_size = MAX_CHUNK_SIZE;
    if (ctx->file_size < (uint64_t)ctx->chunk_size) {
        ctx->chunk_size = (uint32_t)ctx->file_size;
        if (ctx->chunk_size < MIN_CHUNK_SIZE)
            ctx->chunk_size = MIN_CHUNK_SIZE > ctx->file_size ?
                              (uint32_t)ctx->file_size : MIN_CHUNK_SIZE;
    }

    ctx->total_chunks = (ctx->file_size + ctx->chunk_size - 1) / ctx->chunk_size;

    strncpy(ctx->filename, get_basename(filename), MAX_FILENAME_LEN - 1);

    ctx->fd = open(filename, O_RDONLY);
    if (ctx->fd < 0) {
        perror("open for read");
        return -1;
    }

    uint32_t bitmap_size = (ctx->total_chunks + 31) / 32;
    ctx->chunk_map = (uint8_t *)calloc(bitmap_size, sizeof(uint8_t));
    if (!ctx->chunk_map) {
        close(ctx->fd);
        return -1;
    }

    ctx->next_chunk_to_send = 0;
    ctx->total_sent = 0;
    ctx->transfer_complete = false;
    ctx->transfer_failed = false;
    pthread_mutex_init(&ctx->file_lock, NULL);

    return 0;
}

int file_ctx_open_recv(file_context_t *ctx, const char *filename,
                        uint64_t file_size, uint32_t chunk_size)
{
    memset(ctx, 0, sizeof(*ctx));

    ctx->file_size = file_size;
    ctx->chunk_size = chunk_size;
    ctx->total_chunks = (file_size + chunk_size - 1) / chunk_size;
    strncpy(ctx->filename, filename, MAX_FILENAME_LEN - 1);

    ctx->fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (ctx->fd < 0) {
        perror("open for write");
        return -1;
    }

    if (ftruncate(ctx->fd, (off_t)file_size) < 0) {
        perror("ftruncate");
        close(ctx->fd);
        return -1;
    }

    uint32_t bitmap_size = (ctx->total_chunks + 31) / 32;
    ctx->chunk_map = (uint8_t *)calloc(bitmap_size, sizeof(uint8_t));
    if (!ctx->chunk_map) {
        close(ctx->fd);
        return -1;
    }

    ctx->next_chunk_to_recv = 0;
    ctx->total_received = 0;
    ctx->transfer_complete = false;
    ctx->transfer_failed = false;
    pthread_mutex_init(&ctx->file_lock, NULL);

    return 0;
}

int file_ctx_read_chunk(file_context_t *ctx, uint32_t chunk_id,
                         void *buf, size_t *len)
{
    if (chunk_id >= ctx->total_chunks)
        return -1;

    uint64_t offset = (uint64_t)chunk_id * ctx->chunk_size;
    size_t to_read = ctx->chunk_size;

    if (offset + to_read > ctx->file_size)
        to_read = (size_t)(ctx->file_size - offset);

    pthread_mutex_lock(&ctx->file_lock);
    ssize_t bytes = pread(ctx->fd, buf, to_read, (off_t)offset);
    pthread_mutex_unlock(&ctx->file_lock);

    if (bytes < 0) {
        perror("pread");
        return -1;
    }

    *len = (size_t)bytes;
    return 0;
}

int file_ctx_write_chunk(file_context_t *ctx, uint32_t chunk_id,
                          const void *buf, size_t len)
{
    if (chunk_id >= ctx->total_chunks)
        return -1;

    uint64_t offset = (uint64_t)chunk_id * ctx->chunk_size;

    pthread_mutex_lock(&ctx->file_lock);
    ssize_t bytes = pwrite(ctx->fd, buf, len, (off_t)offset);
    pthread_mutex_unlock(&ctx->file_lock);

    if (bytes < 0) {
        perror("pwrite");
        return -1;
    }

    uint32_t byte_idx = chunk_id / 8;
    uint8_t bit_mask = (uint8_t)(1 << (chunk_id % 8));
    ctx->chunk_map[byte_idx] |= bit_mask;

    pthread_mutex_lock(&ctx->file_lock);
    ctx->total_received += bytes;
    pthread_mutex_unlock(&ctx->file_lock);

    return 0;
}

int file_ctx_compute_crc(file_context_t *ctx, uint32_t *out_crc)
{
    if (lseek(ctx->fd, 0, SEEK_SET) < 0) {
        perror("lseek");
        return -1;
    }

    uint32_t crc = 0xFFFFFFFF;
    uint8_t buf[256 * 1024];
    ssize_t bytes;

    while ((bytes = read(ctx->fd, buf, sizeof(buf))) > 0) {
        crc = crc32c_update(crc, buf, (size_t)bytes);
    }

    if (bytes < 0) {
        perror("read");
        return -1;
    }

    *out_crc = ~crc;
    return 0;
}

void file_ctx_close(file_context_t *ctx)
{
    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
    if (ctx->chunk_map) {
        free(ctx->chunk_map);
        ctx->chunk_map = NULL;
    }
    pthread_mutex_destroy(&ctx->file_lock);
}

bool file_ctx_all_chunks_received(file_context_t *ctx)
{
    uint32_t full_bytes = ctx->total_chunks / 8;
    uint32_t remainder = ctx->total_chunks % 8;

    for (uint32_t i = 0; i < full_bytes; i++) {
        if (ctx->chunk_map[i] != 0xFF)
            return false;
    }

    if (remainder > 0) {
        uint8_t mask = (uint8_t)((1 << remainder) - 1);
        if ((ctx->chunk_map[full_bytes] & mask) != mask)
            return false;
    }

    return true;
}

bool file_ctx_chunk_received(file_context_t *ctx, uint32_t chunk_id)
{
    if (!ctx || !ctx->chunk_map || chunk_id >= ctx->total_chunks)
        return false;
    uint32_t byte_idx = chunk_id / 8;
    uint8_t bit_mask = (uint8_t)(1 << (chunk_id % 8));
    return (ctx->chunk_map[byte_idx] & bit_mask) != 0;
}
