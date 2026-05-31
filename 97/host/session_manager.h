#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <chrono>
#include <unordered_map>
#include <list>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>

namespace sgxagg {
namespace host {

// 会话信息：会话管理在 host（非 Enclave）完成，会话密钥派生在 Enclave 内
struct Session {
    uint64_t id = 0;
    std::array<uint8_t, 32> key;   // AES-256-GCM 密钥
    std::chrono::steady_clock::time_point created;
    std::chrono::steady_clock::time_point last_used;
    uint32_t ttl_seconds = 0;      // 本会话的 TTL
    bool has_key = false;          // 预留会话还没安装密钥时为 false
};

class SessionManager
{
public:
    static SessionManager& instance();

    void set_default_ttl(uint32_t ttl_seconds) { default_ttl_ = ttl_seconds; }
    uint32_t default_ttl() const { return default_ttl_; }

    void start_reaper();
    void stop_reaper();

    // 创建新会话，返回 session_id；如果超过 kMaxSessions 则淘汰最旧的
    uint64_t create_session(const uint8_t key[32], uint32_t ttl_override = 0);

    // 预留一个 session_id（不带密钥），后续通过 install_session_key 安装
    uint64_t create_session_dry();

    // 为已预留的 session_id 安装密钥（用于两步握手）
    bool install_session_key(uint64_t id, const uint8_t key[32], uint32_t ttl_override = 0);

    // 获取会话（若过期或不存在返回 nullptr），同时更新 last_used
    Session* get_session(uint64_t id);

    // 删除会话
    void remove_session(uint64_t id);

    // 当前会话数
    size_t size() const;

private:
    SessionManager() = default;
    ~SessionManager() { stop_reaper(); }

    void reap_loop();
    void evict_lru_locked();

    mutable std::mutex mtx_;
    std::unordered_map<uint64_t, Session> sessions_;
    std::list<uint64_t> lru_order_;   // 前面是最近用过
    std::thread reaper_;
    std::atomic<bool> running_{false};
    uint32_t default_ttl_ = 24 * 60 * 60;
    uint64_t next_id_ = 1;
};

} // namespace host
} // namespace sgxagg
