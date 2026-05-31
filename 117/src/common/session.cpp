#include "common/session.h"
#include "common/utils.h"
#include <cstring>
#include <sstream>

namespace moshpp {

Session::Session(const std::string& session_id)
    : session_id_(session_id)
    , state_(SessionState::DISCONNECTED)
    , pty_fd_(-1)
    , created_time_(current_time_ms())
    , last_activity_(current_time_ms())
    , next_seq_num_(1)
    , next_ack_num_(1)
    , last_seq_num_(0)
    , last_ack_num_(0)
    , tmux_attached_(false)
    , screen_integration_enabled_(false)
    , output_buffer_(1024 * 1024)
    , last_snapshot_id_(0)
    , client_last_offset_(0)
{
    terminal_state_ = {
        .rows = 24,
        .cols = 80,
        .cursor_x = 0,
        .cursor_y = 0,
        .screen_buffer = {},
        .frame_number = 0
    };
}

Session::~Session() {
    if (pty_fd_ >= 0) {
        close(pty_fd_);
    }
}

void Session::buffer_data(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    send_buffer_.insert(send_buffer_.end(), data.begin(), data.end());
}

std::vector<uint8_t> Session::get_buffered_data() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    std::vector<uint8_t> result = send_buffer_;
    send_buffer_.clear();
    return result;
}

void Session::update_screen_state(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    terminal_state_.frame_number++;
    frame_cache_[static_cast<uint32_t>(terminal_state_.frame_number)] = data;
    
    const size_t max_frames = 100;
    while (frame_cache_.size() > max_frames) {
        frame_cache_.erase(frame_cache_.begin());
    }
}

std::vector<uint8_t> Session::serialize_screen_state() {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    std::vector<uint8_t> result;
    
    uint32_t rows = htobe32(terminal_state_.rows);
    uint32_t cols = htobe32(terminal_state_.cols);
    uint32_t cursor_x = htobe32(terminal_state_.cursor_x);
    uint32_t cursor_y = htobe32(terminal_state_.cursor_y);
    uint64_t frame_num = htobe64(terminal_state_.frame_number);
    
    result.insert(result.end(), reinterpret_cast<uint8_t*>(&rows), reinterpret_cast<uint8_t*>(&rows) + 4);
    result.insert(result.end(), reinterpret_cast<uint8_t*>(&cols), reinterpret_cast<uint8_t*>(&cols) + 4);
    result.insert(result.end(), reinterpret_cast<uint8_t*>(&cursor_x), reinterpret_cast<uint8_t*>(&cursor_x) + 4);
    result.insert(result.end(), reinterpret_cast<uint8_t*>(&cursor_y), reinterpret_cast<uint8_t*>(&cursor_y) + 4);
    result.insert(result.end(), reinterpret_cast<uint8_t*>(&frame_num), reinterpret_cast<uint8_t*>(&frame_num) + 8);
    
    uint32_t buffer_size = htobe32(static_cast<uint32_t>(terminal_state_.screen_buffer.size()));
    result.insert(result.end(), reinterpret_cast<uint8_t*>(&buffer_size), reinterpret_cast<uint8_t*>(&buffer_size) + 4);
    result.insert(result.end(), terminal_state_.screen_buffer.begin(), terminal_state_.screen_buffer.end());
    
    return result;
}

void Session::restore_screen_state(const std::vector<uint8_t>& data) {
    if (data.size() < 28) {
        return;
    }
    
    size_t offset = 0;
    terminal_state_.rows = be32toh(*reinterpret_cast<const uint32_t*>(data.data() + offset));
    offset += 4;
    terminal_state_.cols = be32toh(*reinterpret_cast<const uint32_t*>(data.data() + offset));
    offset += 4;
    terminal_state_.cursor_x = be32toh(*reinterpret_cast<const uint32_t*>(data.data() + offset));
    offset += 4;
    terminal_state_.cursor_y = be32toh(*reinterpret_cast<const uint32_t*>(data.data() + offset));
    offset += 4;
    terminal_state_.frame_number = be64toh(*reinterpret_cast<const uint64_t*>(data.data() + offset));
    offset += 8;
    
    uint32_t buffer_size = be32toh(*reinterpret_cast<const uint32_t*>(data.data() + offset));
    offset += 4;
    
    if (data.size() >= offset + buffer_size) {
        terminal_state_.screen_buffer.assign(data.begin() + offset, data.begin() + offset + buffer_size);
    }
}

void Session::start_screen_integration() {
    screen_integration_enabled_ = true;
    Logger::info("Screen integration enabled for session " + session_id_);
}

void Session::stop_screen_integration() {
    screen_integration_enabled_ = false;
    Logger::info("Screen integration disabled for session " + session_id_);
}

std::string Session::capture_screen() {
    if (!tmux_attached_) {
        return "";
    }
    
    try {
        std::string cmd = "tmux capture-pane -t " + tmux_session_name_ + " -p";
        return exec_command(cmd);
    } catch (const std::exception& e) {
        Logger::warn("Failed to capture screen: " + std::string(e.what()));
        return "";
    }
}

void Session::restore_screen(const std::string& screen_data) {
    if (!tmux_attached_ || screen_data.empty()) {
        return;
    }
    
    try {
        std::string cmd = "tmux send-keys -t " + tmux_session_name_ + " '" + screen_data + "'";
        exec_command(cmd);
    } catch (const std::exception& e) {
        Logger::warn("Failed to restore screen: " + std::string(e.what()));
    }
}

void Session::append_output(const std::vector<uint8_t>& data) {
    output_buffer_.append(data);
}

void Session::append_output(const std::string& data) {
    output_buffer_.append(data);
}

SessionManager::SessionManager() {
}

SessionManager::~SessionManager() {
}

std::shared_ptr<Session> SessionManager::create_session() {
    std::string session_id = generate_session_id();
    return create_session(session_id);
}

std::shared_ptr<Session> SessionManager::create_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    if (sessions_.size() >= MAX_SESSIONS) {
        Logger::error("Maximum sessions limit reached: " + std::to_string(MAX_SESSIONS));
        return nullptr;
    }
    
    if (sessions_.find(session_id) != sessions_.end()) {
        Logger::warn("Session already exists: " + session_id);
        return sessions_[session_id];
    }
    
    auto session = std::make_shared<Session>(session_id);
    sessions_[session_id] = session;
    Logger::info("Created session: " + session_id + " (total: " + std::to_string(sessions_.size()) + ")");
    return session;
}

std::shared_ptr<Session> SessionManager::get_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        return it->second;
    }
    return nullptr;
}

bool SessionManager::session_exists(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return sessions_.find(session_id) != sessions_.end();
}

void SessionManager::remove_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        it->second->set_state(SessionState::CLOSED);
        sessions_.erase(it);
        Logger::info("Removed session: " + session_id + " (total: " + std::to_string(sessions_.size()) + ")");
    }
}

void SessionManager::cleanup_inactive_sessions(uint64_t timeout_ms) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    uint64_t now = current_time_ms();
    
    auto it = sessions_.begin();
    while (it != sessions_.end()) {
        uint64_t idle_time = now - it->second->get_last_activity();
        if (idle_time > timeout_ms) {
            Logger::info("Session " + it->first + " timed out after " + std::to_string(idle_time) + "ms");
            it->second->set_state(SessionState::CLOSED);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t SessionManager::get_session_count() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return sessions_.size();
}

std::vector<std::string> SessionManager::get_all_session_ids() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    std::vector<std::string> ids;
    for (const auto& pair : sessions_) {
        ids.push_back(pair.first);
    }
    return ids;
}

}
