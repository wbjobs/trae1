#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <mutex>
#include <memory>
#include <functional>
#include "session.h"
#include "auth.h"

namespace moshpp {

struct DeviceInfo {
    std::string device_id;
    std::string device_name;
    std::string device_type;
    std::string ip_address;
    uint64_t connected_at;
    uint64_t last_active;
    bool is_active;
    bool is_readonly;
};

struct SessionDeviceState {
    std::string session_id;
    std::string owner_user_id;
    std::vector<DeviceInfo> connected_devices;
    std::string primary_device_id;
    std::map<std::string, std::string> device_positions;
    uint64_t created_at;
    uint64_t last_updated;
    bool is_active;
    int active_device_count;
};

struct ShellState {
    std::string current_directory;
    std::map<std::string, std::string> environment_variables;
    std::vector<std::string> command_history;
    std::string shell_type;
    int terminal_rows;
    int terminal_cols;
    uint64_t state_cursor_x;
    uint64_t state_cursor_y;
    bool is_processing;
};

class RedisSessionStore {
public:
    RedisSessionStore();
    ~RedisSessionStore();

    bool connect(const std::string& host = "localhost", int port = 6379, 
                 const std::string& password = "", int db = 0);
    void disconnect();
    bool is_connected() const { return connected_; }

    void set_ttl(uint64_t seconds) { ttl_seconds_ = seconds; }
    uint64_t get_ttl() const { return ttl_seconds_; }

    bool save_session_state(const std::string& session_id, 
                          const SessionDeviceState& state);
    SessionDeviceState load_session_state(const std::string& session_id);
    bool delete_session_state(const std::string& session_id);
    bool session_exists(const std::string& session_id);

    bool add_device(const std::string& session_id, 
                   const DeviceInfo& device);
    bool remove_device(const std::string& session_id, 
                      const std::string& device_id);
    bool update_device(const std::string& session_id,
                    const DeviceInfo& device);
    std::vector<DeviceInfo> get_devices(const std::string& session_id);
    DeviceInfo get_primary_device(const std::string& session_id);
    bool set_primary_device(const std::string& session_id, 
                        const std::string& device_id);

    bool save_shell_state(const std::string& session_id, 
                          const ShellState& state);
    ShellState load_shell_state(const std::string& session_id);

    bool save_output_chunk(const std::string& session_id, 
                       uint64_t offset, 
                       const std::string& data);
    std::string load_output_chunk(const std::string& session_id, 
                                uint64_t offset, 
                                size_t length);
    uint64_t get_output_size(const std::string& session_id);

    bool publish_to_channel(const std::string& channel, 
                           const std::string& message);
    bool subscribe_channel(const std::string& channel, 
                          std::function<void(const std::string&)> callback);
    bool unsubscribe_channel(const std::string& channel);

    void set_connection_params(const std::string& host, int port) {
        redis_host_ = host;
        redis_port_ = port;
    }

    bool use_local_storage(bool local) { use_local_ = local; }
    bool is_using_local() const { return use_local_; }

    bool save_user_sessions(const std::string& user_id, 
                           const std::vector<std::string>& session_ids);
    std::vector<std::string> get_user_sessions(const std::string& user_id);
    bool remove_user_session(const std::string& user_id, 
                          const std::string& session_id);

private:
    std::string redis_host_;
    int redis_port_;
    std::string redis_password_;
    int redis_db_;
    bool connected_;
    bool use_local_;
    uint64_t ttl_seconds_;
    
    std::map<std::string, SessionDeviceState> local_session_states_;
    std::map<std::string, ShellState> local_shell_states_;
    std::map<std::string, std::map<uint64_t, std::string>> local_output_;
    std::map<std::string, std::vector<std::string>> local_user_sessions_;
    std::map<std::string, std::function<void(const std::string&)>> local_subscribers_;
    
    std::mutex mutex_;
    void* redis_context_;
};

struct RoamingPacket {
    enum Type {
        DEVICE_JOIN = 0,
        DEVICE_LEAVE = 1,
        DEVICE_STEAL = 2,
        OUTPUT_APPEND = 3,
        STATE_UPDATE = 4,
        HEARTBEAT = 5,
        NOTIFICATION = 6
    };
    
    Type type;
    std::string session_id;
    std::string device_id;
    std::string user_id;
    uint64_t timestamp;
    std::string payload;
};

class RoamingManager {
public:
    RoamingManager();
    ~RoamingManager();

    void set_session_store(std::shared_ptr<RedisSessionStore> store) {
        store_ = store;
    }
    
    void set_permission_manager(std::shared_ptr<PermissionManager> perm_mgr) {
        permission_mgr_ = perm_mgr;
    }

    bool handle_device_join(const std::string& session_id,
                        const DeviceInfo& device,
                        const std::string& user_id,
                        SessionAccessMode mode = SessionAccessMode::STEAL);

    bool handle_device_leave(const std::string& session_id,
                            const std::string& device_id);

    bool handle_device_steal(const std::string& session_id,
                            const DeviceInfo& new_device,
                            const std::string& user_id);

    bool handle_output_append(const std::string& session_id,
                            uint64_t offset,
                            const std::string& data);

    bool handle_state_update(const std::string& session_id,
                          const ShellState& state);

    ShellState get_current_state(const std::string& session_id);
    std::string get_output_since(const std::string& session_id, uint64_t offset);

    bool notify_device_readonly(const std::string& session_id,
                              const std::string& device_id);

    int get_active_device_count(const std::string& session_id);
    bool can_accept_new_device(const std::string& session_id,
                              const std::string& user_id,
                              SessionAccessMode mode);

    void set_max_devices_per_session(int max) { max_devices_ = max; }

private:
    std::shared_ptr<RedisSessionStore> store_;
    std::shared_ptr<PermissionManager> permission_mgr_;
    int max_devices_;
    std::mutex mutex_;
};

}
