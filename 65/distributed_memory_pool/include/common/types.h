#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <chrono>
#include <vector>
#include <mutex>
#include <shared_mutex>

namespace dmp {

using NodeId = uint64_t;
using BlockId = uint64_t;
using RKey = uint32_t;

constexpr uint64_t INVALID_NODE_ID = UINT64_MAX;
constexpr uint64_t INVALID_BLOCK_ID = UINT64_MAX;
constexpr uint32_t INVALID_RKEY = 0;

constexpr uint64_t MIN_BLOCK_SIZE = 4 * 1024;
constexpr uint64_t MAX_TRANSFER_SIZE = 1024 * 1024;
constexpr uint64_t TOTAL_CAPACITY = 64ULL * 1024 * 1024 * 1024;

constexpr size_t SLAB_CLASS_COUNT = 8;
constexpr uint64_t SLAB_SIZES[SLAB_CLASS_COUNT] = {
    4 * 1024,
    8 * 1024,
    16 * 1024,
    32 * 1024,
    64 * 1024,
    128 * 1024,
    256 * 1024,
    1024 * 1024
};

constexpr uint64_t HEARTBEAT_INTERVAL_MS = 1000;
constexpr uint64_t HEARTBEAT_TIMEOUT_MS = 5000;
constexpr uint64_t LEASE_DURATION_MS = 30000;

constexpr uint64_t LOCK_TIMEOUT_MS = 10000;
constexpr uint64_t LOCK_CHECK_INTERVAL_MS = 1000;
constexpr uint64_t LOCK_MAX_HOLD_TIME_MS = 30000;

constexpr uint32_t DEFAULT_GRPC_PORT = 50051;
constexpr uint16_t DEFAULT_RDMA_PORT = 4791;
constexpr int DEFAULT_RDMA_CQ_SIZE = 64;
constexpr int DEFAULT_RDMA_SRQ_SIZE = 64;
constexpr int DEFAULT_RDMA_QP_SIZE = 64;

enum class BlockState : uint8_t {
    Free = 0,
    Allocated = 1,
    Fault = 2
};

constexpr uint64_t BLOCK_DATA_STATE_OFFSET = 0;
constexpr uint64_t BLOCK_DATA_HEADER_SIZE = 8;

constexpr uint64_t BLOCK_LOCK_STATE_OFFSET = 8;
constexpr uint64_t BLOCK_LOCK_SIZE = 8;
constexpr uint64_t BLOCK_HEADER_TOTAL = 16;

enum class BlockDataState : uint64_t {
    INITIAL = 0,
    WRITING = 1,
    WRITTEN = 2,
    WRITE_FAILED = 3
};

enum class LockState : uint64_t {
    UNLOCKED = 0,
    LOCKED = 1,
    CONTENTION = 2
};

struct LockInfo {
    BlockId block_id;
    NodeId owner_node_id;
    LockState state;
    std::chrono::steady_clock::time_point acquire_time;
    std::chrono::steady_clock::time_point last_renew_time;
    uint64_t acquire_count;
    bool expired;
};

struct LockStats {
    uint64_t total_locks;
    uint64_t active_locks;
    uint64_t lock_acquisitions;
    uint64_t lock_releases;
    uint64_t lock_timeouts;
    uint64_t lock_contentions;
    uint64_t deadlocks_detected;
    double avg_lock_hold_time_ms;
};

enum class NodeState : uint8_t {
    Online = 0,
    Offline = 1,
    Degraded = 2
};

struct MemoryRegion {
    uint64_t addr;
    uint64_t length;
    uint32_t rkey;
    uint32_t lkey;
};

struct BlockInfo {
    BlockId block_id;
    uint64_t offset;
    uint64_t size;
    uint64_t data_offset;
    BlockState state;
    NodeId owner_node_id;
    std::chrono::steady_clock::time_point lease_expiry;
    uint32_t rkey;
    uint64_t remote_addr;
};

struct NodeInfo {
    NodeId node_id;
    std::string address;
    uint32_t port;
    NodeState state;
    std::chrono::steady_clock::time_point last_heartbeat;
    uint64_t total_memory;
    uint64_t used_memory;
};

struct Stats {
    uint64_t total_capacity;
    uint64_t used_capacity;
    uint64_t free_capacity;
    double usage_percent;
    double fragmentation_ratio;
    uint64_t total_blocks;
    uint64_t allocated_blocks;
    uint64_t free_blocks;
};

struct Result {
    bool success;
    std::string error_message;

    static Result ok() { return {true, ""}; }
    static Result error(const std::string& msg) { return {false, msg}; }
};

template<typename T>
struct ResultT {
    bool success;
    std::string error_message;
    T value;

    static ResultT ok(T&& v) { return {true, "", std::move(v)}; }
    static ResultT ok(const T& v) { return {true, "", v}; }
    static ResultT error(const std::string& msg) { return {false, msg, T{}}; }
};

inline uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

inline uint64_t align_down(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1);
}

inline bool is_aligned(uint64_t value, uint64_t alignment) {
    return (value & (alignment - 1)) == 0;
}

}
