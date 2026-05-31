#include "common/snapshot.h"
#include "common/utils.h"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <cstring>
#include <algorithm>

namespace moshpp {

static void write_uint64(std::vector<uint8_t>& buf, uint64_t value) {
    uint64_t be = htobe64(value);
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&be), reinterpret_cast<uint8_t*>(&be) + 8);
}

static void write_uint32(std::vector<uint8_t>& buf, uint32_t value) {
    uint32_t be = htobe32(value);
    buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&be), reinterpret_cast<uint8_t*>(&be) + 4);
}

static void write_string(std::vector<uint8_t>& buf, const std::string& str) {
    write_uint32(buf, static_cast<uint32_t>(str.size()));
    buf.insert(buf.end(), str.begin(), str.end());
}

static void write_bytes(std::vector<uint8_t>& buf, const std::vector<uint8_t>& data) {
    write_uint32(buf, static_cast<uint32_t>(data.size()));
    buf.insert(buf.end(), data.begin(), data.end());
}

static uint64_t read_uint64(const std::vector<uint8_t>& buf, size_t& offset) {
    uint64_t be = *reinterpret_cast<const uint64_t*>(buf.data() + offset);
    offset += 8;
    return be64toh(be);
}

static uint32_t read_uint32(const std::vector<uint8_t>& buf, size_t& offset) {
    uint32_t be = *reinterpret_cast<const uint32_t*>(buf.data() + offset);
    offset += 4;
    return be32toh(be);
}

static std::string read_string(const std::vector<uint8_t>& buf, size_t& offset) {
    uint32_t len = read_uint32(buf, offset);
    std::string str(buf.data() + offset, buf.data() + offset + len);
    offset += len;
    return str;
}

static std::vector<uint8_t> read_bytes(const std::vector<uint8_t>& buf, size_t& offset) {
    uint32_t len = read_uint32(buf, offset);
    std::vector<uint8_t> data(buf.data() + offset, buf.data() + offset + len);
    offset += len;
    return data;
}

std::vector<uint8_t> SessionSnapshot::serialize() const {
    std::vector<uint8_t> buf;
    write_string(buf, session_id);
    write_uint64(buf, snapshot_id);
    write_uint64(buf, timestamp);
    write_uint64(buf, output_offset);
    write_uint32(buf, terminal_rows);
    write_uint32(buf, terminal_cols);
    write_uint32(buf, cursor_x);
    write_uint32(buf, cursor_y);
    write_bytes(buf, output_data);
    write_bytes(buf, process_state);
    write_string(buf, tmux_session_name);
    write_uint64(buf, checksum);
    return buf;
}

bool SessionSnapshot::deserialize(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    try {
        session_id = read_string(data, offset);
        snapshot_id = read_uint64(data, offset);
        timestamp = read_uint64(data, offset);
        output_offset = read_uint64(data, offset);
        terminal_rows = read_uint32(data, offset);
        terminal_cols = read_uint32(data, offset);
        cursor_x = read_uint32(data, offset);
        cursor_y = read_uint32(data, offset);
        output_data = read_bytes(data, offset);
        process_state = read_bytes(data, offset);
        tmux_session_name = read_string(data, offset);
        checksum = read_uint64(data, offset);
        return true;
    } catch (...) {
        return false;
    }
}

uint64_t SessionSnapshot::compute_checksum() const {
    uint64_t sum = 0;
    for (uint8_t b : output_data) {
        sum = sum * 31 + b;
    }
    return sum;
}

std::vector<uint8_t> DeltaSyncResponse::serialize() const {
    std::vector<uint8_t> buf;
    write_uint64(buf, current_snapshot_id);
    write_uint64(buf, current_output_offset);
    buf.push_back(has_delta ? 1 : 0);
    buf.push_back(full_sync_required ? 1 : 0);
    write_bytes(buf, delta_data);
    write_bytes(buf, snapshot_data);
    return buf;
}

bool DeltaSyncResponse::deserialize(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    try {
        current_snapshot_id = read_uint64(data, offset);
        current_output_offset = read_uint64(data, offset);
        has_delta = data[offset++] != 0;
        full_sync_required = data[offset++] != 0;
        delta_data = read_bytes(data, offset);
        snapshot_data = read_bytes(data, offset);
        return true;
    } catch (...) {
        return false;
    }
}

SnapshotManager::SnapshotManager(const std::string& storage_dir)
    : storage_dir_(storage_dir)
{
    encryption_key_ = generate_random_bytes(KEY_LENGTH);
    crypto_.generate_keys();
    ensure_directory_exists(storage_dir_);
}

SnapshotManager::~SnapshotManager() {
}

bool SnapshotManager::create_snapshot(const std::string& session_id,
                                      const OutputBuffer& output_buffer,
                                      uint32_t rows, uint32_t cols,
                                      uint32_t cursor_x, uint32_t cursor_y,
                                      const std::string& tmux_session) {
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t snapshot_id = ++last_snapshot_id_[session_id];
    auto snapshot = std::make_shared<SessionSnapshot>();
    snapshot->session_id = session_id;
    snapshot->snapshot_id = snapshot_id;
    snapshot->timestamp = current_time_ms();
    snapshot->output_offset = output_buffer.get_total_bytes();
    snapshot->terminal_rows = rows;
    snapshot->terminal_cols = cols;
    snapshot->cursor_x = cursor_x;
    snapshot->cursor_y = cursor_y;
    snapshot->output_data = output_buffer.get_delta(snapshot->output_offset > 1024 * 1024 ? 
                                                    snapshot->output_offset - 1024 * 1024 : 0);
    snapshot->tmux_session_name = tmux_session;
    snapshot->checksum = snapshot->compute_checksum();

    snapshots_[session_id][snapshot_id] = snapshot;
    
    while (snapshots_[session_id].size() > 10) {
        snapshots_[session_id].erase(snapshots_[session_id].begin());
    }

    save_snapshot_to_disk(*snapshot);

    Logger::debug("Created snapshot " + std::to_string(snapshot_id) + 
                  " for session " + bytes_to_hex(std::vector<uint8_t>(
                      reinterpret_cast<const uint8_t*>(session_id.data()),
                      reinterpret_cast<const uint8_t*>(session_id.data()) + session_id.size()
                  ).substr(0, 16));

    return true;
}

std::shared_ptr<SessionSnapshot> SnapshotManager::get_latest_snapshot(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = snapshots_.find(session_id);
    if (it == snapshots_.end() || it->second.empty()) {
        return load_snapshot_from_disk(session_id, 0);
    }
    return it->second.rbegin()->second;
}

std::shared_ptr<SessionSnapshot> SnapshotManager::get_snapshot(const std::string& session_id, uint64_t snapshot_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = snapshots_.find(session_id);
    if (it != snapshots_.end()) {
        auto sit = it->second.find(snapshot_id);
        if (sit != it->second.end()) {
            return sit->second;
        }
    }
    return load_snapshot_from_disk(session_id, snapshot_id);
}

DeltaSyncResponse SnapshotManager::compute_delta(const DeltaSyncRequest& request,
                                                 const OutputBuffer& current_output) {
    DeltaSyncResponse response;
    response.has_delta = false;
    response.full_sync_required = false;

    auto latest = get_latest_snapshot(request.session_id);
    if (latest) {
        response.current_snapshot_id = latest->snapshot_id;
    }
    response.current_output_offset = current_output.get_total_bytes();

    if (request.last_output_offset >= current_output.get_total_bytes()) {
        return response;
    }

    size_t delta_size = current_output.get_total_bytes() - request.last_output_offset;
    if (delta_size > 2 * 1024 * 1024) {
        response.full_sync_required = true;
        if (latest) {
            response.snapshot_data = latest->output_data;
        }
        Logger::info("Full sync required: delta size " + std::to_string(delta_size) + " bytes");
        return response;
    }

    response.has_delta = true;
    response.delta_data = current_output.get_delta(request.last_output_offset);
    
    Logger::debug("Delta sync: " + std::to_string(response.delta_data.size()) + " bytes");
    return response;
}

bool SnapshotManager::delete_snapshots(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshots_.erase(session_id);
    last_snapshot_id_.erase(session_id);
    
    std::string session_dir = get_session_dir(session_id);
    if (directory_exists(session_dir)) {
        std::string cmd = "rm -rf " + session_dir;
        system(cmd.c_str());
    }
    return true;
}

bool SnapshotManager::cleanup_old_snapshots(uint64_t max_age_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t now = current_time_ms();
    uint64_t max_age_ms = max_age_seconds * 1000;

    for (auto it = snapshots_.begin(); it != snapshots_.end(); ) {
        if (!it->second.empty()) {
            auto latest = it->second.rbegin()->second;
            if (now - latest->timestamp > max_age_ms) {
                it = snapshots_.erase(it);
                continue;
            }
        }
        ++it;
    }
    return true;
}

void SnapshotManager::set_encryption_key(const std::vector<uint8_t>& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    encryption_key_ = key;
    crypto_.init(key, generate_random_bytes(IV_LENGTH));
}

bool SnapshotManager::save_snapshot_to_disk(const SessionSnapshot& snapshot) {
    std::string session_dir = get_session_dir(snapshot.session_id);
    if (!ensure_directory_exists(session_dir)) {
        return false;
    }

    std::string path = get_snapshot_path(snapshot.session_id, snapshot.snapshot_id);
    std::vector<uint8_t> data = snapshot.serialize();
    std::vector<uint8_t> encrypted = encrypt_snapshot(data);

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        Logger::warn("Failed to open snapshot file for writing: " + path);
        return false;
    }

    file.write(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
    return true;
}

std::shared_ptr<SessionSnapshot> SnapshotManager::load_snapshot_from_disk(const std::string& session_id, uint64_t snapshot_id) {
    std::string path;
    if (snapshot_id == 0) {
        std::string session_dir = get_session_dir(session_id);
        if (!directory_exists(session_dir)) {
            return nullptr;
        }
        std::vector<uint64_t> ids = list_snapshots(session_id);
        if (ids.empty()) {
            return nullptr;
        }
        snapshot_id = ids.back();
    }

    path = get_snapshot_path(session_id, snapshot_id);
    if (!file_exists(path)) {
        return nullptr;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return nullptr;
    }

    std::vector<uint8_t> encrypted((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    
    std::vector<uint8_t> decrypted = decrypt_snapshot(encrypted);
    auto snapshot = std::make_shared<SessionSnapshot>();
    if (!snapshot->deserialize(decrypted)) {
        return nullptr;
    }

    return snapshot;
}

std::vector<uint64_t> SnapshotManager::list_snapshots(const std::string& session_id) {
    std::vector<uint64_t> ids;
    std::string session_dir = get_session_dir(session_id);
    
    if (!directory_exists(session_dir)) {
        return ids;
    }

    try {
        std::string cmd = "ls " + session_dir + "/*.snap 2>/dev/null";
        std::string output = exec_command(cmd);
        std::vector<std::string> lines = split_string(output, '\n');
        
        for (const auto& line : lines) {
            if (line.empty()) continue;
            size_t pos = line.find_last_of('/');
            if (pos != std::string::npos) {
                std::string filename = line.substr(pos + 1);
                size_t dot_pos = filename.find('.');
                if (dot_pos != std::string::npos) {
                    uint64_t id = std::stoull(filename.substr(0, dot_pos));
                    ids.push_back(id);
                }
            }
        }
        std::sort(ids.begin(), ids.end());
    } catch (...) {
    }
    
    return ids;
}

std::string SnapshotManager::get_snapshot_path(const std::string& session_id, uint64_t snapshot_id) {
    return get_session_dir(session_id) + "/" + std::to_string(snapshot_id) + ".snap";
}

std::string SnapshotManager::get_session_dir(const std::string& session_id) {
    return storage_dir_ + "/" + bytes_to_hex(std::vector<uint8_t>(
        reinterpret_cast<const uint8_t*>(session_id.data()),
        reinterpret_cast<const uint8_t*>(session_id.data()) + SESSION_ID_LENGTH
    ));
}

bool SnapshotManager::ensure_directory_exists(const std::string& path) {
    if (directory_exists(path)) {
        return true;
    }
    return create_directory(path);
}

std::vector<uint8_t> SnapshotManager::encrypt_snapshot(const std::vector<uint8_t>& data) {
    return crypto_.encrypt(data);
}

std::vector<uint8_t> SnapshotManager::decrypt_snapshot(const std::vector<uint8_t>& data) {
    if (data.size() < IV_LENGTH + AUTH_TAG_LENGTH) {
        return {};
    }
    std::vector<uint8_t> iv(data.begin(), data.begin() + IV_LENGTH);
    std::vector<uint8_t> ciphertext(data.begin() + IV_LENGTH, data.end());
    return crypto_.decrypt(ciphertext, iv);
}

ReplaySession::ReplaySession(const std::string& session_id, SnapshotManager& snapshot_manager)
    : session_id_(session_id)
    , snapshot_manager_(snapshot_manager)
{
}

bool ReplaySession::load_snapshots() {
    std::vector<uint8_t> session_bytes = hex_to_bytes(session_id_);
    std::string raw_session_id(session_bytes.begin(), session_bytes.end());
    
    std::vector<uint64_t> ids = snapshot_manager_.list_snapshots(raw_session_id);
    for (uint64_t id : ids) {
        auto snapshot = snapshot_manager_.get_snapshot(raw_session_id, id);
        if (snapshot) {
            snapshots_.push_back(snapshot);
        }
    }
    
    std::sort(snapshots_.begin(), snapshots_.end(),
              [](const std::shared_ptr<SessionSnapshot>& a, 
                 const std::shared_ptr<SessionSnapshot>& b) {
                  return a->timestamp < b->timestamp;
              });
    
    return !snapshots_.empty();
}

std::vector<uint8_t> ReplaySession::get_full_output() const {
    if (snapshots_.empty()) {
        return {};
    }
    
    std::vector<uint8_t> result;
    for (const auto& snap : snapshots_) {
        result.insert(result.end(), snap->output_data.begin(), snap->output_data.end());
    }
    return result;
}

std::vector<uint8_t> ReplaySession::get_output_between(uint64_t start_time, uint64_t end_time) const {
    std::vector<uint8_t> result;
    for (const auto& snap : snapshots_) {
        if (snap->timestamp >= start_time && snap->timestamp <= end_time) {
            result.insert(result.end(), snap->output_data.begin(), snap->output_data.end());
        }
    }
    return result;
}

uint64_t ReplaySession::get_duration() const {
    if (snapshots_.size() < 2) {
        return 0;
    }
    return snapshots_.back()->timestamp - snapshots_.front()->timestamp;
}

uint64_t ReplaySession::get_start_time() const {
    if (snapshots_.empty()) {
        return 0;
    }
    return snapshots_.front()->timestamp;
}

uint64_t ReplaySession::get_end_time() const {
    if (snapshots_.empty()) {
        return 0;
    }
    return snapshots_.back()->timestamp;
}

void ReplaySession::play(std::function<void(const std::vector<uint8_t>&)> output_callback,
                         float speed) const {
    if (snapshots_.empty()) {
        return;
    }

    for (size_t i = 0; i < snapshots_.size(); ++i) {
        const auto& snap = snapshots_[i];
        output_callback(snap->output_data);
        
        if (i < snapshots_.size() - 1 && speed > 0) {
            uint64_t interval = (snapshots_[i + 1]->timestamp - snap->timestamp) / speed;
            sleep_ms(static_cast<uint32_t>(std::min<uint64_t>(interval, 5000)));
        }
    }
}

}
