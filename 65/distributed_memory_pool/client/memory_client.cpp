#include "memory_client.h"
#include <chrono>

namespace dmp {

MemoryClient::MemoryClient() = default;

MemoryClient::~MemoryClient() {
    disconnect();
}

bool MemoryClient::connect(const std::string& server_address, uint32_t port) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (connected_.load()) {
        return true;
    }

    std::string address = server_address + ":" + std::to_string(port);

    grpc::ChannelArguments args;
    args.SetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, 2 * MAX_TRANSFER_SIZE);
    args.SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, 2 * MAX_TRANSFER_SIZE);

    channel_ = grpc::CreateCustomChannel(address,
                                          grpc::InsecureChannelCredentials(),
                                          args);

    if (!channel_->WaitForConnected(std::chrono::system_clock::now() +
                                     std::chrono::seconds(5))) {
        DMP_ERROR("Failed to connect to server: {}", address);
        return false;
    }

    stub_ = dmp::MemoryService::NewStub(channel_);
    connected_.store(true, std::memory_order_release);

    DMP_INFO("Connected to server: {}", address);

    return true;
}

void MemoryClient::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    if (channel_) {
        channel_.reset();
    }

    if (stub_) {
        stub_.reset();
    }

    DMP_INFO("Disconnected from server");
}

ResultT<BlockInfo> MemoryClient::allocate(uint64_t size, NodeId client_node_id,
                                           bool exclusive) {
    if (!connected_.load()) {
        return ResultT<BlockInfo>::error("Not connected to server");
    }

    dmp::AllocateRequest request;
    request.set_size(size);
    request.set_client_node_id(client_node_id);
    request.set_exclusive(exclusive);

    dmp::AllocateResponse response;

    grpc::ClientContext context;
    grpc::Status status = stub_->Allocate(&context, request, &response);

    if (!status.ok()) {
        return ResultT<BlockInfo>::error("RPC failed: " + status.error_message());
    }

    if (!response.success()) {
        return ResultT<BlockInfo>::error(response.error_message());
    }

    BlockInfo info{};
    info.block_id = response.block().block_id();
    info.offset = response.block().offset();
    info.size = response.block().size();
    info.data_offset = response.block().offset();
    info.state = static_cast<BlockState>(response.block().state());
    info.owner_node_id = response.block().owner_node_id();
    info.rkey = response.remote_rkey();
    info.remote_addr = 0;

    DMP_DEBUG("Allocated block: id={}, offset={}, size={}, data_offset={}",
              info.block_id, info.offset, info.size, info.data_offset);

    return ResultT<BlockInfo>::ok(info);
}

Result MemoryClient::release(BlockId block_id, NodeId client_node_id) {
    if (!connected_.load()) {
        return Result::error("Not connected to server");
    }

    dmp::ReleaseRequest request;
    request.set_block_id(block_id);
    request.set_client_node_id(client_node_id);

    dmp::ReleaseResponse response;

    grpc::ClientContext context;
    grpc::Status status = stub_->Release(&context, request, &response);

    if (!status.ok()) {
        return Result::error("RPC failed: " + status.error_message());
    }

    if (!response.success()) {
        return Result::error(response.error_message());
    }

    DMP_DEBUG("Released block: id={}", block_id);

    return Result::ok();
}

Result MemoryClient::put(BlockId block_id, uint64_t offset, const void* data,
                          uint64_t length, NodeId client_node_id) {
    if (!connected_.load()) {
        return Result::error("Not connected to server");
    }

    if (length > MAX_TRANSFER_SIZE) {
        return Result::error("Data too large");
    }

    dmp::PutRequest request;
    request.set_block_id(block_id);
    request.set_offset(offset);
    request.set_length(length);
    request.set_data(data, length);
    request.set_client_node_id(client_node_id);

    dmp::PutResponse response;

    grpc::ClientContext context;
    grpc::Status status = stub_->Put(&context, request, &response);

    if (!status.ok()) {
        return Result::error("RPC failed: " + status.error_message());
    }

    if (!response.success()) {
        return Result::error(response.error_message());
    }

    DMP_DEBUG("Put data: block_id={}, offset={}, length={}",
              block_id, offset, length);

    return Result::ok();
}

ResultT<std::vector<uint8_t>> MemoryClient::get(BlockId block_id, uint64_t offset,
                                                  uint64_t length, NodeId client_node_id) {
    if (!connected_.load()) {
        return ResultT<std::vector<uint8_t>>::error("Not connected to server");
    }

    if (length > MAX_TRANSFER_SIZE) {
        return ResultT<std::vector<uint8_t>>::error("Request too large");
    }

    dmp::GetRequest request;
    request.set_block_id(block_id);
    request.set_offset(offset);
    request.set_length(length);
    request.set_client_node_id(client_node_id);

    dmp::GetResponse response;

    grpc::ClientContext context;
    grpc::Status status = stub_->Get(&context, request, &response);

    if (!status.ok()) {
        return ResultT<std::vector<uint8_t>>::error("RPC failed: " + status.error_message());
    }

    if (!response.success()) {
        return ResultT<std::vector<uint8_t>>::error(response.error_message());
    }

    std::vector<uint8_t> data(response.data().begin(), response.data().end());

    DMP_DEBUG("Get data: block_id={}, offset={}, length={}",
              block_id, offset, data.size());

    return ResultT<std::vector<uint8_t>>::ok(std::move(data));
}

ResultT<Stats> MemoryClient::monitor() {
    if (!connected_.load()) {
        return ResultT<Stats>::error("Not connected to server");
    }

    dmp::MonitorRequest request;
    request.set_detailed(true);

    dmp::MonitorResponse response;

    grpc::ClientContext context;
    grpc::Status status = stub_->Monitor(&context, request, &response);

    if (!status.ok()) {
        return ResultT<Stats>::error("RPC failed: " + status.error_message());
    }

    Stats stats{};
    const auto& global_stats = response.global_stats();
    stats.total_capacity = global_stats.total_capacity();
    stats.used_capacity = global_stats.used_capacity();
    stats.free_capacity = global_stats.free_capacity();
    stats.usage_percent = global_stats.usage_percent();
    stats.fragmentation_ratio = global_stats.fragmentation_ratio();
    stats.total_blocks = global_stats.total_blocks();
    stats.allocated_blocks = global_stats.allocated_blocks();
    stats.free_blocks = global_stats.free_blocks();

    DMP_DEBUG("Monitor: usage={:.2f}%, fragments={:.2f}",
              stats.usage_percent, stats.fragmentation_ratio);

    return ResultT<Stats>::ok(stats);
}

Result MemoryClient::heartbeat(NodeId node_id, const Stats& stats) {
    if (!connected_.load()) {
        return Result::error("Not connected to server");
    }

    dmp::HeartbeatRequest request;
    request.set_node_id(node_id);
    request.set_timestamp(current_timestamp_ms());

    auto* stats_msg = request.mutable_stats();
    stats_msg->set_total_capacity(stats.total_capacity);
    stats_msg->set_used_capacity(stats.used_capacity);
    stats_msg->set_free_capacity(stats.free_capacity);
    stats_msg->set_usage_percent(stats.usage_percent);
    stats_msg->set_fragmentation_ratio(stats.fragmentation_ratio);

    dmp::HeartbeatResponse response;

    grpc::ClientContext context;
    grpc::Status status = stub_->Heartbeat(&context, request, &response);

    if (!status.ok()) {
        return Result::error("RPC failed: " + status.error_message());
    }

    return Result::ok();
}

Result MemoryClient::report_node_failure(NodeId failed_node_id, const std::string& reason) {
    if (!connected_.load()) {
        return Result::error("Not connected to server");
    }

    dmp::NodeFailureRequest request;
    request.set_failed_node_id(failed_node_id);
    request.set_reason(reason);

    dmp::NodeFailureResponse response;

    grpc::ClientContext context;
    grpc::Status status = stub_->ReportNodeFailure(&context, request, &response);

    if (!status.ok()) {
        return Result::error("RPC failed: " + status.error_message());
    }

    DMP_INFO("Reported node failure: node_id={}, recovered={} bytes",
             failed_node_id, response.recovered_memory());

    return Result::ok();
}

bool MemoryClient::is_data_state_error(const std::string& error_msg) {
    return error_msg.find("Block has not been written") != std::string::npos ||
           error_msg.find("Block is currently being written") != std::string::npos ||
           error_msg.find("Previous write to this block failed") != std::string::npos ||
           error_msg.find("Block data state is invalid") != std::string::npos;
}

bool MemoryClient::is_writing_error(const std::string& error_msg) {
    return error_msg.find("currently being written") != std::string::npos;
}

bool MemoryClient::is_write_failed_error(const std::string& error_msg) {
    return error_msg.find("Previous write to this block failed") != std::string::npos;
}

bool MemoryClient::is_initial_state_error(const std::string& error_msg) {
    return error_msg.find("has not been written") != std::string::npos;
}

ResultT<LockInfo> MemoryClient::lock(BlockId block_id, NodeId client_node_id,
                                       uint64_t timeout_ms) {
    if (!connected_.load()) {
        return ResultT<LockInfo>::error("Not connected to server");
    }

    dmp::LockRequest request;
    request.set_block_id(block_id);
    request.set_client_node_id(client_node_id);
    request.set_timeout_ms(timeout_ms);

    dmp::LockResponse response;

    grpc::ClientContext context;
    grpc::Status status = stub_->Lock(&context, request, &response);

    if (!status.ok()) {
        return ResultT<LockInfo>::error("RPC failed: " + status.error_message());
    }

    if (!response.success()) {
        return ResultT<LockInfo>::error(response.error_message());
    }

    LockInfo info{};
    info.block_id = response.lock().block_id();
    info.owner_node_id = response.lock().owner_node_id();
    info.state = static_cast<LockState>(response.lock().state());
    info.acquire_count = response.lock().acquire_count();
    info.expired = response.lock().expired();

    DMP_DEBUG("Lock acquired: block={}, node={}", block_id, client_node_id);

    return ResultT<LockInfo>::ok(info);
}

ResultT<LockInfo> MemoryClient::try_lock(BlockId block_id, NodeId client_node_id) {
    return lock(block_id, client_node_id, 0);
}

Result MemoryClient::unlock(BlockId block_id, NodeId client_node_id, bool force) {
    if (!connected_.load()) {
        return Result::error("Not connected to server");
    }

    dmp::UnlockRequest request;
    request.set_block_id(block_id);
    request.set_client_node_id(client_node_id);
    request.set_force(force);

    dmp::UnlockResponse response;

    grpc::ClientContext context;
    grpc::Status status = stub_->Unlock(&context, request, &response);

    if (!status.ok()) {
        return Result::error("RPC failed: " + status.error_message());
    }

    if (!response.success()) {
        return Result::error(response.error_message());
    }

    DMP_DEBUG("Lock released: block={}, node={}", block_id, client_node_id);

    return Result::ok();
}

Result MemoryClient::renew_lock(BlockId block_id, NodeId client_node_id) {
    if (!connected_.load()) {
        return Result::error("Not connected to server");
    }

    dmp::RenewLockRequest request;
    request.set_block_id(block_id);
    request.set_client_node_id(client_node_id);

    dmp::RenewLockResponse response;

    grpc::ClientContext context;
    grpc::Status status = stub_->RenewLock(&context, request, &response);

    if (!status.ok()) {
        return Result::error("RPC failed: " + status.error_message());
    }

    if (!response.success()) {
        return Result::error(response.error_message());
    }

    DMP_DEBUG("Lock renewed: block={}, node={}", block_id, client_node_id);

    return Result::ok();
}

ResultT<LockInfo> MemoryClient::get_lock_info(BlockId block_id) {
    return ResultT<LockInfo>::error("Not implemented, use lock_monitor");
}

ResultT<LockStats> MemoryClient::lock_monitor(bool detailed) {
    if (!connected_.load()) {
        return ResultT<LockStats>::error("Not connected to server");
    }

    dmp::LockMonitorRequest request;
    request.set_detailed(detailed);

    dmp::LockMonitorResponse response;

    grpc::ClientContext context;
    grpc::Status status = stub_->LockMonitor(&context, request, &response);

    if (!status.ok()) {
        return ResultT<LockStats>::error("RPC failed: " + status.error_message());
    }

    LockStats stats{};
    const auto& pb_stats = response.stats();
    stats.total_locks = pb_stats.total_locks();
    stats.active_locks = pb_stats.active_locks();
    stats.lock_acquisitions = pb_stats.lock_acquisitions();
    stats.lock_releases = pb_stats.lock_releases();
    stats.lock_timeouts = pb_stats.lock_timeouts();
    stats.lock_contentions = pb_stats.lock_contentions();
    stats.deadlocks_detected = pb_stats.deadlocks_detected();
    stats.avg_lock_hold_time_ms = pb_stats.avg_lock_hold_time_ms();

    return ResultT<LockStats>::ok(stats);
}

}
