#include "session_manager.h"
#include "protocol.h"
#include <algorithm>

namespace sgxagg { namespace host {

SessionManager& SessionManager::instance() {
    static SessionManager s_inst;
    return s_inst;
}

void SessionManager::start_reaper() {
    if (running_.exchange(true)) return;
    reaper_ = std::thread([this]() { reap_loop(); });
}

void SessionManager::stop_reaper() {
    if (!running_.exchange(false)) return;
    if (reaper_.joinable()) reaper_.join();
}

uint64_t SessionManager::create_session(const uint8_t key[32], uint32_t ttl_override) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (sessions_.size() >= kMaxSessions) {
        evict_lru_locked();
    }
    uint64_t id = next_id_++;
    Session s;
    s.id = id;
    std::copy_n(key, 32, s.key.begin());
    s.has_key = true;
    s.created = std::chrono::steady_clock::now();
    s.last_used = s.created;
    s.ttl_seconds = ttl_override ? ttl_override : default_ttl_;
    sessions_.emplace(id, s);
    lru_order_.push_front(id);
    return id;
}

uint64_t SessionManager::create_session_dry() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (sessions_.size() >= kMaxSessions) {
        evict_lru_locked();
    }
    uint64_t id = next_id_++;
    Session s;
    s.id = id;
    s.has_key = false;
    s.created = std::chrono::steady_clock::now();
    s.last_used = s.created;
    s.ttl_seconds = default_ttl_;
    sessions_.emplace(id, s);
    lru_order_.push_front(id);
    return id;
}

bool SessionManager::install_session_key(uint64_t id, const uint8_t key[32], uint32_t ttl_override) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return false;
    std::copy_n(key, 32, it->second.key.begin());
    it->second.has_key = true;
    if (ttl_override) it->second.ttl_seconds = ttl_override;
    it->second.created = std::chrono::steady_clock::now();
    it->second.last_used = it->second.created;
    return true;
}

Session* SessionManager::get_session(uint64_t id) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return nullptr;
    if (!it->second.has_key) return nullptr;  // 还没安装密钥的预留会话不可用

    // 检查是否过期
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.created).count();
    if ((uint32_t)age > it->second.ttl_seconds) {
        sessions_.erase(it);
        lru_order_.remove(id);
        return nullptr;
    }

    // 更新 LRU：移动到最前面
    lru_order_.remove(id);
    lru_order_.push_front(id);
    it->second.last_used = now;
    return &it->second;
}

void SessionManager::remove_session(uint64_t id) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = sessions_.find(id);
    if (it != sessions_.end()) {
        sessions_.erase(it);
        lru_order_.remove(id);
    }
}

size_t SessionManager::size() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return sessions_.size();
}

void SessionManager::evict_lru_locked() {
    if (lru_order_.empty()) return;
    uint64_t oldest = lru_order_.back();
    lru_order_.pop_back();
    sessions_.erase(oldest);
}

void SessionManager::reap_loop() {
    using namespace std::chrono_literals;
    while (running_) {
        std::this_thread::sleep_for(60s);  // 每分钟清理一次
        std::lock_guard<std::mutex> lk(mtx_);
        auto now = std::chrono::steady_clock::now();
        for (auto it = sessions_.begin(); it != sessions_.end(); ) {
            bool expired = false;
            if (it->second.has_key) {
                // 有密钥的会话：按 TTL 过期
                auto age = std::chrono::duration_cast<std::chrono::seconds>(
                    now - it->second.created).count();
                if ((uint32_t)age > it->second.ttl_seconds) expired = true;
            } else {
                // 预留会话（还没装密钥）：超过 60 秒未完成握手则清理
                auto age = std::chrono::duration_cast<std::chrono::seconds>(
                    now - it->second.created).count();
                if ((uint32_t)age > 60) expired = true;
            }
            if (expired) {
                uint64_t id = it->first;
                it = sessions_.erase(it);
                lru_order_.remove(id);
            } else {
                ++it;
            }
        }
    }
}

}} // sgxagg::host
