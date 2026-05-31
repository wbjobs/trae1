#pragma once

#include "common/types.h"
#include "common/utils.h"
#include "memory/memory_pool.h"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>

namespace dmp {

class DistributedLockManager {
public:
    using LockTimeoutCallback = std::function<void(BlockId, NodeId)>;
    using DeadlockCallback = std::function<void(const std::vector<BlockId>&)>;

    DistributedLockManager();
    ~DistributedLockManager();

    bool initialize(MemoryPool* memory_pool);

    void shutdown();

    ResultT<LockInfo> lock(BlockId block_id, NodeId node_id,
                           uint64_t timeout_ms = LOCK_TIMEOUT_MS);

    Result unlock(BlockId block_id, NodeId node_id);

    ResultT<LockInfo> try_lock(BlockId block_id, NodeId node_id);

    Result renew_lock(BlockId block_id, NodeId node_id);

    ResultT<LockInfo> get_lock_info(BlockId block_id) const;

    bool is_locked(BlockId block_id) const;
    bool is_locked_by(BlockId block_id, NodeId node_id) const;

    LockStats get_stats() const;

    std::vector<LockInfo> get_active_locks() const;
    std::vector<LockInfo> get_expired_locks() const;

    void set_lock_timeout_callback(LockTimeoutCallback callback);
    void set_deadlock_callback(DeadlockCallback callback);

    void force_unlock(BlockId block_id);

    bool detect_deadlock() const;

private:
    void lock_check_task();
    void check_lock_timeouts();
    bool check_potential_deadlock() const;

    mutable std::shared_mutex mutex_;
    MemoryPool* memory_pool_;
    std::unordered_map<BlockId, LockInfo> locks_;
    std::unique_ptr<std::thread> check_thread_;
    std::atomic<bool> running_{false};

    LockTimeoutCallback lock_timeout_callback_;
    DeadlockCallback deadlock_callback_;

    mutable std::atomic<uint64_t> total_locks_{0};
    mutable std::atomic<uint64_t> lock_acquisitions_{0};
    mutable std::atomic<uint64_t> lock_releases_{0};
    mutable std::atomic<uint64_t> lock_timeouts_{0};
    mutable std::atomic<uint64_t> lock_contentions_{0};
    mutable std::atomic<uint64_t> deadlocks_detected_{0};
    mutable std::atomic<uint64_t> total_lock_hold_time_us_{0};
};

}
