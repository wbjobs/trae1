#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <unordered_map>

#include "protocol.h"

namespace sgxagg {
namespace host {

// TCP 服务：支持多客户端并发，使用 epoll + 线程池
class Server
{
public:
    Server(const std::string& listen_host, uint16_t port);
    ~Server();

    bool start(int num_threads = 4);
    void stop();

private:
    struct Conn {
        int fd = -1;
        std::vector<uint8_t> recv_buf;
        size_t recv_consumed = 0;
        ~Conn() { if (fd >= 0) close(fd); }
    };

    void accept_loop();
    void worker_loop();

    // 处理一条完整帧
    std::vector<uint8_t> handle_frame(const uint8_t* data, size_t len);

    std::vector<uint8_t> handle_get_quote();
    std::vector<uint8_t> handle_handshake(const uint8_t* data, size_t len);
    std::vector<uint8_t> handle_submit(const uint8_t* data, size_t len);
    std::vector<uint8_t> handle_get_aggregates(const uint8_t* data, size_t len);

    static std::vector<uint8_t> make_response(Status s, const uint8_t* payload = nullptr, size_t plen = 0);

    std::string host_;
    uint16_t port_;
    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    std::vector<std::thread> workers_;

    std::unordered_map<int, Conn*> conns_;
    std::mutex conns_mtx_;
};

} // namespace host
} // namespace sgxagg
