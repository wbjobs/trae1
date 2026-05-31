#include "common/crdt.h"
#include <sstream>
#include <algorithm>
#include <random>

namespace moshpp {

std::string CRDTOperation::serialize() const {
    std::ostringstream oss;
    oss << static_cast<int>(type) << ":"
        << element.id << ":"
        << element.clock.device_id << ":"
        << element.clock.counter << ":"
        << static_cast<int>(element.character) << ":"
        << (element.is_deleted ? 1 : 0) << ":"
        << element.origin_left_id << ":"
        << element.origin_right_id << ":"
        << timestamp << ":"
        << device_id;
    return oss.str();
}

CRDTOperation CRDTOperation::deserialize(const std::string& data) {
    CRDTOperation op;
    std::istringstream iss(data);
    std::string part;
    std::vector<std::string> parts;
    
    while (std::getline(iss, part, ':')) {
        parts.push_back(part);
    }
    
    if (parts.size() < 10) return op;
    
    op.type = static_cast<Type>(std::stoi(parts[0]));
    op.element.id = parts[1];
    op.element.clock.device_id = parts[2];
    op.element.clock.counter = std::stoull(parts[3]);
    op.element.character = static_cast<char>(std::stoi(parts[4]));
    op.element.is_deleted = (parts[5] == "1");
    op.element.origin_left_id = parts[6];
    op.element.origin_right_id = parts[7];
    op.timestamp = std::stoull(parts[8]);
    op.device_id = parts[9];
    
    return op;
}

CRDTDocument::CRDTDocument()
    : version_(0)
    , local_clock_("", 0) {
}

CRDTDocument::~CRDTDocument() = default;

std::string CRDTDocument::get_full_text() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string result;
    for (const auto& id : ordered_ids_) {
        auto it = elements_.find(id);
        if (it != elements_.end() && !it->second.is_deleted) {
            result += it->second.character;
        }
    }
    return result;
}

size_t CRDTDocument::get_length() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t count = 0;
    for (const auto& id : ordered_ids_) {
        auto it = elements_.find(id);
        if (it != elements_.end() && !it->second.is_deleted) {
            count++;
        }
    }
    return count;
}

CRDTOperation CRDTDocument::local_insert(size_t position, char c) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    local_clock_.counter++;
    
    CRDTElement element;
    element.id = generate_element_id();
    element.clock = local_clock_;
    element.character = c;
    element.is_deleted = false;
    
    if (position == 0) {
        element.origin_left_id = "";
        element.origin_right_id = ordered_ids_.empty() ? "" : ordered_ids_.front();
        ordered_ids_.insert(ordered_ids_.begin(), element.id);
    } else if (position >= ordered_ids_.size()) {
        element.origin_left_id = ordered_ids_.empty() ? "" : ordered_ids_.back();
        element.origin_right_id = "";
        ordered_ids_.push_back(element.id);
    } else {
        element.origin_left_id = ordered_ids_[position - 1];
        element.origin_right_id = ordered_ids_[position];
        ordered_ids_.insert(ordered_ids_.begin() + position, element.id);
    }
    
    elements_[element.id] = element;
    
    CRDTOperation op;
    op.type = CRDTOperation::INSERT;
    op.element = element;
    op.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    op.device_id = device_id_;
    
    seen_operations_.insert(element.clock);
    operation_log_[op.timestamp] = op;
    version_++;
    
    return op;
}

CRDTOperation CRDTDocument::local_delete(size_t position) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (position >= ordered_ids_.size()) {
        return CRDTOperation{};
    }
    
    local_clock_.counter++;
    
    std::string element_id = ordered_ids_[position];
    auto it = elements_.find(element_id);
    if (it == elements_.end()) {
        return CRDTOperation{};
    }
    
    it->second.is_deleted = true;
    
    CRDTOperation op;
    op.type = CRDTOperation::DELETE;
    op.element = it->second;
    op.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    op.device_id = device_id_;
    
    seen_operations_.insert(it->second.clock);
    operation_log_[op.timestamp] = op;
    version_++;
    
    return op;
}

bool CRDTDocument::apply_remote_operation(const CRDTOperation& op) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (seen_operations_.find(op.element.clock) != seen_operations_.end()) {
        return false;
    }
    
    if (op.type == CRDTOperation::INSERT) {
        elements_[op.element.id] = op.element;
        
        auto insert_pos = ordered_ids_.begin();
        for (auto it = ordered_ids_.begin(); it != ordered_ids_.end(); ++it) {
            auto elem_it = elements_.find(*it);
            if (elem_it != elements_.end() && elem_it->second.clock > op.element.clock) {
                insert_pos = it;
                break;
            }
            insert_pos = it + 1;
        }
        ordered_ids_.insert(insert_pos, op.element.id);
        
    } else if (op.type == CRDTOperation::DELETE) {
        auto it = elements_.find(op.element.id);
        if (it != elements_.end()) {
            it->second.is_deleted = true;
        }
    }
    
    seen_operations_.insert(op.element.clock);
    operation_log_[op.timestamp] = op;
    version_++;
    
    return true;
}

std::vector<CRDTOperation> CRDTDocument::get_operations_since(uint64_t timestamp) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<CRDTOperation> result;
    for (const auto& [ts, op] : operation_log_) {
        if (ts > timestamp) {
            result.push_back(op);
        }
    }
    return result;
}

std::vector<CRDTOperation> CRDTDocument::get_all_operations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<CRDTOperation> result;
    for (const auto& [ts, op] : operation_log_) {
        result.push_back(op);
    }
    return result;
}

void CRDTDocument::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    elements_.clear();
    ordered_ids_.clear();
    seen_operations_.clear();
    operation_log_.clear();
    version_ = 0;
}

bool CRDTDocument::is_empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ordered_ids_.empty();
}

std::vector<CRDTElement> CRDTDocument::get_elements() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<CRDTElement> result;
    for (const auto& id : ordered_ids_) {
        auto it = elements_.find(id);
        if (it != elements_.end()) {
            result.push_back(it->second);
        }
    }
    return result;
}

std::string CRDTDocument::generate_element_id() {
    static std::atomic<uint64_t> counter{0};
    std::ostringstream oss;
    oss << device_id_ << "_" << local_clock_.counter << "_" << counter.fetch_add(1);
    return oss.str();
}

size_t CRDTDocument::find_position(const std::string& element_id) const {
    for (size_t i = 0; i < ordered_ids_.size(); i++) {
        if (ordered_ids_[i] == element_id) {
            return i;
        }
    }
    return ordered_ids_.size();
}

std::string CRDTDocument::get_element_at_position(size_t position) const {
    if (position >= ordered_ids_.size()) {
        return "";
    }
    return ordered_ids_[position];
}

std::string CRDTSyncPacket::serialize() const {
    std::ostringstream oss;
    oss << static_cast<int>(type) << ":"
        << session_id << ":"
        << device_id << ":"
        << base_version << ":"
        << target_version << ":"
        << operations.size() << ":"
        << timestamp;
    
    for (const auto& op : operations) {
        oss << ":" << op.serialize();
    }
    
    return oss.str();
}

CRDTSyncPacket CRDTSyncPacket::deserialize(const std::string& data) {
    CRDTSyncPacket packet;
    std::istringstream iss(data);
    std::string part;
    std::vector<std::string> parts;
    
    while (std::getline(iss, part, ':')) {
        parts.push_back(part);
    }
    
    if (parts.size() < 7) return packet;
    
    packet.type = static_cast<Type>(std::stoi(parts[0]));
    packet.session_id = parts[1];
    packet.device_id = parts[2];
    packet.base_version = std::stoull(parts[3]);
    packet.target_version = std::stoull(parts[4]);
    size_t op_count = std::stoull(parts[5]);
    packet.timestamp = std::stoull(parts[6]);
    
    size_t idx = 7;
    for (size_t i = 0; i < op_count && idx < parts.size(); i++) {
        std::string op_data = parts[idx++];
        packet.operations.push_back(CRDTOperation::deserialize(op_data));
    }
    
    return packet;
}

CRDTSyncManager::CRDTSyncManager()
    : session_id_("") {
}

CRDTSyncManager::~CRDTSyncManager() = default;

bool CRDTSyncManager::add_device(const std::string& device_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (connected_devices_.find(device_id) != connected_devices_.end()) {
        return false;
    }
    
    connected_devices_.insert(device_id);
    device_versions_[device_id] = 0;
    device_clocks_[device_id] = LogicalClock(device_id, 0);
    
    return true;
}

bool CRDTSyncManager::remove_device(const std::string& device_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = connected_devices_.find(device_id);
    if (it == connected_devices_.end()) {
        return false;
    }
    
    connected_devices_.erase(it);
    device_versions_.erase(device_id);
    device_clocks_.erase(device_id);
    
    return true;
}

std::vector<std::string> CRDTSyncManager::get_devices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<std::string>(connected_devices_.begin(), connected_devices_.end());
}

std::string CRDTSyncManager::get_full_text() const {
    return document_.get_full_text();
}

std::vector<CRDTOperation> CRDTSyncManager::local_insert(const std::string& device_id, 
                                                          size_t position, 
                                                          char c) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    document_.set_device_id(device_id);
    CRDTOperation op = document_.local_insert(position, c);
    
    device_clocks_[device_id] = op.element.clock;
    device_versions_[device_id] = document_.get_version();
    
    return {op};
}

std::vector<CRDTOperation> CRDTSyncManager::local_delete(const std::string& device_id, 
                                                          size_t position) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    document_.set_device_id(device_id);
    CRDTOperation op = document_.local_delete(position);
    
    device_clocks_[device_id] = op.element.clock;
    device_versions_[device_id] = document_.get_version();
    
    return {op};
}

std::vector<CRDTOperation> CRDTSyncManager::local_insert_string(const std::string& device_id,
                                                                 size_t position,
                                                                 const std::string& str) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<CRDTOperation> ops;
    document_.set_device_id(device_id);
    
    for (size_t i = 0; i < str.size(); i++) {
        CRDTOperation op = document_.local_insert(position + i, str[i]);
        ops.push_back(op);
    }
    
    if (!ops.empty()) {
        device_clocks_[device_id] = ops.back().element.clock;
        device_versions_[device_id] = document_.get_version();
    }
    
    return ops;
}

bool CRDTSyncManager::apply_remote_operations(const std::string& device_id, 
                                               const std::vector<CRDTOperation>& ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    bool applied = false;
    for (const auto& op : ops) {
        if (document_.apply_remote_operation(op)) {
            applied = true;
            if (op.element.clock > device_clocks_[device_id]) {
                device_clocks_[device_id] = op.element.clock;
            }
        }
    }
    
    if (applied) {
        device_versions_[device_id] = document_.get_version();
    }
    
    return applied;
}

CRDTSyncPacket CRDTSyncManager::create_sync_packet(const std::string& target_device) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    CRDTSyncPacket packet;
    packet.type = CRDTSyncPacket::OPERATION_SYNC;
    packet.session_id = session_id_;
    packet.device_id = "";
    
    auto it = device_versions_.find(target_device);
    packet.base_version = (it != device_versions_.end()) ? it->second : 0;
    packet.target_version = document_.get_version();
    
    uint64_t timestamp = 0;
    packet.operations = document_.get_operations_since(timestamp);
    packet.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    return packet;
}

bool CRDTSyncManager::apply_sync_packet(const CRDTSyncPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (packet.session_id != session_id_) {
        return false;
    }
    
    return apply_remote_operations(packet.device_id, packet.operations);
}

CRDTSyncPacket CRDTSyncManager::create_full_sync_packet() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    CRDTSyncPacket packet;
    packet.type = CRDTSyncPacket::FULL_SYNC;
    packet.session_id = session_id_;
    packet.device_id = "";
    packet.base_version = 0;
    packet.target_version = document_.get_version();
    packet.operations = document_.get_all_operations();
    packet.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    return packet;
}

void CRDTSyncManager::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    document_.clear();
    device_versions_.clear();
    device_clocks_.clear();
    connected_devices_.clear();
}

LogicalClock CRDTSyncManager::advance_clock(const std::string& device_id) {
    auto it = device_clocks_.find(device_id);
    if (it == device_clocks_.end()) {
        device_clocks_[device_id] = LogicalClock(device_id, 1);
        return device_clocks_[device_id];
    }
    
    it->second.counter++;
    return it->second;
}

std::string TerminalSyncState::serialize() const {
    std::ostringstream oss;
    oss << session_id << ":"
        << version << ":"
        << cursor_x << ":"
        << cursor_y << ":"
        << terminal_width << ":"
        << terminal_height << ":"
        << current_directory << ":"
        << environment_vars.size() << ":"
        << command_history.size();
    
    for (const auto& [key, value] : environment_vars) {
        oss << ":" << key << "=" << value;
    }
    
    for (const auto& cmd : command_history) {
        oss << ":" << cmd;
    }
    
    return oss.str();
}

TerminalSyncState TerminalSyncState::deserialize(const std::string& data) {
    TerminalSyncState state;
    std::istringstream iss(data);
    std::string part;
    std::vector<std::string> parts;
    
    while (std::getline(iss, part, ':')) {
        parts.push_back(part);
    }
    
    if (parts.size() < 9) return state;
    
    state.session_id = parts[0];
    state.version = std::stoull(parts[1]);
    state.cursor_x = std::stoi(parts[2]);
    state.cursor_y = std::stoi(parts[3]);
    state.terminal_width = std::stoi(parts[4]);
    state.terminal_height = std::stoi(parts[5]);
    state.current_directory = parts[6];
    size_t env_count = std::stoull(parts[7]);
    size_t hist_count = std::stoull(parts[8]);
    
    size_t idx = 9;
    for (size_t i = 0; i < env_count && idx < parts.size(); i++) {
        std::string env_str = parts[idx++];
        size_t eq_pos = env_str.find('=');
        if (eq_pos != std::string::npos) {
            state.environment_vars[env_str.substr(0, eq_pos)] = env_str.substr(eq_pos + 1);
        }
    }
    
    for (size_t i = 0; i < hist_count && idx < parts.size(); i++) {
        state.command_history.push_back(parts[idx++]);
    }
    
    return state;
}

TerminalSyncManager::TerminalSyncManager() = default;

TerminalSyncManager::~TerminalSyncManager() = default;

void TerminalSyncManager::update_cursor(int x, int y) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.cursor_x = x;
    state_.cursor_y = y;
}

void TerminalSyncManager::update_terminal_size(int width, int height) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.terminal_width = width;
    state_.terminal_height = height;
}

void TerminalSyncManager::update_current_directory(const std::string& dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.current_directory = dir;
}

void TerminalSyncManager::update_environment(const std::map<std::string, std::string>& vars) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.environment_vars = vars;
}

void TerminalSyncManager::add_command_to_history(const std::string& cmd) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.command_history.push_back(cmd);
    if (state_.command_history.size() > 100) {
        state_.command_history.erase(state_.command_history.begin());
    }
}

TerminalSyncState TerminalSyncManager::get_sync_state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (crdt_) {
        state_.version = crdt_->get_version();
    }
    return state_;
}

bool TerminalSyncManager::apply_sync_state(const TerminalSyncState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state.session_id != state_.session_id) {
        return false;
    }
    
    state_.cursor_x = state.cursor_x;
    state_.cursor_y = state.cursor_y;
    state_.terminal_width = state.terminal_width;
    state_.terminal_height = state.terminal_height;
    state_.current_directory = state.current_directory;
    state_.environment_vars = state.environment_vars;
    state_.command_history = state.command_history;
    state_.version = state.version;
    
    return true;
}

std::string TerminalSyncManager::get_terminal_output() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (crdt_) {
        return crdt_->get_full_text();
    }
    return "";
}

void TerminalSyncManager::set_session_id(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.session_id = session_id;
}

}
