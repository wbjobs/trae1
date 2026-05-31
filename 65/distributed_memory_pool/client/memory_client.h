#pragma once

#include "common/types.h"
#include "common/utils.h"
#include "memory_service.grpc.pb.h"
#include <grpc++/grpc++.h>
#include <string>
#include <memory>
#include <vector>
#include <mutex>

namespace dmp {

class MemoryClient {
public:
    MemoryClient();
    ~MemoryClient();

    bool connect(const std::string& server_address, uint32_t port);

    void disconnect();

    ResultT<BlockInfo> allocate(uint64_t size, NodeId client_node_id,
                                bool exclusive = false);

    Result release(BlockId block_id, NodeId client_node_id);

    Result put(BlockId block_id, uint64_t offset, const void* data,
               uint64_t length, NodeId client_node_id);

    ResultT<std::vector<uint8_t>> get(BlockId block_id, uint64_t offset,
                                       uint64_t length, NodeId client_node_id);

    ResultT<Stats> monitor();

    Result heartbeat(NodeId node_id, const Stats& stats);

    Result report_node_failure(NodeId failed_node_id, const std::string& reason);

    ResultT<LockInfo> lock(BlockId block_id, NodeId client_node_id,
                           uint64_t timeout_ms = LOCK_TIMEOUT_MS);

    ResultT<LockInfo> try_lock(BlockId block_id, NodeId client_node_id);

    Result unlock(BlockId block_id, NodeId client_node_id, bool force = false);

    Result renew_lock(BlockId block_id, NodeId client_node_id);

    ResultT<LockInfo> get_lock_info(BlockId block_id);

    ResultT<LockStats> lock_monitor(bool detailed = false);

    bool is_connected() const { return connected_.load(std::memory_order_acquire); }

    static bool is_data_state_error(const std::string& error_msg);
    static bool is_writing_error(const std::string& error_msg);
    static bool is_write_failed_error(const std::string& error_msg);
    static bool is_initial_state_error(const std::string& error_msg);

private:
    std::unique_ptr<dmp::MemoryService::Stub> stub_;
    std::shared_ptr<grpc::Channel> channel_;
    std::atomic<bool> connected_{false};
    std::mutex mutex_;
};

}
