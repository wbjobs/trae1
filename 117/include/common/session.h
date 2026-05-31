#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <mutex>
#include <memory>
#include <chrono>
#include "crypto.h"
#include "udp_transport.h"
#include "circular_buffer.h"

namespace moshpp {

enum class SessionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RESUMING,
    RECONNECTING,
    CLOSED
};

struct TerminalState {
    uint32_t rows;
    uint32_t cols;
    uint32_t cursor_x;
    uint32_t cursor_y;
    std::vector<uint8_t> screen_buffer;
    uint64_t frame_number;
};

class Session {
public:
    Session(const std::string& session_id);
    ~Session();

    const std::string& get_id() const { return session_id_; }
    SessionState get_state() const { return state_; }
    void set_state(SessionState state) { state_ = state; }

    Crypto& get_crypto() { return crypto_; }
    TerminalState& get_terminal_state() { return terminal_state_; }
    void set_terminal_state(const TerminalState& state) { terminal_state_ = state; }

    void set_pty_fd(int fd) { pty_fd_ = fd; }
    int get_pty_fd() const { return pty_fd_; }

    void set_user_id(const std::string& user_id) { user_id_ = user_id; }
    const std::string& get_user_id() const { return user_id_; }

    void set_created_time(uint64_t time) { created_time_ = time; }
    uint64_t get_created_time() const { return created_time_; }

    void set_last_activity(uint64_t time) { last_activity_ = time; }
    uint64_t get_last_activity() const { return last_activity_; }

    void set_endpoint(const NetworkEndpoint& endpoint) { endpoint_ = endpoint; }
    const NetworkEndpoint& get_endpoint() const { return endpoint_; }

    uint32_t get_next_seq_num() { return next_seq_num_++; }
    uint32_t get_next_ack_num() { return next_ack_num_++; }

    void set_last_seq_num(uint32_t seq) { last_seq_num_ = seq; }
    uint32_t get_last_seq_num() const { return last_seq_num_; }

    void set_last_ack_num(uint32_t ack) { last_ack_num_ = ack; }
    uint32_t get_last_ack_num() const { return last_ack_num_; }

    void buffer_data(const std::vector<uint8_t>& data);
    std::vector<uint8_t> get_buffered_data();
    bool has_buffered_data() const { return !send_buffer_.empty(); }

    void update_screen_state(const std::vector<uint8_t>& data);
    std::vector<uint8_t> serialize_screen_state();
    void restore_screen_state(const std::vector<uint8_t>& data);

    bool is_tmux_attached() const { return tmux_attached_; }
    void set_tmux_attached(bool attached) { tmux_attached_ = attached; }
    const std::string& get_tmux_session() const { return tmux_session_name_; }
    void set_tmux_session(const std::string& name) { tmux_session_name_ = name; }

    void start_screen_integration();
    void stop_screen_integration();
    std::string capture_screen();
    void restore_screen(const std::string& screen_data);

    OutputBuffer& get_output_buffer() { return output_buffer_; }
    void append_output(const std::vector<uint8_t>& data);
    void append_output(const std::string& data);
    size_t get_output_offset() const { return output_buffer_.get_total_bytes(); }

    uint64_t get_last_snapshot_id() const { return last_snapshot_id_; }
    void set_last_snapshot_id(uint64_t id) { last_snapshot_id_ = id; }

    uint64_t get_client_last_offset() const { return client_last_offset_; }
    void set_client_last_offset(uint64_t offset) { client_last_offset_ = offset; }

private:
    std::string session_id_;
    SessionState state_;
    Crypto crypto_;
    TerminalState terminal_state_;
    NetworkEndpoint endpoint_;

    int pty_fd_;
    std::string user_id_;
    uint64_t created_time_;
    uint64_t last_activity_;

    uint32_t next_seq_num_;
    uint32_t next_ack_num_;
    uint32_t last_seq_num_;
    uint32_t last_ack_num_;

    std::vector<uint8_t> send_buffer_;
    std::vector<uint8_t> receive_buffer_;
    std::mutex buffer_mutex_;

    bool tmux_attached_;
    std::string tmux_session_name_;
    bool screen_integration_enabled_;

    std::map<uint32_t, std::vector<uint8_t>> frame_cache_;
    std::mutex frame_mutex_;

    OutputBuffer output_buffer_;
    uint64_t last_snapshot_id_;
    uint64_t client_last_offset_;
};

class SessionManager {
public:
    SessionManager();
    ~SessionManager();

    std::shared_ptr<Session> create_session();
    std::shared_ptr<Session> create_session(const std::string& session_id);
    std::shared_ptr<Session> get_session(const std::string& session_id);
    bool session_exists(const std::string& session_id) const;
    void remove_session(const std::string& session_id);
    void cleanup_inactive_sessions(uint64_t timeout_ms);

    size_t get_session_count() const;
    std::vector<std::string> get_all_session_ids() const;

private:
    std::map<std::string, std::shared_ptr<Session>> sessions_;
    mutable std::mutex sessions_mutex_;
};

}
