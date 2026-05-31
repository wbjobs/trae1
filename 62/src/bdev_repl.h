#ifndef BDEV_REPL_H
#define BDEV_REPL_H

#include <stdbool.h>
#include <stdint.h>

#include "nvmeof_repl.h"

int  bdev_repl_create(const char *name, const char *backend_name,
                      uint32_t local_nsid,
                      struct nvmeof_repl_mgr *mgr);
void bdev_repl_delete(struct spdk_bdev *bdev);

#endif
