#ifndef NVME_RPC_H
#define NVME_RPC_H

#include <spdk/jsonrpc.h>

struct nvmeof_repl_mgr;

void nvmeof_rpc_register(void);
struct nvmeof_repl_mgr *nvmeof_target_get_repl_mgr(void);

#endif
