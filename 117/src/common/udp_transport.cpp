#include "common/udp_transport.h"
#include "common/utils.h"
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>

namespace moshpp {

UDPTransport::UDPTransport()
    : socket_fd_(-1)
    , running_(false)
{
}

UDPTransport::~UDPTransport() {
    close();
}

bool UDPTransport::bind(uint16_t port) {
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd_ < 0) {
        Logger::error("Failed to create socket: " + std::string(strerror(errno)));
        return false;
    }

    int opt = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        Logger::warn("Failed to set SO_REUSEADDR");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        Logger::error("Failed to bind socket: " + std::string(strerror(errno)));
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    Logger::info("UDP transport bound to port " + std::to_string(port));
    return true;
}

bool UDPTransport::connect(const std::string& host, uint16_t port) {
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd_ < 0) {
        Logger::error("Failed to create socket: " + std::string(strerror(errno)));
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* result;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0) {
        Logger::error("Failed to resolve host: " + host);
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    if (::connect(socket_fd_, result->ai_addr, result->ai_addrlen) < 0) {
        Logger::error("Failed to connect socket: " + std::string(strerror(errno)));
        freeaddrinfo(result);
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    freeaddrinfo(result);
    return true;
}

void UDPTransport::close() {
    running_ = false;
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
}

void UDPTransport::register_session(const std::string& session_id, const NetworkEndpoint& endpoint) {
    std::lock_guard<std::mutex> lock(mutex_);
    session_endpoints_[session_id] = endpoint;
    congestion_control_[session_id] = {
        .cwnd = 1,
        .ssthresh = 64,
        .rtt = 100,
        .rtt_var = 50,
        .min_rtt = 100,
        .last_rtt_update = current_time_ms(),
        .duplicates = 0,
        .recovery_start = 0
    };
    next_seq_num_[session_id] = 1;
}

void UDPTransport::update_session_endpoint(const std::string& session_id, const NetworkEndpoint& endpoint) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (session_endpoints_.find(session_id) != session_endpoints_.end()) {
        Logger::info("Updating endpoint for session " + session_id + ": " + 
            session_endpoints_[session_id].ip + ":" + std::to_string(session_endpoints_[session_id].port) +
            " -> " + endpoint.ip + ":" + std::to_string(endpoint.port));
    }
    session_endpoints_[session_id] = endpoint;
}

NetworkEndpoint UDPTransport::get_session_endpoint(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = session_endpoints_.find(session_id);
    if (it != session_endpoints_.end()) {
        return it->second;
    }
    return NetworkEndpoint{};
}

void UDPTransport::start_receive_thread() {
    running_ = true;
    receive_thread_ = std::thread(&UDPTransport::receive_loop, this);
}

void UDPTransport::stop_receive_thread() {
    running_ = false;
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
}

void UDPTransport::receive_loop() {
    std::vector<uint8_t> buffer(MAX_PACKET_SIZE);
    sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);

    while (running_) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(socket_fd_, &read_fds);
        
        timeval tv{0, 100000};
        int ret = select(socket_fd_ + 1, &read_fds, nullptr, nullptr, &tv);
        
        if (ret < 0) {
            if (errno != EINTR) {
                Logger::error("Select error: " + std::string(strerror(errno)));
            }
            continue;
        }
        
        if (ret == 0) {
            continue;
        }

        ssize_t n = recvfrom(socket_fd_, buffer.data(), buffer.size(), 0,
                            reinterpret_cast<sockaddr*>(&from_addr), &from_len);
        if (n < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                Logger::error("Receive error: " + std::string(strerror(errno)));
            }
            continue;
        }

        NetworkEndpoint from;
        from.ip = inet_ntoa(from_addr.sin_addr);
        from.port = ntohs(from_addr.sin_port);
        from.addr = from_addr;
        from.last_seen = current_time_ms();

        std::vector<uint8_t> packet_data(buffer.begin(), buffer.begin() + n);
        handle_packet(packet_data, from);
    }
}

void UDPTransport::handle_packet(const std::vector<uint8_t>& packet_data, const NetworkEndpoint& from) {
    if (packet_data.size() < sizeof(PacketHeader)) {
        Logger::warn("Received packet too small");
        return;
    }

    const PacketHeader* header = reinterpret_cast<const PacketHeader*>(packet_data.data());
    std::string session_id(reinterpret_cast<const char*>(header->session_id), SESSION_ID_LENGTH);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (session_endpoints_.find(session_id) == session_endpoints_.end()) {
            if (header->type != PacketType::HELLO && header->type != PacketType::RESUME) {
                Logger::warn("Unknown session: " + bytes_to_hex(std::vector<uint8_t>(header->session_id, header->session_id + SESSION_ID_LENGTH)));
                return;
            }
        }
    }

    if (data_cb_) {
        data_cb_(packet_data, from);
    }
}

bool UDPTransport::send_data(const std::string& session_id, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = session_endpoints_.find(session_id);
    if (it == session_endpoints_.end()) {
        return false;
    }

    ssize_t sent = sendto(socket_fd_, data.data(), data.size(), 0,
                          reinterpret_cast<const sockaddr*>(&it->second.addr), sizeof(it->second.addr));
    return sent == static_cast<ssize_t>(data.size());
}

bool UDPTransport::send_packet(const NetworkEndpoint& endpoint, const std::vector<uint8_t>& packet) {
    ssize_t sent = sendto(socket_fd_, packet.data(), packet.size(), 0,
                          reinterpret_cast<const sockaddr*>(&endpoint.addr), sizeof(endpoint.addr));
    return sent == static_cast<ssize_t>(packet.size());
}

uint32_t UDPTransport::get_rtt(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = congestion_control_.find(session_id);
    if (it != congestion_control_.end()) {
        return it->second.rtt;
    }
    return 100;
}

void UDPTransport::update_rtt(const std::string& session_id, uint32_t rtt_sample) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = congestion_control_.find(session_id);
    if (it == congestion_control_.end()) {
        return;
    }

    CongestionControl& cc = it->second;
    const uint32_t alpha = 125;
    const uint32_t beta = 250;

    int64_t delta = static_cast<int64_t>(rtt_sample) - static_cast<int64_t>(cc.rtt);
    cc.rtt = static_cast<uint32_t>(cc.rtt + (alpha * delta) / 1000);
    
    uint32_t abs_delta = static_cast<uint32_t>(std::abs(delta));
    cc.rtt_var = static_cast<uint32_t>(cc.rtt_var + (beta * (static_cast<int64_t>(abs_delta) - static_cast<int64_t>(cc.rtt_var))) / 1000);

    if (rtt_sample < cc.min_rtt) {
        cc.min_rtt = rtt_sample;
    }

    cc.last_rtt_update = current_time_ms();
}

uint32_t UDPTransport::get_cwnd(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = congestion_control_.find(session_id);
    if (it != congestion_control_.end()) {
        return it->second.cwnd;
    }
    return 1;
}

void UDPTransport::on_packet_ack(const std::string& session_id, uint32_t ack_num) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = congestion_control_.find(session_id);
    if (it != congestion_control_.end()) {
        adjust_congestion_window(session_id, true);
    }
    process_ack(session_id, ack_num);
}

void UDPTransport::on_packet_loss(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = congestion_control_.find(session_id);
    if (it == congestion_control_.end()) {
        return;
    }

    CongestionControl& cc = it->second;
    cc.ssthresh = std::max(2u, cc.cwnd / 2);
    cc.cwnd = 1;
    cc.duplicates = 0;
    Logger::debug("Packet loss detected, cwnd reduced to " + std::to_string(cc.cwnd));
}

void UDPTransport::adjust_congestion_window(const std::string& session_id, bool is_ack) {
    auto it = congestion_control_.find(session_id);
    if (it == congestion_control_.end()) {
        return;
    }

    CongestionControl& cc = it->second;
    if (cc.cwnd < cc.ssthresh) {
        cc.cwnd += 1;
    } else {
        cc.cwnd += std::max(1u, 1024 / cc.cwnd);
    }
    
    const uint32_t max_cwnd = 1024;
    if (cc.cwnd > max_cwnd) {
        cc.cwnd = max_cwnd;
    }
}

void UDPTransport::process_ack(const std::string& session_id, uint32_t ack_num) {
    auto it = retransmission_queue_.find(session_id);
    if (it == retransmission_queue_.end()) {
        return;
    }

    auto& queue = it->second;
    while (!queue.empty() && queue.front().first <= ack_num) {
        queue.pop_front();
    }
}

void UDPTransport::retransmit_timed_out(const std::string& session_id) {
    auto it = retransmission_queue_.find(session_id);
    if (it == retransmission_queue_.end()) {
        return;
    }

    auto endpoint_it = session_endpoints_.find(session_id);
    if (endpoint_it == session_endpoints_.end()) {
        return;
    }

    auto& queue = it->second;
    uint64_t now = current_time_ms();
    auto& cc = congestion_control_[session_id];
    uint64_t timeout = cc.rtt + 4 * cc.rtt_var;

    for (auto& item : queue) {
        send_packet(endpoint_it->second, item.second);
    }
}

bool UDPTransport::detect_ip_change(const std::string& session_id) {
    std::string current_ip = get_current_ip();
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = session_endpoints_.find(session_id);
    if (it == session_endpoints_.end()) {
        return false;
    }

    if (it->second.ip != current_ip && !current_ip.empty()) {
        Logger::info("IP change detected: " + it->second.ip + " -> " + current_ip);
        return true;
    }
    return false;
}

std::string UDPTransport::get_current_ip() {
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        return "";
    }

    std::string result;
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (std::string(ifa->ifa_name) == "lo") continue;

        sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ip_str, INET_ADDRSTRLEN);
        
        std::string ip(ip_str);
        if (ip.substr(0, 3) != "127" && ip.substr(0, 7) != "169.254") {
            result = ip;
            break;
        }
    }

    freeifaddrs(ifaddr);
    return result;
}

}
