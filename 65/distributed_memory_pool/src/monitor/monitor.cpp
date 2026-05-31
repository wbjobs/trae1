#include "monitor/monitor.h"
#include <sstream>
#include <iomanip>
#include <thread>

namespace dmp {

Monitor::Monitor()
    : memory_pool_(nullptr)
{
    memset(&current_stats_, 0, sizeof(current_stats_));
}

Monitor::~Monitor() {
    shutdown();
}

bool Monitor::initialize(MemoryPool* memory_pool, uint32_t port) {
    memory_pool_ = memory_pool;

    collect_stats();

    running_.store(true, std::memory_order_release);
    collection_thread_ = std::make_unique<std::thread>(&Monitor::collection_task, this);

    DMP_INFO("Monitor initialized, collection interval={}ms", collection_interval_ms_);

    return true;
}

void Monitor::shutdown() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    if (collection_thread_ && collection_thread_->joinable()) {
        collection_thread_->join();
    }

    DMP_INFO("Monitor shut down");
}

void Monitor::collection_task() {
    DMP_INFO("Stats collection task started");

    while (running_.load(std::memory_order_acquire)) {
        collect_stats();

        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            for (const auto& callback : callbacks_) {
                callback(current_stats_);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(collection_interval_ms_));
    }

    DMP_INFO("Stats collection task stopped");
}

void Monitor::collect_stats() {
    if (!memory_pool_) {
        return;
    }

    Stats stats = memory_pool_->get_stats();

    {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        current_stats_ = stats;
    }
}

Stats Monitor::get_stats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return current_stats_;
}

std::string Monitor::get_stats_json() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{";
    oss << "\"total_capacity\":" << current_stats_.total_capacity << ",";
    oss << "\"used_capacity\":" << current_stats_.used_capacity << ",";
    oss << "\"free_capacity\":" << current_stats_.free_capacity << ",";
    oss << "\"usage_percent\":" << current_stats_.usage_percent << ",";
    oss << "\"fragmentation_ratio\":" << current_stats_.fragmentation_ratio << ",";
    oss << "\"total_blocks\":" << current_stats_.total_blocks << ",";
    oss << "\"allocated_blocks\":" << current_stats_.allocated_blocks << ",";
    oss << "\"free_blocks\":" << current_stats_.free_blocks;
    oss << "}";

    return oss.str();
}

void Monitor::register_stats_callback(StatsCallback callback) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    callbacks_.push_back(std::move(callback));
}

void Monitor::set_collection_interval(uint64_t interval_ms) {
    collection_interval_ms_ = interval_ms;
    DMP_INFO("Collection interval changed to {}ms", interval_ms);
}

double Monitor::get_usage_percent() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return current_stats_.usage_percent;
}

double Monitor::get_fragmentation_ratio() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return current_stats_.fragmentation_ratio;
}

uint64_t Monitor::get_used_capacity() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return current_stats_.used_capacity;
}

uint64_t Monitor::get_free_capacity() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return current_stats_.free_capacity;
}

uint64_t Monitor::get_allocated_blocks() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return current_stats_.allocated_blocks;
}

uint64_t Monitor::get_free_blocks() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return current_stats_.free_blocks;
}

}
