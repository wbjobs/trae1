#include "rdma/rdma_endpoint.h"
#include <cstring>

namespace dmp {

RdmaEndpoint::RdmaEndpoint()
    : qp_(nullptr)
    , send_cq_(nullptr)
    , recv_cq_(nullptr)
    , qpn_(0)
{
}

RdmaEndpoint::~RdmaEndpoint() {
    destroy();
}

bool RdmaEndpoint::create(RdmaContext& ctx, int send_cq_size, int recv_cq_size) {
    if (qp_) {
        DMP_WARN("Endpoint already created");
        return true;
    }

    send_cq_ = ctx.create_cq(send_cq_size);
    if (!send_cq_) {
        DMP_ERROR("Failed to create send CQ");
        return false;
    }

    recv_cq_ = ctx.create_cq(recv_cq_size);
    if (!recv_cq_) {
        DMP_ERROR("Failed to create recv CQ");
        ctx.destroy_cq(send_cq_);
        send_cq_ = nullptr;
        return false;
    }

    ibv_qp_init_attr init_attr;
    memset(&init_attr, 0, sizeof(init_attr));
    init_attr.send_cq = send_cq_;
    init_attr.recv_cq = recv_cq_;
    init_attr.qp_type = IBV_QPT_RC;
    init_attr.cap.max_send_wr = DEFAULT_RDMA_QP_SIZE;
    init_attr.cap.max_recv_wr = DEFAULT_RDMA_QP_SIZE;
    init_attr.cap.max_send_sge = 1;
    init_attr.cap.max_recv_sge = 1;
    init_attr.cap.max_inline_data = 256;

    qp_ = ctx.create_qp(init_attr);
    if (!qp_) {
        DMP_ERROR("Failed to create QP");
        ctx.destroy_cq(send_cq_);
        ctx.destroy_cq(recv_cq_);
        send_cq_ = nullptr;
        recv_cq_ = nullptr;
        return false;
    }

    qpn_ = qp_->qp_num;

    ibv_port_attr port_attr;
    memset(&port_attr, 0, sizeof(port_attr));
    if (ibv_query_port(ctx.context(), ctx.port_num(), &port_attr) != 0) {
        DMP_ERROR("Failed to query port");
        destroy();
        return false;
    }

    if (!ctx.modify_qp_to_init(qp_, port_attr)) {
        DMP_ERROR("Failed to modify QP to INIT");
        destroy();
        return false;
    }

    DMP_DEBUG("Endpoint created: qpn={}", qpn_);

    return true;
}

void RdmaEndpoint::destroy() {
    if (qp_) {
        ibv_destroy_qp(qp_);
        qp_ = nullptr;
    }

    if (send_cq_) {
        ibv_destroy_cq(send_cq_);
        send_cq_ = nullptr;
    }

    if (recv_cq_) {
        ibv_destroy_cq(recv_cq_);
        recv_cq_ = nullptr;
    }

    connected_.store(false, std::memory_order_release);
    qpn_ = 0;
}

bool RdmaEndpoint::connect(RdmaContext& ctx, const RdmaEndpointInfo& remote_info) {
    if (!qp_) {
        DMP_ERROR("QP not created");
        return false;
    }

    ibv_port_attr port_attr;
    memset(&port_attr, 0, sizeof(port_attr));
    if (ibv_query_port(ctx.context(), ctx.port_num(), &port_attr) != 0) {
        DMP_ERROR("Failed to query port");
        return false;
    }

    if (!ctx.modify_qp_to_rtr(qp_, remote_info.qpn, remote_info.lid,
                               remote_info.gid, port_attr)) {
        DMP_ERROR("Failed to modify QP to RTR");
        return false;
    }

    if (!ctx.modify_qp_to_rts(qp_)) {
        DMP_ERROR("Failed to modify QP to RTS");
        return false;
    }

    connected_.store(true, std::memory_order_release);

    DMP_INFO("Endpoint connected: local_qpn={}, remote_qpn={}", qpn_, remote_info.qpn);

    return true;
}

RdmaEndpointInfo RdmaEndpoint::get_local_info() const {
    RdmaEndpointInfo info{};
    info.qpn = qpn_;
    return info;
}

}
