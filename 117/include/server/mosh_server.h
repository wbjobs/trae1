#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <thread>
#include <map>
#include <memory>
#include "common/udp_transport.h"
#include "common/session.h"
#include "common/crypto.h"
#include "common/snapshot.h"
#include "common/auth.h"
#include "common/redis_store.h"
#include "common/crdt.h"

namespace moshpp {

class MoshServer {
public:
    MoshServer();
    ~MoshServer();

    bool start(uint16_t port = DEFAULT_PORT);
    void stop();
    void wait();

    void set_session_timeout(uint64_t timeout_ms) { session_timeout_ = timeout_ms; }
    uint64_t get_session_timeout() const { return session_timeout_; }
    
    void set_redis_host(const std::string& host) { redis_host_ = host; }
    void set_redis_port(int port) { redis_port_ = port; }
    void set_snapshot_dir(const std::string& dir) { snapshot_manager_.set_snapshot_dir(dir); }
    
    bool enable_roaming() { return init_roaming(); }
    bool list_sessions(std::vector<std::string>& sessions);
    bool replay_session(const std::string& session_id);

private:
    std::atomic<bool> running_;
    uint16_t port_;
    UDPTransport transport_;
    SessionManager session_manager_;
    SnapshotManager snapshot_manager_;
    std::thread cleanup_thread_;
    std::thread snapshot_thread_;
    uint64_t session_timeout_;
    uint32_t snapshot_interval_;
    
    std::shared_ptr<AuthManager> auth_manager_;
    std::shared_ptr<PermissionManager> permission_manager_;
    std::shared_ptr<RedisSessionStore> session_store_;
    std::shared_ptr<RoamingManager> roaming_manager_;
    std::shared_ptr<CRDTSyncManager> crdt_manager_;
    std::shared_ptr<TerminalSyncManager> terminal_sync_manager_;
    
    std::string redis_host_;
    int redis_port_;
    bool roaming_enabled_;
    
    std::map<std::string, std::string> session_device_ids_;
    std::map<std::string, std::string> session_user_ids_;
    
    bool init_roaming();
    void handle_roaming_sync(const std::string& session_id);
    void sync_session_state(const std::string& session_id);

    void on_data_received(const std::vector<uint8_t>& data, const NetworkEndpoint& from);
    void on_client_connected(const std::string& session_id, const NetworkEndpoint& endpoint);
    void on_client_disconnected(const std::string& session_id);

    void handle_hello(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from);
    void handle_resume(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from);
    void handle_data(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from);
    void handle_ack(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from);
    void handle_ping(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from);
    void handle_fin(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from);
    void handle_delta_request(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from);
    
    void handle_roaming_join(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from);
    void handle_roaming_leave(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from);
    void handle_roaming_sync(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from);

    void send_hello_ack(const std::string& session_id, const NetworkEndpoint& endpoint, const std::vector<uint8_t>& key);
    void send_resume_ack(const std::string& session_id, const NetworkEndpoint& endpoint);
    void send_pong(const std::string& session_id, const NetworkEndpoint& endpoint);
    void send_fin_ack(const std::string& session_id, const NetworkEndpoint& endpoint);
    void send_error(const NetworkEndpoint& endpoint, const std::string& message);
    void send_delta_response(const std::string& session_id, const NetworkEndpoint& endpoint, 
                             const DeltaSyncResponse& response);
    void send_ack(const std::string& session_id, const NetworkEndpoint& endpoint, uint32_t seq_num);
    void send_roaming_notification(const std::string& session_id, const std::string& device_id, const std::string& message);

    void cleanup_loop();
    void snapshot_loop();
    void forward_pty_to_network(std::shared_ptr<Session> session);
    void forward_network_to_pty(std::shared_ptr<Session> session, const std::vector<uint8_t>& data);

    bool spawn_shell(std::shared_ptr<Session> session);
    void attach_to_tmux(std::shared_ptr<Session> session);
    void create_tmux_session(std::shared_ptr<Session> session);
};

}
