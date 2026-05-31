#include "bdev_lock.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "spdk/bdev.h"
#include "spdk/bdev_module.h"
#include "spdk/env.h"
#include "spdk/log.h"
#include "spdk/string.h"
#include "spdk/util.h"

static struct nvmeof_lock_manager g_lock_mgr;
static bool g_lock_mgr_inited = false;

struct bdev_lock_bdev {
    struct spdk_bdev bdev;
    struct spdk_bdev *backend_bdev;
    struct spdk_bdev_desc *backend_desc;
    bool enable_barrier;
};

struct bdev_lock_channel {
    struct spdk_bdev_channel *backend_ch;
};

struct bdev_lock_io_ctx {
    struct bdev_lock_bdev *lock_bdev;
    struct spdk_bdev_channel *backend_ch;
    bool write_locked;
    uint64_t locked_lba_start;
    uint64_t locked_lba_count;
    bool need_flush;
};

static int
bdev_lock_get_ctx_size(void)
{
    return sizeof(struct bdev_lock_io_ctx);
}

static int
bdev_lock_channel_create(void *bdev_ctx, struct spdk_io_channel *ch)
{
    struct bdev_lock_bdev *lock_bdev = bdev_ctx;
    struct bdev_lock_channel *lock_ch = spdk_io_channel_get_ctx(ch);

    lock_ch->backend_ch = spdk_bdev_get_io_channel(lock_bdev->backend_desc);
    if (!lock_ch->backend_ch) {
        SPDK_ERRLOG("Failed to get backend io channel\n");
        return -ENOMEM;
    }
    return 0;
}

static void
bdev_lock_channel_destroy(void *bdev_ctx, struct spdk_io_channel *ch)
{
    struct bdev_lock_channel *lock_ch = spdk_io_channel_get_ctx(ch);
    (void)bdev_ctx;

    if (lock_ch->backend_ch)
        spdk_put_io_channel(lock_ch->backend_ch);
}

static int
bdev_lock_submit_request(struct spdk_bdev_io *bio)
{
    struct bdev_lock_bdev *lock_bdev = bio->bdev->ctxt;
    struct bdev_lock_io_ctx *io_ctx = spdk_bdev_io_get_ctx(bio);
    struct spdk_bdev_channel *ch = spdk_bdev_io_get_channel(bio);
    struct bdev_lock_channel *lock_ch = spdk_io_channel_get_ctx(ch);

    io_ctx->lock_bdev = lock_bdev;
    io_ctx->backend_ch = lock_ch->backend_ch;
    io_ctx->write_locked = false;
    io_ctx->need_flush = false;

    struct spdk_bdev *backend = lock_bdev->backend_bdev;
    struct spdk_bdev_desc *desc = lock_bdev->backend_desc;
    uint64_t offset_blocks = bio->u.bdev.offset_blocks;
    uint64_t num_blocks = bio->u.bdev.num_blocks;
    struct iovec *iovs = bio->u.bdev.iovs;
    int iovcnt = bio->u.bdev.iovcnt;

    switch (bio->type) {
    case SPDK_BDEV_IO_TYPE_READ:
        spdk_bdev_readv_blocks(desc, iovs, iovcnt, offset_blocks, num_blocks,
                               spdk_bdev_io_complete, bio);
        return 0;

    case SPDK_BDEV_IO_TYPE_WRITE: {
        nvmeof_lock_owner_t owner = (nvmeof_lock_owner_t)(uintptr_t)lock_ch->backend_ch;
        uint32_t nsid = 0;
        const char *bdev_name = spdk_bdev_get_name(&lock_bdev->bdev);
        if (sscanf(bdev_name, "lock_%*[^_]_%u", &nsid) != 1)
            nsid = 1;

        int rc = nvmeof_lock_acquire(&g_lock_mgr, nsid, owner,
                                     offset_blocks, num_blocks,
                                     NVME_LOCK_FLAG_WRITE |
                                     (lock_bdev->enable_barrier ? NVME_LOCK_FLAG_BARRIER : 0),
                                     0);
        if (rc != 0) {
            SPDK_DEBUGLOG("Lock conflict for write: offset=%lu count=%lu (rc=%d)\n",
                          offset_blocks, num_blocks, rc);
            spdk_bdev_io_complete(bio, SPDK_BDEV_IO_STATUS_FAILED);
            return 0;
        }

        io_ctx->write_locked = true;
        io_ctx->locked_lba_start = offset_blocks;
        io_ctx->locked_lba_count = num_blocks;
        io_ctx->need_flush = lock_bdev->enable_barrier;

        spdk_bdev_writev_blocks(desc, iovs, iovcnt, offset_blocks, num_blocks,
                                spdk_bdev_io_complete, bio);
        return 0;
    }

    case SPDK_BDEV_IO_TYPE_FLUSH:
    case SPDK_BDEV_IO_TYPE_UNMAP:
        spdk_bdev_flush(desc, offset_blocks, num_blocks,
                        spdk_bdev_io_complete, bio);
        return 0;

    default:
        spdk_bdev_io_complete(bio, SPDK_BDEV_IO_STATUS_FAILED);
        return 0;
    }
}

static void
bdev_lock_io_complete(struct spdk_bdev_io *bio, bool success)
{
    struct bdev_lock_io_ctx *io_ctx = spdk_bdev_io_get_ctx(bio);
    struct bdev_lock_bdev *lock_bdev = io_ctx->lock_bdev;
    struct spdk_bdev_channel *ch = spdk_bdev_io_get_channel(bio);
    struct bdev_lock_channel *lock_ch = spdk_io_channel_get_ctx(ch);

    if (bio->type == SPDK_BDEV_IO_TYPE_WRITE && io_ctx->write_locked) {
        if (io_ctx->need_flush && success) {
            struct spdk_bdev_desc *desc = lock_bdev->backend_desc;
            nvmeof_lock_owner_t owner = (nvmeof_lock_owner_t)(uintptr_t)lock_ch->backend_ch;
            uint32_t nsid = 0;
            const char *bdev_name = spdk_bdev_get_name(&lock_bdev->bdev);
            if (sscanf(bdev_name, "lock_%*[^_]_%u", &nsid) != 1)
                nsid = 1;

            spdk_bdev_flush(desc, 0, spdk_bdev_get_num_blocks(lock_bdev->backend_bdev),
                            spdk_bdev_io_complete, bio);

            nvmeof_lock_release(&g_lock_mgr, nsid, owner,
                                io_ctx->locked_lba_start, io_ctx->locked_lba_count);
            io_ctx->write_locked = false;
            io_ctx->need_flush = false;
            return;
        }

        nvmeof_lock_owner_t owner = (nvmeof_lock_owner_t)(uintptr_t)lock_ch->backend_ch;
        uint32_t nsid = 0;
        const char *bdev_name = spdk_bdev_get_name(&lock_bdev->bdev);
        if (sscanf(bdev_name, "lock_%*[^_]_%u", &nsid) != 1)
            nsid = 1;

        nvmeof_lock_release(&g_lock_mgr, nsid, owner,
                            io_ctx->locked_lba_start, io_ctx->locked_lba_count);
        io_ctx->write_locked = false;
    }

    spdk_bdev_io_complete(bio, success ? SPDK_BDEV_IO_STATUS_SUCCESS : SPDK_BDEV_IO_STATUS_FAILED);
}

static void
bdev_lock_write_rest(struct spdk_bdev_io *bio)
{
    bdev_lock_io_complete(bio, true);
}

static int
bdev_lock_destruct(void *bdev_ctx)
{
    struct bdev_lock_bdev *lock_bdev = bdev_ctx;

    if (lock_bdev->backend_desc)
        spdk_bdev_close(lock_bdev->backend_desc);

    spdk_bdev_unregister(&lock_bdev->bdev, NULL, NULL);
    return 0;
}

static bool
bdev_lock_module_supports_extended_buf(const struct spdk_bdev *bdev)
{
    (void)bdev;
    return false;
}

static const struct spdk_bdev_module bdev_lock_if = {
    .name = "lock",
    .module_id = 0,
    .get_ctx_size = bdev_lock_get_ctx_size,
    .examine_config = NULL,
    .config_text = NULL,
    .destruct = bdev_lock_destruct,
    .submit_request = bdev_lock_submit_request,
    .io_complete = bdev_lock_io_complete,
    .write_rest = bdev_lock_write_rest,
    .get_channel_size = NULL,
    .channel_create = bdev_lock_channel_create,
    .channel_destroy = bdev_lock_channel_destroy,
    .module_supports_extended_buf = bdev_lock_module_supports_extended_buf,
};

static void
bdev_lock_on_backend_create(void *cb_arg, int status)
{
    struct bdev_lock_bdev *lock_bdev = cb_arg;
    if (status != 0) {
        SPDK_ERRLOG("Failed to open backend bdev: %d\n", status);
        spdk_bdev_unregister(&lock_bdev->bdev, NULL, NULL);
        return;
    }
    spdk_bdev_module_release_bdev(lock_bdev->backend_bdev);
    SPDK_NOTICELOG("Lock bdev '%s' created on backend '%s'\n",
                   spdk_bdev_get_name(&lock_bdev->bdev),
                   spdk_bdev_get_name(lock_bdev->backend_bdev));
}

int
bdev_lock_create(const char *name, const char *backend_name,
                 bool enable_barrier)
{
    if (!g_lock_mgr_inited) {
        nvmeof_lock_mgr_init(&g_lock_mgr);
        g_lock_mgr_inited = true;
    }

    struct spdk_bdev *backend = spdk_bdev_first();
    while (backend) {
        if (strcmp(spdk_bdev_get_name(backend), backend_name) == 0)
            break;
        backend = spdk_bdev_next(backend);
    }
    if (!backend) {
        SPDK_ERRLOG("Backend bdev '%s' not found\n", backend_name);
        return -ENODEV;
    }

    struct bdev_lock_bdev *lock_bdev = calloc(1, sizeof(*lock_bdev));
    if (!lock_bdev) return -ENOMEM;

    lock_bdev->backend_bdev = backend;
    lock_bdev->enable_barrier = enable_barrier;

    lock_bdev->bdev.name = strdup(name);
    lock_bdev->bdev.product_name = "lock_bdev";
    lock_bdev->bdev.write_cache = backend->write_cache;
    lock_bdev->bdev.atomic_write_size = backend->atomic_write_size;
    lock_bdev->bdev.optimal_io_boundary = backend->optimal_io_boundary;
    lock_bdev->bdev.blocklen = backend->blocklen;
    lock_bdev->bdev.blockcnt = backend->blockcnt;
    lock_bdev->bdev.ctxt = lock_bdev;
    lock_bdev->bdev.finish = NULL;
    lock_bdev->bdev.ctxt_size = sizeof(struct bdev_lock_bdev);

    int rc = spdk_bdev_register(&lock_bdev->bdev);
    if (rc != 0) {
        SPDK_ERRLOG("Failed to register lock bdev '%s': %d\n", name, rc);
        free((void *)lock_bdev->bdev.name);
        free(lock_bdev);
        return rc;
    }

    rc = spdk_bdev_open_ext(backend_name, true, NULL, NULL,
                            &lock_bdev->backend_desc);
    if (rc != 0) {
        SPDK_ERRLOG("Failed to open backend '%s': %d\n", backend_name, rc);
        spdk_bdev_unregister(&lock_bdev->bdev, NULL, NULL);
        return rc;
    }

    uint32_t nsid = 0;
    if (sscanf(name, "lock_%*[^_]_%u", &nsid) != 1)
        nsid = 1;
    nvmeof_lock_table_add(&g_lock_mgr, nsid);

    bdev_lock_on_backend_create(lock_bdev, 0);
    return 0;
}

void
bdev_lock_delete(struct spdk_bdev *bdev)
{
    if (!bdev) return;
    uint32_t nsid = 0;
    if (sscanf(spdk_bdev_get_name(bdev), "lock_%*[^_]_%u", &nsid) == 1)
        nvmeof_lock_table_remove(&g_lock_mgr, nsid);
    spdk_bdev_unregister(bdev, NULL, NULL);
}

struct nvmeof_lock_manager *
bdev_lock_get_mgr(void)
{
    return &g_lock_mgr;
}
