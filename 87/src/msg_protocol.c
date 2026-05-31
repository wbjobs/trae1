#include "sctp_transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static uint32_t g_seq_num = 0;

#define MAX_MSG_SIZE  (sizeof(msg_header_t) + sizeof(chunk_payload_t) + MAX_CHUNK_SIZE)

int msg_send(int sock_fd, uint8_t msg_type, const void *payload,
              size_t payload_len, uint16_t stream, uint32_t ppid)
{
    size_t total_len = sizeof(msg_header_t) + payload_len;
    uint8_t *buf = (uint8_t *)malloc(total_len);
    if (!buf)
        return -1;

    msg_header_t *hdr = (msg_header_t *)buf;
    hdr->magic = htonl(MAGIC_NUMBER);
    hdr->version = PROTOCOL_VERSION;
    hdr->msg_type = msg_type;
    hdr->flags = 0;
    hdr->length = htonl((uint32_t)total_len);
    hdr->seq_num = htonl(g_seq_num++);
    hdr->crc32c = 0;

    if (payload_len > 0 && payload)
        memcpy(buf + sizeof(msg_header_t), payload, payload_len);

    hdr->crc32c = htonl(crc32c_compute(buf, total_len));

    ssize_t sent = sctp_sendmsg(sock_fd, buf, total_len,
                                NULL, 0, htonl(ppid), 0,
                                stream, 0, 0);
    free(buf);

    if (sent < 0) {
        if (errno != EPIPE && errno != ECONNRESET)
            fprintf(stderr, "msg_send failed for type %d: %s\n",
                    msg_type, strerror(errno));
        return -1;
    }

    return 0;
}

int msg_recv(int sock_fd, msg_header_t *out_hdr, void *out_payload,
              size_t max_payload, struct sctp_sndrcvinfo *sinfo)
{
    static uint8_t recv_buf[MAX_MSG_SIZE];
    int msg_flags = 0;

    ssize_t ret = sctp_recvmsg(sock_fd, recv_buf, sizeof(recv_buf),
                               NULL, 0, sinfo, &msg_flags);
    if (ret < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("sctp_recvmsg");
        return -1;
    }

    if ((size_t)ret < sizeof(msg_header_t)) {
        fprintf(stderr, "Message too short: %zd bytes\n", ret);
        return -1;
    }

    msg_header_t *hdr = (msg_header_t *)recv_buf;
    uint32_t magic = ntohl(hdr->magic);
    uint32_t length = ntohl(hdr->length);
    uint32_t seq_num = ntohl(hdr->seq_num);
    uint32_t recv_crc = ntohl(hdr->crc32c);

    if (magic != MAGIC_NUMBER) {
        fprintf(stderr, "Invalid magic: 0x%x\n", magic);
        return -1;
    }

    if (hdr->version != PROTOCOL_VERSION) {
        fprintf(stderr, "Unsupported version: %d\n", hdr->version);
        return -1;
    }

    if (length != (uint32_t)ret) {
        fprintf(stderr, "Length mismatch: header=%u actual=%zd\n",
                length, ret);
        return -1;
    }

    uint32_t saved_crc = hdr->crc32c;
    hdr->crc32c = 0;
    uint32_t calc_crc = ~crc32c_compute(recv_buf, (size_t)ret);
    hdr->crc32c = saved_crc;

    if (calc_crc != recv_crc) {
        fprintf(stderr, "CRC32C mismatch: calc=0x%08x recv=0x%08x\n",
                calc_crc, recv_crc);
        return -1;
    }

    out_hdr->magic = magic;
    out_hdr->version = hdr->version;
    out_hdr->msg_type = hdr->msg_type;
    out_hdr->flags = hdr->flags;
    out_hdr->length = length;
    out_hdr->seq_num = seq_num;
    out_hdr->crc32c = recv_crc;

    size_t payload_len = length - sizeof(msg_header_t);
    if (payload_len > 0) {
        if (payload_len > max_payload) {
            fprintf(stderr, "Payload too large: %zu > %zu\n",
                    payload_len, max_payload);
            return -1;
        }
        memcpy(out_payload, recv_buf + sizeof(msg_header_t), payload_len);
    }

    return (int)payload_len;
}

int msg_send_file_meta(int sock_fd, const file_context_t *ctx,
                        uint16_t stream)
{
    file_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    strncpy(meta.filename, ctx->filename, MAX_FILENAME_LEN - 1);
    meta.file_size = ctx->file_size;
    meta.chunk_size = ctx->chunk_size;
    meta.total_chunks = ctx->total_chunks;
    meta.crc32c = 0;

    meta.crc32c = crc32c_compute(&meta, sizeof(meta) - sizeof(meta.crc32c));

    return msg_send(sock_fd, MSG_FILE_META, &meta, sizeof(meta), stream, 0);
}

int msg_recv_file_meta(int sock_fd, file_context_t *ctx,
                        struct sctp_sndrcvinfo *sinfo)
{
    msg_header_t hdr;
    file_meta_t meta;
    int ret = msg_recv(sock_fd, &hdr, &meta, sizeof(meta), sinfo);
    if (ret < 0)
        return -1;

    if (hdr.msg_type != MSG_FILE_META)
        return -1;

    if (meta.crc32c != 0) {
        uint32_t calc = crc32c_compute(&meta,
                       sizeof(meta) - sizeof(meta.crc32c));
        if (calc != meta.crc32c) {
            fprintf(stderr, "Meta CRC mismatch\n");
            return -1;
        }
    }

    strncpy(ctx->filename, meta.filename, MAX_FILENAME_LEN - 1);
    ctx->file_size = meta.file_size;
    ctx->chunk_size = meta.chunk_size;
    ctx->total_chunks = meta.total_chunks;
    ctx->file_crc32c = meta.crc32c;

    return 0;
}

int msg_send_chunk(int sock_fd, uint32_t chunk_id, const void *data,
                    size_t len, uint16_t stream)
{
    size_t payload_len = sizeof(chunk_payload_t) + len;
    chunk_payload_t *payload = (chunk_payload_t *)malloc(payload_len);
    if (!payload)
        return -1;

    payload->chunk_id = htonl(chunk_id);
    payload->offset = 0;
    payload->data_len = htonl((uint32_t)len);
    memcpy(payload->data, data, len);

    int ret = msg_send(sock_fd, MSG_CHUNK, payload, payload_len, stream, 0);
    free(payload);
    return ret;
}

int msg_recv_chunk(int sock_fd, uint32_t *chunk_id, void *data,
                    size_t max_len, struct sctp_sndrcvinfo *sinfo)
{
    msg_header_t hdr;
    static uint8_t chunk_buf[sizeof(chunk_payload_t) + MAX_CHUNK_SIZE];
    int ret = msg_recv(sock_fd, &hdr, chunk_buf, sizeof(chunk_buf), sinfo);
    if (ret < 0)
        return -1;

    if (hdr.msg_type != MSG_CHUNK)
        return -1;

    chunk_payload_t *payload = (chunk_payload_t *)chunk_buf;
    *chunk_id = ntohl(payload->chunk_id);
    size_t data_len = ntohl(payload->data_len);

    if (data_len > max_len) {
        fprintf(stderr, "Chunk data too large: %zu\n", data_len);
        return -1;
    }

    memcpy(data, payload->data, data_len);
    return (int)data_len;
}

int msg_send_nack(int sock_fd, const uint32_t *missing_ids,
                   uint32_t count, uint16_t stream)
{
    if (!missing_ids || count == 0)
        return -1;

    size_t payload_len = sizeof(nack_payload_t) + count * sizeof(uint32_t);
    nack_payload_t *payload = (nack_payload_t *)malloc(payload_len);
    if (!payload)
        return -1;

    uint32_t min_id = missing_ids[0];
    uint32_t max_id = missing_ids[0];
    for (uint32_t i = 1; i < count; i++) {
        if (missing_ids[i] < min_id) min_id = missing_ids[i];
        if (missing_ids[i] > max_id) max_id = missing_ids[i];
    }

    payload->start_chunk = htonl(min_id);
    payload->end_chunk = htonl(max_id);
    payload->missing_count = htonl(count);
    for (uint32_t i = 0; i < count; i++)
        payload->missing_ids[i] = htonl(missing_ids[i]);

    int ret = msg_send(sock_fd, MSG_NACK, payload, payload_len, stream, 0);
    free(payload);
    return ret;
}
