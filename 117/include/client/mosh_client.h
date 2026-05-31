#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <thread>
#include <memory>
#include "common/udp_transport.h"
#include "common/session.h"
#include "common/crypto.h"
#include "common/auth.h"
#include "common/redis_store.h"
#include "common/crdt.h"

namespace moshpp {

class MoshClient {
public:
    MoshClient();
    ~MoshClient();

    bool connect(const std::string& host, uint16_t port, const std::string& session_id = "");
    bool new_session(const std::string& host, uint16_t port);
    bool attach_session(const std::string& host, uint16_t port, const std::string& session_id);
    void disconnect();
    void run();

    void set_terminal_size(uint32_t rows, uint32_t cols);
    void send_input(const std::vector<uint8_t>& data);

    const std::string& get_session_id() const { return session_id_; }
    bool is_connected() const { return connected_; }
    
    void set_device_id(const std::string& device_id) { device_id_ = device_id; }
    void set_device_name(const std::string& device_name) { device_name_ = device_name; }
    void set_user_id(const std::string& user_id) { user_id_ = user_id; }
    void set_access_mode(SessionAccessMode mode) { access_mode_ = mode; }
    
    bool is_readonly() const { return is_readonly_; }

private:
    std::atomic<bool> running_;
    std::atomic<bool> connected_;
    std::string session_id_;
    std::string host_;
    uint16_t port_;
    NetworkEndpoint server_endpoint_;

    UDPTransport transport_;
    std::shared_ptr<Session> session_;
    Crypto crypto_;
    
    std::string device_id_;
    std::string device_name_;
    std::string user_id_;
    SessionAccessMode access_mode_;
    bool is_readonly_;
    
    std::shared_ptr<CRDTSyncManager> crdt_manager_;
    std::shared_ptr<TerminalSyncManager> terminal_sync_manager_;

    std::thread input_thread_;
    std::thread output_thread_;
    std::thread reconnect_thread_;

    uint32_t terminal_rows_;
    uint32_t terminal_cols_;

    uint64_t last_snapshot_id_;
    uint64_t last_output_offset_;
    std::vector<uint8_t> output_buffer_;

    bool perform_handshake();
    bool resume_session();
    bool perform_roaming_join();
    void on_data_received(const std::vector<uint8_t>& data, const NetworkEndpoint& from);

    void handle_hello_ack(const PacketHeader& header, const std::vector<uint8_t>& data);
    void handle_resume_ack(const PacketHeader& header, const std::vector<uint8_t>& data);
    void handle_data(const PacketHeader& header, const std::vector<uint8_t>& data);
    void handle_ack(const PacketHeader& header, const std::vector<uint8_t>& data);
    void handle_pong(const PacketHeader& header, const std::vector<uint8_t>& data);
    void handle_fin_ack(const PacketHeader& header, const std::vector<uint8_t>& data);
    void handle_sync(const PacketHeader& header, const std::vector<uint8_t>& data);
    void handle_delta_response(const PacketHeader& header, const std::vector<uint8_t>& data);
    void handle_roaming_join_ack(const PacketHeader& header, const std::vector<uint8_t>& data);
    void handle_roaming_sync(const PacketHeader& header, const std::vector<uint8_t>& data);
    void handle_roaming_notification(const PacketHeader& header, const std::vector<uint8_t>& data);
    void handle_roaming_state(const PacketHeader& header, const std::vector<uint8_t>& data);

    void send_hello();
    void send_resume();
    void send_ack(uint32_t ack_num);
    void send_ping();
    void send_fin();
    void send_sync();
    void send_delta_request();
    void send_roaming_join();
    void send_roaming_leave();

    void input_thread_func();
    void output_thread_func();
    void reconnect_thread_func();

    void setup_terminal();
    void restore_terminal();
    void render_screen(const std::vector<uint8_t>& data);

    bool check_network_change();
    void attempt_reconnect();
    void sync_terminal_state();
};

}
