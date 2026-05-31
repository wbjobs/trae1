#ifndef BDEV_LOCK_H
#define BDEV_LOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "spdk/bdev_module.h"
#include "nvmeof_lock.h"

int  bdev_lock_create(const char *name, const char *backend_name,
                      bool enable_barrier);
void bdev_lock_delete(struct spdk_bdev *bdev);

struct nvmeof_lock_manager *bdev_lock_get_mgr(void);

#endif
