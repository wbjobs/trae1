#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <cstdint>
#include <chrono>

namespace moshpp {

struct LogicalClock {
    std::string device_id;
    uint64_t counter;
    
    LogicalClock() : counter(0) {}
    LogicalClock(const std::string& dev, uint64_t cnt) : device_id(dev), counter(cnt) {}
    
    bool operator<(const LogicalClock& other) const {
        if (counter != other.counter) return counter < other.counter;
        return device_id < other.device_id;
    }
    
    bool operator==(const LogicalClock& other) const {
        return device_id == other.device_id && counter == other.counter;
    }
    
    bool operator>(const LogicalClock& other) const {
        return !(*this < other) && !(*this == other);
    }
};

struct CRDTElement {
    std::string id;
    LogicalClock clock;
    char character;
    bool is_deleted;
    std::string origin_left_id;
    std::string origin_right_id;
    
    CRDTElement() : character(0), is_deleted(false) {}
    
    bool operator<(const CRDTElement& other) const {
        return clock < other.clock;
    }
};

struct CRDTOperation {
    enum Type { INSERT, DELETE } type;
    CRDTElement element;
    uint64_t timestamp;
    std::string device_id;
    
    std::string serialize() const;
    static CRDTOperation deserialize(const std::string& data);
};

class CRDTDocument {
public:
    CRDTDocument();
    ~CRDTDocument();
    
    void set_device_id(const std::string& device_id) { device_id_ = device_id; }
    std::string get_device_id() const { return device_id_; }
    
    std::string get_full_text() const;
    size_t get_length() const;
    
    CRDTOperation local_insert(size_t position, char c);
    CRDTOperation local_delete(size_t position);
    
    bool apply_remote_operation(const CRDTOperation& op);
    
    std::vector<CRDTOperation> get_operations_since(uint64_t timestamp) const;
    std::vector<CRDTOperation> get_all_operations() const;
    
    void clear();
    bool is_empty() const;
    
    uint64_t get_version() const { return version_; }
    void set_version(uint64_t version) { version_ = version; }
    
    std::vector<CRDTElement> get_elements() const;
    
private:
    std::string device_id_;
    std::map<std::string, CRDTElement> elements_;
    std::vector<std::string> ordered_ids_;
    std::set<LogicalClock> seen_operations_;
    std::map<uint64_t, CRDTOperation> operation_log_;
    uint64_t version_;
    LogicalClock local_clock_;
    mutable std::mutex mutex_;
    
    std::string generate_element_id();
    size_t find_position(const std::string& element_id) const;
    std::string get_element_at_position(size_t position) const;
};

struct CRDTSyncPacket {
    enum Type { FULL_SYNC, OPERATION_SYNC, ACK } type;
    std::string session_id;
    std::string device_id;
    uint64_t base_version;
    uint64_t target_version;
    std::vector<CRDTOperation> operations;
    uint64_t timestamp;
    
    std::string serialize() const;
    static CRDTSyncPacket deserialize(const std::string& data);
};

class CRDTSyncManager {
public:
    CRDTSyncManager();
    ~CRDTSyncManager();
    
    void set_session_id(const std::string& session_id) { session_id_ = session_id; }
    std::string get_session_id() const { return session_id_; }
    
    bool add_device(const std::string& device_id);
    bool remove_device(const std::string& device_id);
    std::vector<std::string> get_devices() const;
    
    std::string get_full_text() const;
    
    std::vector<CRDTOperation> local_insert(const std::string& device_id, 
                                             size_t position, 
                                             char c);
    std::vector<CRDTOperation> local_delete(const std::string& device_id, 
                                             size_t position);
    std::vector<CRDTOperation> local_insert_string(const std::string& device_id,
                                                    size_t position,
                                                    const std::string& str);
    
    bool apply_remote_operations(const std::string& device_id, 
                                  const std::vector<CRDTOperation>& ops);
    
    CRDTSyncPacket create_sync_packet(const std::string& target_device) const;
    bool apply_sync_packet(const CRDTSyncPacket& packet);
    
    CRDTSyncPacket create_full_sync_packet() const;
    
    uint64_t get_version() const { return document_.get_version(); }
    
    size_t get_document_length() const { return document_.get_length(); }
    
    void reset();

private:
    std::string session_id_;
    CRDTDocument document_;
    std::map<std::string, uint64_t> device_versions_;
    std::map<std::string, LogicalClock> device_clocks_;
    std::set<std::string> connected_devices_;
    mutable std::mutex mutex_;
    
    LogicalClock advance_clock(const std::string& device_id);
};

struct TerminalSyncState {
    std::string session_id;
    uint64_t version;
    int cursor_x;
    int cursor_y;
    int terminal_width;
    int terminal_height;
    std::string current_directory;
    std::map<std::string, std::string> environment_vars;
    std::vector<std::string> command_history;
    
    std::string serialize() const;
    static TerminalSyncState deserialize(const std::string& data);
};

class TerminalSyncManager {
public:
    TerminalSyncManager();
    ~TerminalSyncManager();
    
    void set_crdt_manager(std::shared_ptr<CRDTSyncManager> crdt) { crdt_ = crdt; }
    
    void update_cursor(int x, int y);
    void update_terminal_size(int width, int height);
    void update_current_directory(const std::string& dir);
    void update_environment(const std::map<std::string, std::string>& vars);
    void add_command_to_history(const std::string& cmd);
    
    TerminalSyncState get_sync_state() const;
    bool apply_sync_state(const TerminalSyncState& state);
    
    std::string get_terminal_output() const;
    
    int get_cursor_x() const { return state_.cursor_x; }
    int get_cursor_y() const { return state_.cursor_y; }
    int get_terminal_width() const { return state_.terminal_width; }
    int get_terminal_height() const { return state_.terminal_height; }
    
    void set_session_id(const std::string& session_id);

private:
    TerminalSyncState state_;
    std::shared_ptr<CRDTSyncManager> crdt_;
    mutable std::mutex mutex_;
};

}
