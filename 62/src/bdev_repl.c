#include "bdev_repl.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "spdk/bdev.h"
#include "spdk/bdev_module.h"
#include "spdk/env.h"
#include "spdk/log.h"
#include "spdk/string.h"

struct bdev_repl_bdev {
    struct spdk_bdev bdev;
    struct spdk_bdev *backend_bdev;
    struct spdk_bdev_desc *backend_desc;
    uint32_t local_nsid;
    struct nvmeof_repl_mgr *mgr;
};

struct bdev_repl_channel {
    struct spdk_bdev_channel *backend_ch;
};

struct bdev_repl_io_ctx {
    struct bdev_repl_bdev *repl_bdev;
    struct spdk_bdev_channel *backend_ch;
    bool is_write;
};

static int
bdev_repl_get_ctx_size(void)
{
    return sizeof(struct bdev_repl_io_ctx);
}

static int
bdev_repl_channel_create(void *bdev_ctx, struct spdk_io_channel *ch)
{
    struct bdev_repl_bdev *repl = bdev_ctx;
    struct bdev_repl_channel *rch = spdk_io_channel_get_ctx(ch);
    rch->backend_ch = spdk_bdev_get_io_channel(repl->backend_desc);
    if (!rch->backend_ch) {
        SPDK_ERRLOG("Failed to get backend io channel\n");
        return -ENOMEM;
    }
    return 0;
}

static void
bdev_repl_channel_destroy(void *bdev_ctx, struct spdk_io_channel *ch)
{
    struct bdev_repl_channel *rch = spdk_io_channel_get_ctx(ch);
    (void)bdev_ctx;
    if (rch->backend_ch)
        spdk_put_io_channel(rch->backend_ch);
}

static void
bdev_repl_io_complete(struct spdk_bdev_io *bio, bool success)
{
    struct bdev_repl_io_ctx *io_ctx = spdk_bdev_io_get_ctx(bio);
    struct bdev_repl_bdev *repl = io_ctx->repl_bdev;

    if (io_ctx->is_write && success && repl->mgr) {
        uint64_t mask = 0;
        nvmeof_repl_write(repl->mgr, repl->local_nsid,
                          bio->u.bdev.offset_blocks,
                          bio->u.bdev.num_blocks,
                          bio->u.bdev.iovs ? bio->u.bdev.iovs[0].iov_base : NULL,
                          &mask);
    }

    spdk_bdev_io_complete(bio, success ? SPDK_BDEV_IO_STATUS_SUCCESS
                                       : SPDK_BDEV_IO_STATUS_FAILED);
}

static int
bdev_repl_submit_request(struct spdk_bdev_io *bio)
{
    struct bdev_repl_bdev *repl = bio->bdev->ctxt;
    struct bdev_repl_io_ctx *io_ctx = spdk_bdev_io_get_ctx(bio);
    struct spdk_bdev_channel *ch = spdk_bdev_io_get_channel(bio);
    struct bdev_repl_channel *rch = spdk_io_channel_get_ctx(ch);

    io_ctx->repl_bdev = repl;
    io_ctx->backend_ch = rch->backend_ch;
    io_ctx->is_write = (bio->type == SPDK_BDEV_IO_TYPE_WRITE);

    struct spdk_bdev_desc *desc = repl->backend_desc;
    uint64_t off = bio->u.bdev.offset_blocks;
    uint64_t cnt = bio->u.bdev.num_blocks;
    struct iovec *iovs = bio->u.bdev.iovs;
    int iovcnt = bio->u.bdev.iovcnt;

    switch (bio->type) {
    case SPDK_BDEV_IO_TYPE_READ:
        spdk_bdev_readv_blocks(desc, iovs, iovcnt, off, cnt,
                               spdk_bdev_io_complete, bio);
        return 0;
    case SPDK_BDEV_IO_TYPE_WRITE:
        spdk_bdev_writev_blocks(desc, iovs, iovcnt, off, cnt,
                                bdev_repl_io_complete, bio);
        return 0;
    case SPDK_BDEV_IO_TYPE_FLUSH:
        spdk_bdev_flush(desc, off, cnt, spdk_bdev_io_complete, bio);
        return 0;
    default:
        spdk_bdev_io_complete(bio, SPDK_BDEV_IO_STATUS_FAILED);
        return 0;
    }
}

static void
bdev_repl_write_rest(struct spdk_bdev_io *bio)
{
    bdev_repl_io_complete(bio, true);
}

static int
bdev_repl_destruct(void *bdev_ctx)
{
    struct bdev_repl_bdev *repl = bdev_ctx;
    if (repl->backend_desc)
        spdk_bdev_close(repl->backend_desc);
    spdk_bdev_unregister(&repl->bdev, NULL, NULL);
    return 0;
}

static bool
bdev_repl_supports_extended_buf(const struct spdk_bdev *bdev)
{
    (void)bdev;
    return false;
}

static const struct spdk_bdev_module bdev_repl_if = {
    .name = "repl",
    .module_id = 0,
    .get_ctx_size = bdev_repl_get_ctx_size,
    .destruct = bdev_repl_destruct,
    .submit_request = bdev_repl_submit_request,
    .io_complete = bdev_repl_io_complete,
    .write_rest = bdev_repl_write_rest,
    .channel_create = bdev_repl_channel_create,
    .channel_destroy = bdev_repl_channel_destroy,
    .module_supports_extended_buf = bdev_repl_supports_extended_buf,
};

int
bdev_repl_create(const char *name, const char *backend_name,
                 uint32_t local_nsid,
                 struct nvmeof_repl_mgr *mgr)
{
    struct spdk_bdev *backend = spdk_bdev_first();
    while (backend) {
        if (strcmp(spdk_bdev_get_name(backend), backend_name) == 0) break;
        backend = spdk_bdev_next(backend);
    }
    if (!backend) {
        SPDK_ERRLOG("Backend bdev '%s' not found\n", backend_name);
        return -ENODEV;
    }

    struct bdev_repl_bdev *repl = calloc(1, sizeof(*repl));
    if (!repl) return -ENOMEM;

    repl->backend_bdev = backend;
    repl->local_nsid = local_nsid;
    repl->mgr = mgr;

    repl->bdev.name = strdup(name);
    repl->bdev.product_name = "repl_bdev";
    repl->bdev.write_cache = backend->write_cache;
    repl->bdev.atomic_write_size = backend->atomic_write_size;
    repl->bdev.optimal_io_boundary = backend->optimal_io_boundary;
    repl->bdev.blocklen = backend->blocklen;
    repl->bdev.blockcnt = backend->blockcnt;
    repl->bdev.ctxt = repl;
    repl->bdev.ctxt_size = sizeof(struct bdev_repl_bdev);

    int rc = spdk_bdev_register(&repl->bdev);
    if (rc != 0) {
        SPDK_ERRLOG("Failed to register repl bdev '%s': %d\n", name, rc);
        free((void *)repl->bdev.name);
        free(repl);
        return rc;
    }

    rc = spdk_bdev_open_ext(backend_name, true, NULL, NULL, &repl->backend_desc);
    if (rc != 0) {
        SPDK_ERRLOG("Failed to open backend '%s': %d\n", backend_name, rc);
        spdk_bdev_unregister(&repl->bdev, NULL, NULL);
        return rc;
    }

    SPDK_NOTICELOG("Repl bdev '%s' created on backend '%s' (nsid=%u)\n",
                   name, backend_name, local_nsid);
    return 0;
}

void
bdev_repl_delete(struct spdk_bdev *bdev)
{
    if (bdev)
        spdk_bdev_unregister(bdev, NULL, NULL);
}
