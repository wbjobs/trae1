#pragma once

#include "common/types.h"
#include "common/utils.h"
#include <infiniband/verbs.h>
#include <string>
#include <memory>
#include <vector>
#include <atomic>

namespace dmp {

struct RdmaDeviceInfo {
    std::string name;
    ibv_device* device;
    ibv_context* context;
    int port_num;
    uint16_t lid;
    union ibv_gid gid;
};

class RdmaContext {
public:
    RdmaContext();
    ~RdmaContext();

    bool initialize(const std::string& device_name, int port_num, int gid_index);

    void shutdown();

    ibv_pd* protection_domain() const { return pd_; }
    ibv_context* context() const { return context_; }
    ibv_device* device() const { return device_; }
    int port_num() const { return port_num_; }
    uint16_t lid() const { return lid_; }
    const union ibv_gid& gid() const { return gid_; }
    int gid_index() const { return gid_index_; }

    ibv_mr* register_memory(void* addr, size_t length);
    void deregister_memory(ibv_mr* mr);

    ibv_cq* create_cq(int cqe = DEFAULT_RDMA_CQ_SIZE);
    void destroy_cq(ibv_cq* cq);

    ibv_srq* create_srq(int max_wr = DEFAULT_RDMA_SRQ_SIZE);
    void destroy_srq(ibv_srq* srq);

    ibv_qp* create_qp(ibv_qp_init_attr& init_attr);
    void destroy_qp(ibv_qp* qp);

    ibv_ah* create_ah(const ibv_ah_attr& attr);
    void destroy_ah(ibv_ah* ah);

    bool modify_qp_to_init(ibv_qp* qp, ibv_port_attr& port_attr);
    bool modify_qp_to_rtr(ibv_qp* qp, uint32_t remote_qpn, uint16_t remote_lid,
                          const union ibv_gid& remote_gid, ibv_port_attr& port_attr);
    bool modify_qp_to_rts(ibv_qp* qp);

    int poll_cq(ibv_cq* cq, ibv_wc* wc, int max_entries);

    bool post_send(ibv_qp* qp, ibv_send_wr* wr);
    bool post_recv(ibv_qp* qp, ibv_recv_wr* wr);

    static std::vector<std::string> list_devices();

private:
    bool open_device(const std::string& device_name);
    bool query_port();

    ibv_device* device_;
    ibv_context* context_;
    ibv_pd* pd_;
    int port_num_;
    int gid_index_;
    uint16_t lid_;
    union ibv_gid gid_;
    std::atomic<bool> initialized_{false};
};

}
