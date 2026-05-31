#include "rdma/rdma_transport.h"
#include <cstring>
#include <chrono>
#include <thread>

namespace dmp {

RdmaTransport::RdmaTransport()
    : context_(nullptr)
    , local_base_addr_(0)
    , local_rkey_(0)
    , local_lkey_(0)
    , local_size_(0)
{
}

RdmaTransport::~RdmaTransport() {
    shutdown();
}

bool RdmaTransport::initialize(RdmaContext& ctx, uint64_t local_base_addr,
                                uint32_t local_rkey, uint32_t local_lkey, uint64_t local_size) {
    context_ = &ctx;
    local_base_addr_ = local_base_addr;
    local_rkey_ = local_rkey;
    local_lkey_ = local_lkey;
    local_size_ = local_size;

    local_endpoint_ = std::make_unique<RdmaEndpoint>();
    if (!local_endpoint_->create(ctx)) {
        DMP_ERROR("Failed to create local endpoint");
        return false;
    }

    DMP_INFO("RDMA transport initialized: base_addr=0x{:x}, size={}MB",
             local_base_addr, local_size / (1024 * 1024));

    return true;
}

void RdmaTransport::shutdown() {
    {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        for (auto& [node_id, conn] : connections_) {
            if (conn) {
                conn->endpoint->destroy();
            }
        }
        connections_.clear();
    }

    if (local_endpoint_) {
        local_endpoint_->destroy();
        local_endpoint_.reset();
    }

    context_ = nullptr;
    DMP_INFO("RDMA transport shut down");
}

Result RdmaTransport::connect_peer(NodeId peer_node_id, const std::string& address,
                                    uint32_t port, const RdmaEndpointInfo& remote_info) {
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = connections_.find(peer_node_id);
        if (it != connections_.end() && it->second->connected.load()) {
            return Result::ok();
        }
    }

    auto conn = std::make_unique<RdmaConnection>();
    conn->peer_node_id = peer_node_id;
    conn->peer_address = address;
    conn->peer_port = port;
    conn->endpoint = std::make_unique<RdmaEndpoint>();

    if (!conn->endpoint->create(*context_)) {
        return Result::error("Failed to create endpoint for peer");
    }

    RdmaEndpointInfo local_info = local_endpoint_->get_local_info();

    if (!conn->endpoint->connect(*context_, remote_info)) {
        conn->endpoint->destroy();
        return Result::error("Failed to connect to peer");
    }

    conn->remote_rkey = remote_info.rkey;
    conn->remote_base_addr = remote_info.remote_addr;
    conn->connected.store(true, std::memory_order_release);

    {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        connections_[peer_node_id] = std::move(conn);
    }

    if (connection_callback_) {
        connection_callback_(peer_node_id, true);
    }

    DMP_INFO("Connected to peer: node_id={}, address={}:{}", peer_node_id, address, port);

    return Result::ok();
}

void RdmaTransport::disconnect_peer(NodeId peer_node_id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);

    auto it = connections_.find(peer_node_id);
    if (it != connections_.end()) {
        if (it->second) {
            it->second->endpoint->destroy();
        }
        connections_.erase(it);

        if (connection_callback_) {
            connection_callback_(peer_node_id, false);
        }

        DMP_INFO("Disconnected from peer: node_id={}", peer_node_id);
    }
}

RdmaTransferResult RdmaTransport::rdma_write(NodeId peer_node_id, uint64_t remote_offset,
                                              void* local_buf, uint64_t length) {
    if (length > MAX_TRANSFER_SIZE) {
        return {false, 0, "Transfer size exceeds maximum"};
    }

    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = connections_.find(peer_node_id);
    if (it == connections_.end() || !it->second->connected.load()) {
        return {false, 0, "Peer not connected"};
    }

    auto& conn = it->second;

    uint64_t wr_id = next_wr_id_.fetch_add(1, std::memory_order_relaxed);

    ibv_sge sge;
    memset(&sge, 0, sizeof(sge));
    sge.addr = reinterpret_cast<uint64_t>(local_buf);
    sge.length = length;
    sge.lkey = context_->context()->ops->dummy ? 0 : 0;

    ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = conn->remote_base_addr + remote_offset;
    wr.wr.rdma.rkey = conn->remote_rkey;

    if (!context_->post_send(conn->endpoint->qp(), &wr)) {
        return {false, 0, "Failed to post RDMA write"};
    }

    if (!wait_for_completion(conn->endpoint->send_cq(), wr_id)) {
        return {false, 0, "RDMA write completion timeout"};
    }

    return {true, length, ""};
}

RdmaTransferResult RdmaTransport::rdma_read(NodeId peer_node_id, uint64_t remote_offset,
                                             void* local_buf, uint64_t length) {
    if (length > MAX_TRANSFER_SIZE) {
        return {false, 0, "Transfer size exceeds maximum"};
    }

    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = connections_.find(peer_node_id);
    if (it == connections_.end() || !it->second->connected.load()) {
        return {false, 0, "Peer not connected"};
    }

    auto& conn = it->second;

    uint64_t wr_id = next_wr_id_.fetch_add(1, std::memory_order_relaxed);

    ibv_sge sge;
    memset(&sge, 0, sizeof(sge));
    sge.addr = reinterpret_cast<uint64_t>(local_buf);
    sge.length = length;
    sge.lkey = local_lkey_;

    ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_READ;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = conn->remote_base_addr + remote_offset;
    wr.wr.rdma.rkey = conn->remote_rkey;

    if (!context_->post_send(conn->endpoint->qp(), &wr)) {
        return {false, 0, "Failed to post RDMA read"};
    }

    if (!wait_for_completion(conn->endpoint->send_cq(), wr_id)) {
        return {false, 0, "RDMA read completion timeout"};
    }

    return {true, length, ""};
}

RdmaTransferResult RdmaTransport::rdma_write_imm(NodeId peer_node_id, uint64_t remote_offset,
                                                   void* local_buf, uint64_t length,
                                                   uint32_t imm_data) {
    if (length > MAX_TRANSFER_SIZE) {
        return {false, 0, "Transfer size exceeds maximum"};
    }

    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = connections_.find(peer_node_id);
    if (it == connections_.end() || !it->second->connected.load()) {
        return {false, 0, "Peer not connected"};
    }

    auto& conn = it->second;

    uint64_t wr_id = next_wr_id_.fetch_add(1, std::memory_order_relaxed);

    ibv_sge sge;
    memset(&sge, 0, sizeof(sge));
    sge.addr = reinterpret_cast<uint64_t>(local_buf);
    sge.length = length;
    sge.lkey = local_lkey_;

    ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.imm_data = imm_data;
    wr.wr.rdma.remote_addr = conn->remote_base_addr + remote_offset;
    wr.wr.rdma.rkey = conn->remote_rkey;

    if (!context_->post_send(conn->endpoint->qp(), &wr)) {
        return {false, 0, "Failed to post RDMA write with imm"};
    }

    if (!wait_for_completion(conn->endpoint->send_cq(), wr_id)) {
        return {false, 0, "RDMA write with imm completion timeout"};
    }

    return {true, length, ""};
}

RdmaTransferResult RdmaTransport::rdma_compare_and_swap(NodeId peer_node_id,
                                                         uint64_t remote_offset,
                                                         uint64_t compare_val,
                                                         uint64_t swap_val,
                                                         uint64_t* result) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = connections_.find(peer_node_id);
    if (it == connections_.end() || !it->second->connected.load()) {
        return {false, 0, "Peer not connected"};
    }

    auto& conn = it->second;

    uint64_t wr_id = next_wr_id_.fetch_add(1, std::memory_order_relaxed);

    uint64_t local_buf;
    ibv_sge sge;
    memset(&sge, 0, sizeof(sge));
    sge.addr = reinterpret_cast<uint64_t>(&local_buf);
    sge.length = sizeof(local_buf);
    sge.lkey = local_lkey_;

    ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.atomic.remote_addr = conn->remote_base_addr + remote_offset;
    wr.wr.atomic.rkey = conn->remote_rkey;
    wr.wr.atomic.compare_add = compare_val;
    wr.wr.atomic.swap = swap_val;

    if (!context_->post_send(conn->endpoint->qp(), &wr)) {
        return {false, 0, "Failed to post RDMA CAS"};
    }

    if (!wait_for_completion(conn->endpoint->send_cq(), wr_id)) {
        return {false, 0, "RDMA CAS completion timeout"};
    }

    if (result) {
        *result = local_buf;
    }

    return {true, sizeof(local_buf), ""};
}

bool RdmaTransport::wait_for_completion(ibv_cq* cq, uint64_t wr_id, int timeout_ms) {
    auto start = std::chrono::steady_clock::now();

    while (true) {
        ibv_wc wc;
        int num_completions = context_->poll_cq(cq, &wc, 1);

        if (num_completions > 0) {
            if (wc.wr_id == wr_id) {
                if (wc.status != IBV_WC_SUCCESS) {
                    DMP_ERROR("Completion error: status={}", wc.status);
                    return false;
                }
                return true;
            }
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout_ms) {
            DMP_ERROR("Completion timeout for wr_id={}", wr_id);
            return false;
        }

        std::this_thread::yield();
    }
}

bool RdmaTransport::is_peer_connected(NodeId peer_node_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = connections_.find(peer_node_id);
    return it != connections_.end() && it->second->connected.load();
}

std::vector<NodeId> RdmaTransport::get_connected_peers() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<NodeId> result;
    for (const auto& [node_id, conn] : connections_) {
        if (conn->connected.load()) {
            result.push_back(node_id);
        }
    }
    return result;
}

RdmaEndpointInfo RdmaTransport::get_local_endpoint_info() const {
    RdmaEndpointInfo info = local_endpoint_->get_local_info();
    info.rkey = local_rkey_;
    info.remote_addr = local_base_addr_;
    return info;
}

}
