#include "rdma/rdma_context.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>

namespace dmp {

RdmaContext::RdmaContext()
    : device_(nullptr)
    , context_(nullptr)
    , pd_(nullptr)
    , port_num_(1)
    , gid_index_(0)
    , lid_(0)
{
    memset(&gid_, 0, sizeof(gid_));
}

RdmaContext::~RdmaContext() {
    shutdown();
}

std::vector<std::string> RdmaContext::list_devices() {
    std::vector<std::string> result;

    int num_devices;
    ibv_device** devices = ibv_get_device_list(&num_devices);
    if (!devices) {
        DMP_ERROR("Failed to get RDMA device list");
        return result;
    }

    for (int i = 0; i < num_devices; ++i) {
        result.push_back(ibv_get_device_name(devices[i]));
    }

    ibv_free_device_list(devices);
    return result;
}

bool RdmaContext::open_device(const std::string& device_name) {
    int num_devices;
    ibv_device** devices = ibv_get_device_list(&num_devices);
    if (!devices) {
        DMP_ERROR("Failed to get RDMA device list");
        return false;
    }

    device_ = nullptr;
    for (int i = 0; i < num_devices; ++i) {
        if (device_name == ibv_get_device_name(devices[i])) {
            device_ = devices[i];
            break;
        }
    }

    if (!device_) {
        DMP_ERROR("RDMA device '{}' not found", device_name);
        ibv_free_device_list(devices);
        return false;
    }

    context_ = ibv_open_device(device_);
    if (!context_) {
        DMP_ERROR("Failed to open RDMA device '{}'", device_name);
        ibv_free_device_list(devices);
        return false;
    }

    ibv_free_device_list(devices);
    return true;
}

bool RdmaContext::query_port() {
    ibv_port_attr port_attr;
    memset(&port_attr, 0, sizeof(port_attr));

    int ret = ibv_query_port(context_, port_num_, &port_attr);
    if (ret != 0) {
        DMP_ERROR("Failed to query port {}, errno={}", port_num_, ret);
        return false;
    }

    lid_ = port_attr.lid;

    if (port_attr.link_layer == IBV_LINK_LAYER_ETHERNET) {
        ret = ibv_query_gid(context_, port_num_, gid_index_, &gid_);
        if (ret != 0) {
            DMP_ERROR("Failed to query GID index {}, errno={}", gid_index_, ret);
            return false;
        }
    }

    return true;
}

bool RdmaContext::initialize(const std::string& device_name, int port_num, int gid_index) {
    if (initialized_.load(std::memory_order_acquire)) {
        DMP_WARN("RdmaContext already initialized");
        return true;
    }

    port_num_ = port_num;
    gid_index_ = gid_index;

    if (!open_device(device_name)) {
        return false;
    }

    if (!query_port()) {
        return false;
    }

    pd_ = ibv_alloc_pd(context_);
    if (!pd_) {
        DMP_ERROR("Failed to allocate protection domain");
        ibv_close_device(context_);
        context_ = nullptr;
        return false;
    }

    initialized_.store(true, std::memory_order_release);

    DMP_INFO("RDMA context initialized: device={}, port={}, gid_index={}, lid={}",
             device_name, port_num, gid_index, lid_);

    return true;
}

void RdmaContext::shutdown() {
    if (!initialized_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    if (pd_) {
        ibv_dealloc_pd(pd_);
        pd_ = nullptr;
    }

    if (context_) {
        ibv_close_device(context_);
        context_ = nullptr;
    }

    device_ = nullptr;
    DMP_INFO("RDMA context shut down");
}

ibv_mr* RdmaContext::register_memory(void* addr, size_t length) {
    if (!context_ || !pd_) {
        DMP_ERROR("RDMA context not initialized");
        return nullptr;
    }

    ibv_mr* mr = ibv_reg_mr(pd_, addr, length,
                            IBV_ACCESS_LOCAL_WRITE |
                            IBV_ACCESS_REMOTE_WRITE |
                            IBV_ACCESS_REMOTE_READ);
    if (!mr) {
        DMP_ERROR("Failed to register memory region: addr={}, length={}", addr, length);
        return nullptr;
    }

    DMP_DEBUG("Registered MR: addr={}, length={}, rkey={}, lkey={}",
              addr, length, mr->rkey, mr->lkey);

    return mr;
}

void RdmaContext::deregister_memory(ibv_mr* mr) {
    if (mr) {
        ibv_dereg_mr(mr);
    }
}

ibv_cq* RdmaContext::create_cq(int cqe) {
    if (!context_) {
        return nullptr;
    }

    ibv_cq* cq = ibv_create_cq(context_, cqe, nullptr, nullptr, 0);
    if (!cq) {
        DMP_ERROR("Failed to create CQ, cqe={}", cqe);
        return nullptr;
    }

    return cq;
}

void RdmaContext::destroy_cq(ibv_cq* cq) {
    if (cq) {
        ibv_destroy_cq(cq);
    }
}

ibv_srq* RdmaContext::create_srq(int max_wr) {
    if (!pd_) {
        return nullptr;
    }

    ibv_srq_init_attr srq_attr;
    memset(&srq_attr, 0, sizeof(srq_attr));
    srq_attr.attr.max_wr = max_wr;
    srq_attr.attr.max_sge = 1;

    ibv_srq* srq = ibv_create_srq(pd_, &srq_attr);
    if (!srq) {
        DMP_ERROR("Failed to create SRQ, max_wr={}", max_wr);
        return nullptr;
    }

    return srq;
}

void RdmaContext::destroy_srq(ibv_srq* srq) {
    if (srq) {
        ibv_destroy_srq(srq);
    }
}

ibv_qp* RdmaContext::create_qp(ibv_qp_init_attr& init_attr) {
    if (!pd_) {
        return nullptr;
    }

    ibv_qp* qp = ibv_create_qp(pd_, &init_attr);
    if (!qp) {
        DMP_ERROR("Failed to create QP");
        return nullptr;
    }

    return qp;
}

void RdmaContext::destroy_qp(ibv_qp* qp) {
    if (qp) {
        ibv_destroy_qp(qp);
    }
}

ibv_ah* RdmaContext::create_ah(const ibv_ah_attr& attr) {
    if (!pd_) {
        return nullptr;
    }

    ibv_ah* ah = ibv_create_ah(pd_, const_cast<ibv_ah_attr*>(&attr));
    if (!ah) {
        DMP_ERROR("Failed to create AH");
        return nullptr;
    }

    return ah;
}

void RdmaContext::destroy_ah(ibv_ah* ah) {
    if (ah) {
        ibv_destroy_ah(ah);
    }
}

bool RdmaContext::modify_qp_to_init(ibv_qp* qp, ibv_port_attr& port_attr) {
    ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = port_num_;
    attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ |
                            IBV_ACCESS_LOCAL_WRITE;

    int mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;

    int ret = ibv_modify_qp(qp, &attr, mask);
    if (ret != 0) {
        DMP_ERROR("Failed to modify QP to INIT state, errno={}", ret);
        return false;
    }

    return true;
}

bool RdmaContext::modify_qp_to_rtr(ibv_qp* qp, uint32_t remote_qpn, uint16_t remote_lid,
                                    const union ibv_gid& remote_gid, ibv_port_attr& port_attr) {
    ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_4096;
    attr.dest_qpn = remote_qpn;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 16;
    attr.min_rnr_timer = 12;

    if (port_attr.link_layer == IBV_LINK_LAYER_ETHERNET) {
        attr.ah_attr.is_global = 1;
        attr.ah_attr.grh.hop_limit = 1;
        memcpy(attr.ah_attr.grh.dgid.raw, remote_gid.raw, 16);
        attr.ah_attr.grh.sgid_index = gid_index_;
    } else {
        attr.ah_attr.is_global = 0;
        attr.ah_attr.dlid = remote_lid;
    }
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = port_num_;

    int mask = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
               IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
               IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;

    int ret = ibv_modify_qp(qp, &attr, mask);
    if (ret != 0) {
        DMP_ERROR("Failed to modify QP to RTR state, errno={}", ret);
        return false;
    }

    return true;
}

bool RdmaContext::modify_qp_to_rts(ibv_qp* qp) {
    ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = 0;
    attr.timeout = 14;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.max_rd_atomic = 16;

    int mask = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
               IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;

    int ret = ibv_modify_qp(qp, &attr, mask);
    if (ret != 0) {
        DMP_ERROR("Failed to modify QP to RTS state, errno={}", ret);
        return false;
    }

    return true;
}

int RdmaContext::poll_cq(ibv_cq* cq, ibv_wc* wc, int max_entries) {
    return ibv_poll_cq(cq, max_entries, wc);
}

bool RdmaContext::post_send(ibv_qp* qp, ibv_send_wr* wr) {
    ibv_send_wr* bad_wr = nullptr;
    int ret = ibv_post_send(qp, wr, &bad_wr);
    if (ret != 0) {
        DMP_ERROR("Failed to post send, errno={}", ret);
        return false;
    }
    return true;
}

bool RdmaContext::post_recv(ibv_qp* qp, ibv_recv_wr* wr) {
    ibv_recv_wr* bad_wr = nullptr;
    int ret = ibv_post_recv(qp, wr, &bad_wr);
    if (ret != 0) {
        DMP_ERROR("Failed to post recv, errno={}", ret);
        return false;
    }
    return true;
}

}
