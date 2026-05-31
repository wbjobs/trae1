#include "service/memory_service_impl.h"
#include <cstring>
#include <sys/mman.h>
#include <chrono>
#include <thread>

#ifndef MAP_HUGETLB
#define MAP_HUGETLB 0x40000
#endif

namespace dmp {

MemoryServiceImpl::MemoryServiceImpl()
    : memory_base_(nullptr)
    , memory_mr_(nullptr)
{
}

MemoryServiceImpl::~MemoryServiceImpl() {
    shutdown();
}

bool MemoryServiceImpl::initialize(const Config& config) {
    config_ = config;

    if (!initialize_memory()) {
        DMP_ERROR("Failed to initialize memory");
        return false;
    }

    if (!initialize_rdma()) {
        DMP_ERROR("Failed to initialize RDMA");
        return false;
    }

    if (!initialize_lock_manager()) {
        DMP_ERROR("Failed to initialize lock manager");
        return false;
    }

    start_background_tasks();

    DMP_INFO("MemoryService initialized: node_id={}, total_memory={}MB",
             config_.node_id, config_.total_memory / (1024 * 1024));

    return true;
}

void MemoryServiceImpl::shutdown() {
    if (!running_.exchange(false)) {
        return;
    }

    stop_background_tasks();

    if (lock_manager_) {
        lock_manager_->shutdown();
    }

    if (rdma_transport_) {
        rdma_transport_->shutdown();
    }

    if (rdma_context_) {
        if (memory_mr_) {
            rdma_context_->deregister_memory(memory_mr_);
            memory_mr_ = nullptr;
        }
        rdma_context_->shutdown();
    }

    if (memory_base_) {
        munmap(memory_base_, config_.total_memory);
        memory_base_ = nullptr;
    }

    DMP_INFO("MemoryService shut down");
}

bool MemoryServiceImpl::initialize_memory() {
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    if (config_.use_hugepages) {
        flags |= MAP_HUGETLB;
    }

    memory_base_ = mmap(nullptr, config_.total_memory, PROT_READ | PROT_WRITE,
                        flags, -1, 0);
    if (memory_base_ == MAP_FAILED) {
        DMP_ERROR("Failed to allocate memory: {}", strerror(errno));
        memory_base_ = nullptr;
        return false;
    }

    if (mlock(memory_base_, config_.total_memory) != 0) {
        DMP_WARN("Failed to lock memory: {}, continuing without mlock", strerror(errno));
    }

    memset(memory_base_, 0, config_.total_memory);

    memory_pool_ = std::make_unique<MemoryPool>();
    if (!memory_pool_->initialize(config_.total_memory, memory_base_)) {
        DMP_ERROR("Failed to initialize memory pool");
        return false;
    }

    DMP_INFO("Memory initialized: base={}, size={}MB", memory_base_,
             config_.total_memory / (1024 * 1024));

    return true;
}

bool MemoryServiceImpl::initialize_rdma() {
    rdma_context_ = std::make_unique<RdmaContext>();
    if (!rdma_context_->initialize(config_.rdma_device, config_.rdma_port,
                                    config_.rdma_gid_index)) {
        DMP_ERROR("Failed to initialize RDMA context");
        return false;
    }

    memory_mr_ = rdma_context_->register_memory(memory_base_, config_.total_memory);
    if (!memory_mr_) {
        DMP_ERROR("Failed to register memory with RDMA");
        return false;
    }

    memory_pool_->set_rkey(memory_mr_->rkey);
    memory_pool_->set_lkey(memory_mr_->lkey);

    rdma_transport_ = std::make_unique<RdmaTransport>();
    if (!rdma_transport_->initialize(*rdma_context_,
                                      reinterpret_cast<uint64_t>(memory_base_),
                                      memory_mr_->rkey,
                                      memory_mr_->lkey,
                                      config_.total_memory)) {
        DMP_ERROR("Failed to initialize RDMA transport");
        return false;
    }

    DMP_INFO("RDMA initialized: device={}, port={}, gid_index={}",
             config_.rdma_device, config_.rdma_port, config_.rdma_gid_index);

    return true;
}

bool MemoryServiceImpl::initialize_lock_manager() {
    lock_manager_ = std::make_unique<DistributedLockManager>();

    lock_manager_->set_lock_timeout_callback([this](BlockId block_id, NodeId node_id) {
        DMP_WARN("Lock timeout callback: block={}, node={}", block_id, node_id);
    });

    lock_manager_->set_deadlock_callback([this](const std::vector<BlockId>& blocks) {
        DMP_WARN("Deadlock detected: {} blocks involved", blocks.size());
        for (auto block_id : blocks) {
            lock_manager_->force_unlock(block_id);
        }
    });

    if (!lock_manager_->initialize(memory_pool_.get())) {
        DMP_ERROR("Failed to initialize lock manager");
        return false;
    }

    DMP_INFO("Lock manager initialized");
    return true;
}

void MemoryServiceImpl::start_background_tasks() {
    running_.store(true, std::memory_order_release);

    heartbeat_thread_ = std::make_unique<std::thread>(&MemoryServiceImpl::heartbeat_task, this);
    lease_check_thread_ = std::make_unique<std::thread>(&MemoryServiceImpl::lease_check_task, this);
}

void MemoryServiceImpl::stop_background_tasks() {
    if (heartbeat_thread_ && heartbeat_thread_->joinable()) {
        heartbeat_thread_->join();
    }
    if (lease_check_thread_ && lease_check_thread_->joinable()) {
        lease_check_thread_->join();
    }
}

void MemoryServiceImpl::heartbeat_task() {
    DMP_INFO("Heartbeat task started");

    while (running_.load(std::memory_order_acquire)) {
        auto now = std::chrono::steady_clock::now();
        auto timeout = std::chrono::milliseconds(config_.heartbeat_timeout_ms);

        {
            std::lock_guard<std::shared_mutex> lock(peers_mutex_);
            for (auto& [node_id, peer] : peers_) {
                if (peer.state == NodeState::Online) {
                    auto elapsed = now - peer.last_heartbeat;
                    if (elapsed > timeout) {
                        DMP_WARN("Node {} heartbeat timeout, marking as offline", node_id);
                        peer.state = NodeState::Offline;

                        uint64_t recovered = memory_pool_->recover_node(node_id).value;
                        DMP_INFO("Recovered {} bytes from failed node {}", recovered, node_id);
                    }
                }
            }
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.heartbeat_interval_ms / 2));
    }

    DMP_INFO("Heartbeat task stopped");
}

void MemoryServiceImpl::lease_check_task() {
    DMP_INFO("Lease check task started");

    while (running_.load(std::memory_order_acquire)) {
        memory_pool_->check_leases();

        std::this_thread::sleep_for(std::chrono::milliseconds(LEASE_DURATION_MS / 2));
    }

    DMP_INFO("Lease check task stopped");
}

grpc::Status MemoryServiceImpl::Allocate(grpc::ServerContext* context,
                                          const dmp::AllocateRequest* request,
                                          dmp::AllocateResponse* response) {
    DMP_DEBUG("Allocate request: size={}, client_node_id={}",
              request->size(), request->client_node_id());

    if (request->size() == 0 || request->size() > MAX_TRANSFER_SIZE) {
        response->set_success(false);
        response->set_error_message("Invalid allocation size");
        return grpc::Status::OK;
    }

    auto result = memory_pool_->allocate(request->size(), request->client_node_id());
    if (!result.success) {
        response->set_success(false);
        response->set_error_message(result.error_message);
        return grpc::Status::OK;
    }

    auto& block = result.value;
    response->set_success(true);

    auto* pb_block = response->mutable_block();
    pb_block->set_block_id(block.block_id);
    pb_block->set_offset(block.data_offset);
    pb_block->set_size(block.size - BLOCK_DATA_HEADER_SIZE);
    pb_block->set_state(static_cast<dmp::MemoryBlockState>(block.state));
    pb_block->set_owner_node_id(block.owner_node_id);
    pb_block->set_data_state(dmp::DATA_STATE_INITIAL);

    response->set_remote_node_id(config_.node_id);
    response->set_remote_address(config_.grpc_address);
    response->set_remote_rkey(block.rkey);
    response->set_remote_offset(block.data_offset);

    DMP_DEBUG("Allocate response: block_id={}, offset={}, size={}, data_offset={}",
              block.block_id, block.offset, block.size, block.data_offset);

    return grpc::Status::OK;
}

grpc::Status MemoryServiceImpl::Release(grpc::ServerContext* context,
                                         const dmp::ReleaseRequest* request,
                                         dmp::ReleaseResponse* response) {
    DMP_DEBUG("Release request: block_id={}, client_node_id={}",
              request->block_id(), request->client_node_id());

    auto result = memory_pool_->release(request->block_id(), request->client_node_id());
    if (!result.success) {
        response->set_success(false);
        response->set_error_message(result.error_message);
        return grpc::Status::OK;
    }

    response->set_success(true);

    DMP_DEBUG("Release response: success");

    return grpc::Status::OK;
}

grpc::Status MemoryServiceImpl::Put(grpc::ServerContext* context,
                                     const dmp::PutRequest* request,
                                     dmp::PutResponse* response) {
    DMP_DEBUG("Put request: block_id={}, offset={}, length={}",
              request->block_id(), request->offset(), request->length());

    if (request->length() > MAX_TRANSFER_SIZE) {
        response->set_success(false);
        response->set_error_message("Transfer size exceeds maximum");
        return grpc::Status::OK;
    }

    auto block_info = memory_pool_->get_block_info(request->block_id());
    if (!block_info.success) {
        response->set_success(false);
        response->set_error_message(block_info.error_message);
        return grpc::Status::OK;
    }

    if (request->offset() + request->length() > memory_pool_->get_block_data_size(request->block_id())) {
        response->set_success(false);
        response->set_error_message("Write exceeds block bounds");
        return grpc::Status::OK;
    }

    uint64_t old_state = static_cast<uint64_t>(memory_pool_->get_block_data_state(request->block_id()));

    if (old_state != static_cast<uint64_t>(BlockDataState::INITIAL) &&
        old_state != static_cast<uint64_t>(BlockDataState::WRITTEN) &&
        old_state != static_cast<uint64_t>(BlockDataState::WRITE_FAILED)) {
        response->set_success(false);
        response->set_error_message("Block is currently being written to, please retry");
        return grpc::Status::OK;
    }

    memory_pool_->set_block_data_state(request->block_id(), BlockDataState::WRITING);

    uint64_t write_offset = block_info.value.data_offset + request->offset();
    void* dest_ptr = static_cast<uint8_t*>(memory_base_) + write_offset;

    bool write_success = true;
    try {
        memcpy(dest_ptr, request->data().data(), request->length());
    } catch (const std::exception& e) {
        write_success = false;
        DMP_ERROR("Memory copy failed: {}", e.what());
    }

    if (write_success) {
        memory_pool_->set_block_data_state(request->block_id(), BlockDataState::WRITTEN);
    } else {
        memory_pool_->set_block_data_state(request->block_id(),
            static_cast<BlockDataState>(old_state));
        response->set_success(false);
        response->set_error_message("Write operation failed, state rolled back");
        return grpc::Status::OK;
    }

    memory_pool_->renew_lease(request->block_id(), request->client_node_id());

    response->set_success(true);
    response->set_bytes_written(request->length());

    DMP_DEBUG("Put response: bytes_written={}", request->length());

    return grpc::Status::OK;
}

grpc::Status MemoryServiceImpl::Get(grpc::ServerContext* context,
                                     const dmp::GetRequest* request,
                                     dmp::GetResponse* response) {
    DMP_DEBUG("Get request: block_id={}, offset={}, length={}",
              request->block_id(), request->offset(), request->length());

    if (request->length() > MAX_TRANSFER_SIZE) {
        response->set_success(false);
        response->set_error_message("Transfer size exceeds maximum");
        return grpc::Status::OK;
    }

    auto block_info = memory_pool_->get_block_info(request->block_id());
    if (!block_info.success) {
        response->set_success(false);
        response->set_error_message(block_info.error_message);
        return grpc::Status::OK;
    }

    if (request->offset() + request->length() > memory_pool_->get_block_data_size(request->block_id())) {
        response->set_success(false);
        response->set_error_message("Read exceeds block bounds");
        return grpc::Status::OK;
    }

    BlockDataState data_state = memory_pool_->get_block_data_state(request->block_id());
    if (data_state != BlockDataState::WRITTEN) {
        std::string error_msg;
        switch (data_state) {
            case BlockDataState::INITIAL:
                error_msg = "Block has not been written to yet";
                break;
            case BlockDataState::WRITING:
                error_msg = "Block is currently being written to, please retry";
                break;
            case BlockDataState::WRITE_FAILED:
                error_msg = "Previous write to this block failed, data may be corrupted";
                break;
            default:
                error_msg = "Block data state is invalid for reading";
                break;
        }
        response->set_success(false);
        response->set_error_message(error_msg);
        return grpc::Status::OK;
    }

    uint64_t read_offset = block_info.value.data_offset + request->offset();
    void* src_ptr = static_cast<uint8_t*>(memory_base_) + read_offset;

    response->set_data(src_ptr, request->length());

    memory_pool_->renew_lease(request->block_id(), request->client_node_id());

    response->set_success(true);
    response->set_bytes_read(request->length());

    DMP_DEBUG("Get response: bytes_read={}", request->length());

    return grpc::Status::OK;
}

grpc::Status MemoryServiceImpl::Monitor(grpc::ServerContext* context,
                                         const dmp::MonitorRequest* request,
                                         dmp::MonitorResponse* response) {
    auto stats = memory_pool_->get_stats();

    auto* global_stats = response->mutable_global_stats();
    global_stats->set_total_capacity(stats.total_capacity);
    global_stats->set_used_capacity(stats.used_capacity);
    global_stats->set_free_capacity(stats.free_capacity);
    global_stats->set_usage_percent(stats.usage_percent);
    global_stats->set_fragmentation_ratio(stats.fragmentation_ratio);
    global_stats->set_total_blocks(stats.total_blocks);
    global_stats->set_allocated_blocks(stats.allocated_blocks);
    global_stats->set_free_blocks(stats.free_blocks);

    auto* node_info = response->add_nodes();
    node_info->set_node_id(config_.node_id);
    node_info->set_address(config_.grpc_address);
    node_info->set_port(config_.grpc_port);
    node_info->set_state(dmp::NODE_STATE_ONLINE);

    auto* node_stats = node_info->mutable_stats();
    node_stats->CopyFrom(*global_stats);

    if (request->detailed()) {
        auto leaked_blocks = memory_pool_->get_leaked_blocks();
        for (const auto& block : leaked_blocks) {
            auto* pb_block = response->add_leaked_blocks();
            pb_block->set_block_id(block.block_id);
            pb_block->set_offset(block.offset);
            pb_block->set_size(block.size);
            pb_block->set_state(static_cast<dmp::MemoryBlockState>(block.state));
            pb_block->set_owner_node_id(block.owner_node_id);
        }
    }

    return grpc::Status::OK;
}

grpc::Status MemoryServiceImpl::Heartbeat(grpc::ServerContext* context,
                                           const dmp::HeartbeatRequest* request,
                                           dmp::HeartbeatResponse* response) {
    NodeId node_id = request->node_id();

    {
        std::lock_guard<std::shared_mutex> lock(peers_mutex_);
        auto it = peers_.find(node_id);
        if (it == peers_.end()) {
            PeerInfo info;
            info.node_id = node_id;
            info.state = NodeState::Online;
            info.last_heartbeat = std::chrono::steady_clock::now();
            peers_[node_id] = info;

            DMP_INFO("New peer registered: node_id={}", node_id);
        } else {
            it->second.last_heartbeat = std::chrono::steady_clock::now();
            if (it->second.state == NodeState::Offline) {
                it->second.state = NodeState::Online;
                DMP_INFO("Peer came back online: node_id={}", node_id);
            }
        }
    }

    response->set_accepted(true);
    response->set_server_timestamp(current_timestamp_ms());

    return grpc::Status::OK;
}

grpc::Status MemoryServiceImpl::ReportNodeFailure(grpc::ServerContext* context,
                                                    const dmp::NodeFailureRequest* request,
                                                    dmp::NodeFailureResponse* response) {
    DMP_WARN("Node failure reported: failed_node_id={}, reason={}",
             request->failed_node_id(), request->reason());

    {
        std::lock_guard<std::shared_mutex> lock(peers_mutex_);
        auto it = peers_.find(request->failed_node_id());
        if (it != peers_.end()) {
            it->second.state = NodeState::Offline;
        }
    }

    auto recover_result = memory_pool_->recover_node(request->failed_node_id());

    response->set_success(true);
    response->set_recovered_blocks(0);
    response->set_recovered_memory(recover_result.value);

    DMP_INFO("Failure recovery: node_id={}, recovered_memory={}",
             request->failed_node_id(), recover_result.value);

    return grpc::Status::OK;
}

grpc::Status MemoryServiceImpl::Lock(grpc::ServerContext* context,
                                      const dmp::LockRequest* request,
                                      dmp::LockResponse* response) {
    DMP_DEBUG("Lock request: block_id={}, node_id={}, timeout={}ms",
              request->block_id(), request->client_node_id(), request->timeout_ms());

    uint64_t timeout_ms = request->timeout_ms() > 0
                             ? request->timeout_ms()
                             : LOCK_TIMEOUT_MS;

    auto result = lock_manager_->lock(request->block_id(),
                                       request->client_node_id(),
                                       timeout_ms);

    if (!result.success) {
        response->set_success(false);
        response->set_error_message(result.error_message);
        return grpc::Status::OK;
    }

    auto& lock_info = result.value;
    response->set_success(true);

    auto* pb_lock = response->mutable_lock();
    pb_lock->set_block_id(lock_info.block_id);
    pb_lock->set_owner_node_id(lock_info.owner_node_id);
    pb_lock->set_state(static_cast<dmp::LockState>(lock_info.state));
    pb_lock->set_acquire_count(lock_info.acquire_count);
    pb_lock->set_expired(lock_info.expired);

    DMP_DEBUG("Lock response: success, block_id={}", request->block_id());

    return grpc::Status::OK;
}

grpc::Status MemoryServiceImpl::Unlock(grpc::ServerContext* context,
                                        const dmp::UnlockRequest* request,
                                        dmp::UnlockResponse* response) {
    DMP_DEBUG("Unlock request: block_id={}, node_id={}, force={}",
              request->block_id(), request->client_node_id(), request->force());

    Result result;
    if (request->force()) {
        lock_manager_->force_unlock(request->block_id());
        result = Result::ok();
    } else {
        result = lock_manager_->unlock(request->block_id(),
                                        request->client_node_id());
    }

    if (!result.success) {
        response->set_success(false);
        response->set_error_message(result.error_message);
        return grpc::Status::OK;
    }

    response->set_success(true);

    DMP_DEBUG("Unlock response: success, block_id={}", request->block_id());

    return grpc::Status::OK;
}

grpc::Status MemoryServiceImpl::RenewLock(grpc::ServerContext* context,
                                           const dmp::RenewLockRequest* request,
                                           dmp::RenewLockResponse* response) {
    DMP_DEBUG("RenewLock request: block_id={}, node_id={}",
              request->block_id(), request->client_node_id());

    auto result = lock_manager_->renew_lock(request->block_id(),
                                             request->client_node_id());

    if (!result.success) {
        response->set_success(false);
        response->set_error_message(result.error_message);
        return grpc::Status::OK;
    }

    response->set_success(true);

    DMP_DEBUG("RenewLock response: success");

    return grpc::Status::OK;
}

grpc::Status MemoryServiceImpl::LockMonitor(grpc::ServerContext* context,
                                             const dmp::LockMonitorRequest* request,
                                             dmp::LockMonitorResponse* response) {
    auto stats = lock_manager_->get_stats();

    auto* pb_stats = response->mutable_stats();
    pb_stats->set_total_locks(stats.total_locks);
    pb_stats->set_active_locks(stats.active_locks);
    pb_stats->set_lock_acquisitions(stats.lock_acquisitions);
    pb_stats->set_lock_releases(stats.lock_releases);
    pb_stats->set_lock_timeouts(stats.lock_timeouts);
    pb_stats->set_lock_contentions(stats.lock_contentions);
    pb_stats->set_deadlocks_detected(stats.deadlocks_detected);
    pb_stats->set_avg_lock_hold_time_ms(stats.avg_lock_hold_time_ms);

    if (request->detailed()) {
        auto active_locks = lock_manager_->get_active_locks();
        for (const auto& lock_info : active_locks) {
            auto* pb_lock = response->add_active_locks();
            pb_lock->set_block_id(lock_info.block_id);
            pb_lock->set_owner_node_id(lock_info.owner_node_id);
            pb_lock->set_state(static_cast<dmp::LockState>(lock_info.state));
            pb_lock->set_acquire_count(lock_info.acquire_count);
            pb_lock->set_expired(lock_info.expired);
        }

        auto expired_locks = lock_manager_->get_expired_locks();
        for (const auto& lock_info : expired_locks) {
            auto* pb_lock = response->add_expired_locks();
            pb_lock->set_block_id(lock_info.block_id);
            pb_lock->set_owner_node_id(lock_info.owner_node_id);
            pb_lock->set_state(static_cast<dmp::LockState>(lock_info.state));
            pb_lock->set_acquire_count(lock_info.acquire_count);
            pb_lock->set_expired(lock_info.expired);
        }
    }

    return grpc::Status::OK;
}

}
