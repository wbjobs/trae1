#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "protocol.h"

namespace moshpp {

struct CongestionControl {
    uint32_t cwnd;
    uint32_t ssthresh;
    uint32_t rtt;
    uint32_t rtt_var;
    uint32_t min_rtt;
    uint64_t last_rtt_update;
    uint32_t duplicates;
    uint32_t recovery_start;
};

struct NetworkEndpoint {
    std::string ip;
    uint16_t port;
    sockaddr_in addr;
    uint64_t last_seen;

    bool operator==(const NetworkEndpoint& other) const {
        return ip == other.ip && port == other.port;
    }
};

class UDPTransport {
public:
    using DataCallback = std::function<void(const std::vector<uint8_t>&, const NetworkEndpoint&)>;
    using ConnectCallback = std::function<void(const std::string&, const NetworkEndpoint&)>;
    using DisconnectCallback = std::function<void(const std::string&)>;

    UDPTransport();
    ~UDPTransport();

    bool bind(uint16_t port);
    bool connect(const std::string& host, uint16_t port);
    void close();

    void set_data_callback(DataCallback cb) { data_cb_ = cb; }
    void set_connect_callback(ConnectCallback cb) { connect_cb_ = cb; }
    void set_disconnect_callback(DisconnectCallback cb) { disconnect_cb_ = cb; }

    bool send_data(const std::string& session_id, const std::vector<uint8_t>& data);
    bool send_packet(const NetworkEndpoint& endpoint, const std::vector<uint8_t>& packet);

    void register_session(const std::string& session_id, const NetworkEndpoint& endpoint);
    void update_session_endpoint(const std::string& session_id, const NetworkEndpoint& endpoint);
    NetworkEndpoint get_session_endpoint(const std::string& session_id);

    void start_receive_thread();
    void stop_receive_thread();

    uint32_t get_rtt(const std::string& session_id);
    void update_rtt(const std::string& session_id, uint32_t rtt_sample);

    uint32_t get_cwnd(const std::string& session_id);
    void on_packet_ack(const std::string& session_id, uint32_t ack_num);
    void on_packet_loss(const std::string& session_id);

    bool detect_ip_change(const std::string& session_id);
    std::string get_current_ip();

private:
    int socket_fd_;
    std::atomic<bool> running_;
    std::thread receive_thread_;
    std::mutex mutex_;

    std::map<std::string, NetworkEndpoint> session_endpoints_;
    std::map<std::string, CongestionControl> congestion_control_;
    std::map<std::string, uint32_t> next_seq_num_;
    std::map<std::string, std::deque<std::pair<uint32_t, std::vector<uint8_t>>>> retransmission_queue_;

    DataCallback data_cb_;
    ConnectCallback connect_cb_;
    DisconnectCallback disconnect_cb_;

    void receive_loop();
    void handle_packet(const std::vector<uint8_t>& packet_data, const NetworkEndpoint& from);
    void process_ack(const std::string& session_id, uint32_t ack_num);
    void retransmit_timed_out(const std::string& session_id);
    void adjust_congestion_window(const std::string& session_id, bool is_ack);
};

}
