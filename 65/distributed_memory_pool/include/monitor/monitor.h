#pragma once

#include "common/types.h"
#include "common/utils.h"
#include "memory/memory_pool.h"
#include <string>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>

namespace dmp {

class Monitor {
public:
    using StatsCallback = std::function<void(const Stats&)>;

    Monitor();
    ~Monitor();

    bool initialize(MemoryPool* memory_pool, uint32_t port = 0);

    void shutdown();

    Stats get_stats() const;

    std::string get_stats_json() const;

    void register_stats_callback(StatsCallback callback);

    void set_collection_interval(uint64_t interval_ms);

    double get_usage_percent() const;
    double get_fragmentation_ratio() const;
    uint64_t get_used_capacity() const;
    uint64_t get_free_capacity() const;
    uint64_t get_allocated_blocks() const;
    uint64_t get_free_blocks() const;

private:
    void collection_task();
    void collect_stats();

    MemoryPool* memory_pool_;
    mutable std::shared_mutex mutex_;
    Stats current_stats_;
    std::vector<StatsCallback> callbacks_;
    std::unique_ptr<std::thread> collection_thread_;
    std::atomic<bool> running_{false};
    uint64_t collection_interval_ms_{1000};
};

}
