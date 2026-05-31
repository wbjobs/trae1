#pragma once

#include "rdma_context.h"
#include "common/types.h"
#include <string>
#include <memory>
#include <mutex>

namespace dmp {

struct RdmaEndpointInfo {
    uint32_t qpn;
    uint16_t lid;
    union ibv_gid gid;
    uint32_t rkey;
    uint64_t remote_addr;
};

class RdmaEndpoint {
public:
    RdmaEndpoint();
    ~RdmaEndpoint();

    bool create(RdmaContext& ctx, int send_cq_size = DEFAULT_RDMA_CQ_SIZE,
                int recv_cq_size = DEFAULT_RDMA_CQ_SIZE);

    void destroy();

    bool connect(RdmaContext& ctx, const RdmaEndpointInfo& remote_info);

    RdmaEndpointInfo get_local_info() const;

    ibv_qp* qp() const { return qp_; }
    ibv_cq* send_cq() const { return send_cq_; }
    ibv_cq* recv_cq() const { return recv_cq_; }
    uint32_t qpn() const { return qpn_; }

    bool is_connected() const { return connected_.load(std::memory_order_acquire); }

private:
    ibv_qp* qp_;
    ibv_cq* send_cq_;
    ibv_cq* recv_cq_;
    uint32_t qpn_;
    std::atomic<bool> connected_{false};
};

}
