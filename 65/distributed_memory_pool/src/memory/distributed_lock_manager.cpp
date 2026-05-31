#include "memory/distributed_lock_manager.h"
#include <algorithm>

namespace dmp {

DistributedLockManager::DistributedLockManager()
    : memory_pool_(nullptr)
{
}

DistributedLockManager::~DistributedLockManager() {
    shutdown();
}

bool DistributedLockManager::initialize(MemoryPool* memory_pool) {
    if (!memory_pool) {
        DMP_ERROR("Memory pool is null");
        return false;
    }

    memory_pool_ = memory_pool;

    running_.store(true, std::memory_order_release);
    check_thread_ = std::make_unique<std::thread>(&DistributedLockManager::lock_check_task, this);

    DMP_INFO("DistributedLockManager initialized");
    return true;
}

void DistributedLockManager::shutdown() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    if (check_thread_ && check_thread_->joinable()) {
        check_thread_->join();
    }

    {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        for (auto& [block_id, lock_info] : locks_) {
            if (memory_pool_) {
                void* lock_ptr = static_cast<uint8_t*>(memory_pool_->get_base_address()) +
                                 lock_info.block_id;
                LockState* state_ptr = reinterpret_cast<LockState*>(
                    static_cast<uint8_t*>(memory_pool_->get_base_address()) +
                    memory_pool_->get_block_info(block_id).value.offset +
                    BLOCK_LOCK_STATE_OFFSET);
                if (state_ptr) {
                    *state_ptr = LockState::UNLOCKED;
                }
            }
        }
        locks_.clear();
    }

    DMP_INFO("DistributedLockManager shut down");
}

ResultT<LockInfo> DistributedLockManager::lock(BlockId block_id, NodeId node_id,
                                                 uint64_t timeout_ms) {
    auto start = std::chrono::steady_clock::now();

    while (true) {
        auto result = try_lock(block_id, node_id);
        if (result.success) {
            return result;
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >
            static_cast<int64_t>(timeout_ms)) {
            lock_contentions_.fetch_add(1, std::memory_order_relaxed);
            return ResultT<LockInfo>::error("Lock acquisition timeout for block " +
                                             std::to_string(block_id));
        }

        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

ResultT<LockInfo> DistributedLockManager::try_lock(BlockId block_id, NodeId node_id) {
    auto block_info = memory_pool_->get_block_info(block_id);
    if (!block_info.success) {
        return ResultT<LockInfo>::error("Block not found: " + std::to_string(block_id));
    }

    {
        std::lock_guard<std::shared_mutex> lock(mutex_);

        auto it = locks_.find(block_id);
        if (it != locks_.end() && it->second.state == LockState::LOCKED) {
            if (it->second.owner_node_id == node_id) {
                it->second.acquire_count++;
                it->second.last_renew_time = std::chrono::steady_clock::now();
                return ResultT<LockInfo>::ok(it->second);
            }

            lock_contentions_.fetch_add(1, std::memory_order_relaxed);
            return ResultT<LockInfo>::error("Lock already held by node " +
                                             std::to_string(it->second.owner_node_id));
        }

        void* lock_state_ptr = static_cast<uint8_t*>(memory_pool_->get_base_address()) +
                               block_info.value.offset + BLOCK_LOCK_STATE_OFFSET;

        LockState* state_ptr = reinterpret_cast<LockState*>(lock_state_ptr);
        LockState expected = LockState::UNLOCKED;
        LockState desired = LockState::LOCKED;

        if (!__atomic_compare_exchange_n(
                reinterpret_cast<uint64_t*>(state_ptr),
                reinterpret_cast<uint64_t*>(&expected),
                static_cast<uint64_t>(desired),
                false,
                __ATOMIC_SEQ_CST,
                __ATOMIC_SEQ_CST)) {
            lock_contentions_.fetch_add(1, std::memory_order_relaxed);
            return ResultT<LockInfo>::error("Lock contention for block " +
                                             std::to_string(block_id));
        }

        LockInfo lock_info{};
        lock_info.block_id = block_id;
        lock_info.owner_node_id = node_id;
        lock_info.state = LockState::LOCKED;
        lock_info.acquire_time = std::chrono::steady_clock::now();
        lock_info.last_renew_time = lock_info.acquire_time;
        lock_info.acquire_count = 1;
        lock_info.expired = false;

        locks_[block_id] = lock_info;

        lock_acquisitions_.fetch_add(1, std::memory_order_relaxed);
        total_locks_.fetch_add(1, std::memory_order_relaxed);

        DMP_DEBUG("Lock acquired: block={}, node={}", block_id, node_id);

        return ResultT<LockInfo>::ok(lock_info);
    }
}

Result DistributedLockManager::unlock(BlockId block_id, NodeId node_id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);

    auto it = locks_.find(block_id);
    if (it == locks_.end()) {
        return Result::error("Lock not found for block " + std::to_string(block_id));
    }

    if (it->second.owner_node_id != node_id) {
        return Result::error("Lock held by different node");
    }

    if (it->second.acquire_count > 1) {
        it->second.acquire_count--;
        return Result::ok();
    }

    auto block_info = memory_pool_->get_block_info(block_id);
    if (block_info.success) {
        void* lock_state_ptr = static_cast<uint8_t*>(memory_pool_->get_base_address()) +
                               block_info.value.offset + BLOCK_LOCK_STATE_OFFSET;

        LockState* state_ptr = reinterpret_cast<LockState*>(lock_state_ptr);
        *state_ptr = LockState::UNLOCKED;
    }

    auto hold_time = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - it->second.acquire_time).count();
    total_lock_hold_time_us_.fetch_add(hold_time, std::memory_order_relaxed);

    locks_.erase(it);

    lock_releases_.fetch_add(1, std::memory_order_relaxed);

    DMP_DEBUG("Lock released: block={}, node={}", block_id, node_id);

    return Result::ok();
}

Result DistributedLockManager::renew_lock(BlockId block_id, NodeId node_id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);

    auto it = locks_.find(block_id);
    if (it == locks_.end()) {
        return Result::error("Lock not found");
    }

    if (it->second.owner_node_id != node_id) {
        return Result::error("Lock held by different node");
    }

    it->second.last_renew_time = std::chrono::steady_clock::now();
    it->second.expired = false;

    return Result::ok();
}

ResultT<LockInfo> DistributedLockManager::get_lock_info(BlockId block_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = locks_.find(block_id);
    if (it == locks_.end()) {
        return ResultT<LockInfo>::error("Lock not found");
    }

    return ResultT<LockInfo>::ok(it->second);
}

bool DistributedLockManager::is_locked(BlockId block_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = locks_.find(block_id);
    return it != locks_.end() && it->second.state == LockState::LOCKED;
}

bool DistributedLockManager::is_locked_by(BlockId block_id, NodeId node_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = locks_.find(block_id);
    return it != locks_.end() &&
           it->second.state == LockState::LOCKED &&
           it->second.owner_node_id == node_id;
}

LockStats DistributedLockManager::get_stats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    LockStats stats{};
    stats.total_locks = total_locks_.load(std::memory_order_relaxed);
    stats.active_locks = locks_.size();
    stats.lock_acquisitions = lock_acquisitions_.load(std::memory_order_relaxed);
    stats.lock_releases = lock_releases_.load(std::memory_order_relaxed);
    stats.lock_timeouts = lock_timeouts_.load(std::memory_order_relaxed);
    stats.lock_contentions = lock_contentions_.load(std::memory_order_relaxed);
    stats.deadlocks_detected = deadlocks_detected_.load(std::memory_order_relaxed);

    uint64_t total_holds = lock_acquisitions_.load(std::memory_order_relaxed) +
                          lock_releases_.load(std::memory_order_relaxed);
    if (total_holds > 0) {
        stats.avg_lock_hold_time_ms =
            static_cast<double>(total_lock_hold_time_us_.load(std::memory_order_relaxed)) /
            total_holds / 1000.0;
    }

    return stats;
}

std::vector<LockInfo> DistributedLockManager::get_active_locks() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::vector<LockInfo> result;
    for (const auto& [block_id, lock_info] : locks_) {
        if (lock_info.state == LockState::LOCKED) {
            result.push_back(lock_info);
        }
    }
    return result;
}

std::vector<LockInfo> DistributedLockManager::get_expired_locks() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::vector<LockInfo> result;
    auto now = std::chrono::steady_clock::now();

    for (const auto& [block_id, lock_info] : locks_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lock_info.last_renew_time).count();
        if (elapsed > static_cast<int64_t>(LOCK_TIMEOUT_MS)) {
            result.push_back(lock_info);
        }
    }
    return result;
}

void DistributedLockManager::set_lock_timeout_callback(LockTimeoutCallback callback) {
    lock_timeout_callback_ = std::move(callback);
}

void DistributedLockManager::set_deadlock_callback(DeadlockCallback callback) {
    deadlock_callback_ = std::move(callback);
}

void DistributedLockManager::force_unlock(BlockId block_id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);

    auto it = locks_.find(block_id);
    if (it != locks_.end()) {
        auto block_info = memory_pool_->get_block_info(block_id);
        if (block_info.success) {
            void* lock_state_ptr = static_cast<uint8_t*>(memory_pool_->get_base_address()) +
                                   block_info.value.offset + BLOCK_LOCK_STATE_OFFSET;

            LockState* state_ptr = reinterpret_cast<LockState*>(lock_state_ptr);
            *state_ptr = LockState::UNLOCKED;
        }

        auto hold_time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - it->second.acquire_time).count();
        total_lock_hold_time_us_.fetch_add(hold_time, std::memory_order_relaxed);

        locks_.erase(it);
        lock_timeouts_.fetch_add(1, std::memory_order_relaxed);

        DMP_WARN("Force unlocked: block={}", block_id);
    }
}

bool DistributedLockManager::detect_deadlock() const {
    return check_potential_deadlock();
}

void DistributedLockManager::lock_check_task() {
    DMP_INFO("Lock check task started");

    while (running_.load(std::memory_order_acquire)) {
        check_lock_timeouts();

        if (detect_deadlock()) {
            DMP_WARN("Potential deadlock detected");
            deadlocks_detected_.fetch_add(1, std::memory_order_relaxed);

            auto expired = get_expired_locks();
            if (deadlock_callback_ && !expired.empty()) {
                std::vector<BlockId> block_ids;
                for (const auto& lock_info : expired) {
                    block_ids.push_back(lock_info.block_id);
                }
                deadlock_callback_(block_ids);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(LOCK_CHECK_INTERVAL_MS));
    }

    DMP_INFO("Lock check task stopped");
}

void DistributedLockManager::check_lock_timeouts() {
    std::lock_guard<std::shared_mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    std::vector<BlockId> to_timeout;

    for (auto& [block_id, lock_info] : locks_) {
        if (lock_info.state == LockState::LOCKED && !lock_info.expired) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lock_info.last_renew_time).count();

            if (elapsed > static_cast<int64_t>(LOCK_TIMEOUT_MS)) {
                lock_info.expired = true;
                to_timeout.push_back(block_id);

                DMP_WARN("Lock timeout: block={}, node={}", block_id, lock_info.owner_node_id);
            }

            auto hold_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lock_info.acquire_time).count();
            if (hold_elapsed > static_cast<int64_t>(LOCK_MAX_HOLD_TIME_MS)) {
                DMP_WARN("Lock exceeded max hold time: block={}, node={}",
                         block_id, lock_info.owner_node_id);
                to_timeout.push_back(block_id);
            }
        }
    }

    for (auto block_id : to_timeout) {
        auto it = locks_.find(block_id);
        if (it != locks_.end()) {
            auto block_info = memory_pool_->get_block_info(block_id);
            if (block_info.success) {
                void* lock_state_ptr = static_cast<uint8_t*>(memory_pool_->get_base_address()) +
                                       block_info.value.offset + BLOCK_LOCK_STATE_OFFSET;

                LockState* state_ptr = reinterpret_cast<LockState*>(lock_state_ptr);
                *state_ptr = LockState::UNLOCKED;
            }

            if (lock_timeout_callback_) {
                lock_timeout_callback_(block_id, it->second.owner_node_id);
            }

            locks_.erase(it);
            lock_timeouts_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

bool DistributedLockManager::check_potential_deadlock() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    if (locks_.empty()) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    uint64_t expired_count = 0;

    for (const auto& [block_id, lock_info] : locks_) {
        if (lock_info.state == LockState::LOCKED) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lock_info.last_renew_time).count();
            if (elapsed > static_cast<int64_t>(LOCK_TIMEOUT_MS)) {
                expired_count++;
            }
        }
    }

    return expired_count > 0 && expired_count >= locks_.size() / 2;
}

}
