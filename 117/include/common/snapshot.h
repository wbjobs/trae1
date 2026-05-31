#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <mutex>
#include <memory>
#include <chrono>
#include "circular_buffer.h"
#include "crypto.h"

namespace moshpp {

struct SessionSnapshot {
    std::string session_id;
    uint64_t snapshot_id;
    uint64_t timestamp;
    uint64_t output_offset;
    uint32_t terminal_rows;
    uint32_t terminal_cols;
    uint32_t cursor_x;
    uint32_t cursor_y;
    std::vector<uint8_t> output_data;
    std::vector<uint8_t> process_state;
    std::string tmux_session_name;
    uint64_t checksum;

    std::vector<uint8_t> serialize() const;
    bool deserialize(const std::vector<uint8_t>& data);
    uint64_t compute_checksum() const;
};

struct DeltaSyncRequest {
    std::string session_id;
    uint64_t last_snapshot_id;
    uint64_t last_output_offset;
};

struct DeltaSyncResponse {
    uint64_t current_snapshot_id;
    uint64_t current_output_offset;
    bool has_delta;
    bool full_sync_required;
    std::vector<uint8_t> delta_data;
    std::vector<uint8_t> snapshot_data;

    std::vector<uint8_t> serialize() const;
    bool deserialize(const std::vector<uint8_t>& data);
};

class SnapshotManager {
public:
    explicit SnapshotManager(const std::string& storage_dir = "./mosh_snapshots");
    ~SnapshotManager();

    bool create_snapshot(const std::string& session_id, 
                         const OutputBuffer& output_buffer,
                         uint32_t rows, uint32_t cols,
                         uint32_t cursor_x, uint32_t cursor_y,
                         const std::string& tmux_session = "");

    std::shared_ptr<SessionSnapshot> get_latest_snapshot(const std::string& session_id);
    std::shared_ptr<SessionSnapshot> get_snapshot(const std::string& session_id, uint64_t snapshot_id);

    DeltaSyncResponse compute_delta(const DeltaSyncRequest& request,
                                    const OutputBuffer& current_output);

    bool delete_snapshots(const std::string& session_id);
    bool cleanup_old_snapshots(uint64_t max_age_seconds);

    void set_encryption_key(const std::vector<uint8_t>& key);
    bool save_snapshot_to_disk(const SessionSnapshot& snapshot);
    std::shared_ptr<SessionSnapshot> load_snapshot_from_disk(const std::string& session_id, uint64_t snapshot_id);

    std::vector<uint64_t> list_snapshots(const std::string& session_id);

private:
    std::string storage_dir_;
    std::map<std::string, std::map<uint64_t, std::shared_ptr<SessionSnapshot>>> snapshots_;
    std::map<std::string, uint64_t> last_snapshot_id_;
    Crypto crypto_;
    std::vector<uint8_t> encryption_key_;
    mutable std::mutex mutex_;

    std::string get_snapshot_path(const std::string& session_id, uint64_t snapshot_id);
    std::string get_session_dir(const std::string& session_id);
    bool ensure_directory_exists(const std::string& path);
    std::vector<uint8_t> encrypt_snapshot(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decrypt_snapshot(const std::vector<uint8_t>& data);
};

class ReplaySession {
public:
    explicit ReplaySession(const std::string& session_id, SnapshotManager& snapshot_manager);
    
    bool load_snapshots();
    std::vector<uint8_t> get_full_output() const;
    std::vector<uint8_t> get_output_between(uint64_t start_time, uint64_t end_time) const;
    
    size_t get_snapshot_count() const { return snapshots_.size(); }
    uint64_t get_duration() const;
    uint64_t get_start_time() const;
    uint64_t get_end_time() const;

    void play(std::function<void(const std::vector<uint8_t>&)> output_callback,
              float speed = 1.0f) const;

private:
    std::string session_id_;
    SnapshotManager& snapshot_manager_;
    std::vector<std::shared_ptr<SessionSnapshot>> snapshots_;
};

}
