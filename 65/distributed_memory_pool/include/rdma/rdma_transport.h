#pragma once

#include "rdma_context.h"
#include "rdma_endpoint.h"
#include "common/types.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <condition_variable>

namespace dmp {

struct RdmaConnection {
    NodeId peer_node_id;
    std::string peer_address;
    uint32_t peer_port;
    std::unique_ptr<RdmaEndpoint> endpoint;
    uint32_t remote_rkey;
    uint64_t remote_base_addr;
    std::atomic<bool> connected{false};
};

struct RdmaTransferResult {
    bool success;
    uint64_t bytes_transferred;
    std::string error;
};

class RdmaTransport {
public:
    using ConnectionCallback = std::function<void(NodeId, bool)>;

    RdmaTransport();
    ~RdmaTransport();

    bool initialize(RdmaContext& ctx, uint64_t local_base_addr,
                    uint32_t local_rkey, uint32_t local_lkey, uint64_t local_size);

    void shutdown();

    Result connect_peer(NodeId peer_node_id, const std::string& address,
                        uint32_t port, const RdmaEndpointInfo& remote_info);

    void disconnect_peer(NodeId peer_node_id);

    RdmaTransferResult rdma_write(NodeId peer_node_id, uint64_t remote_offset,
                                   void* local_buf, uint64_t length);

    RdmaTransferResult rdma_read(NodeId peer_node_id, uint64_t remote_offset,
                                  void* local_buf, uint64_t length);

    RdmaTransferResult rdma_write_imm(NodeId peer_node_id, uint64_t remote_offset,
                                       void* local_buf, uint64_t length,
                                       uint32_t imm_data);

    RdmaTransferResult rdma_compare_and_swap(NodeId peer_node_id, uint64_t remote_offset,
                                              uint64_t compare_val, uint64_t swap_val,
                                              uint64_t* result);

    bool is_peer_connected(NodeId peer_node_id) const;

    std::vector<NodeId> get_connected_peers() const;

    RdmaEndpointInfo get_local_endpoint_info() const;

    void set_connection_callback(ConnectionCallback callback) {
        connection_callback_ = std::move(callback);
    }

    uint64_t local_base_addr() const { return local_base_addr_; }
    uint32_t local_rkey() const { return local_rkey_; }
    uint32_t local_lkey() const { return local_lkey_; }
    uint64_t local_size() const { return local_size_; }

private:
    bool wait_for_completion(ibv_cq* cq, uint64_t wr_id, int timeout_ms = 5000);

    mutable std::shared_mutex mutex_;
    RdmaContext* context_;
    std::unordered_map<NodeId, std::unique_ptr<RdmaConnection>> connections_;
    std::unique_ptr<RdmaEndpoint> local_endpoint_;
    uint64_t local_base_addr_;
    uint32_t local_rkey_;
    uint32_t local_lkey_;
    uint64_t local_size_;
    ConnectionCallback connection_callback_;
    std::atomic<uint64_t> next_wr_id_{1};
};

}
