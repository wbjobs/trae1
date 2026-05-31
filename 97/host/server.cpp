#include "server.h"
#include "enclave_host.h"
#include "session_manager.h"
#include "crypto.h"

#include <iostream>
#include <cstring>
#include <algorithm>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

namespace sgxagg { namespace host {

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

Server::Server(const std::string& listen_host, uint16_t port)
    : host_(listen_host), port_(port) {}

Server::~Server() { stop(); }

bool Server::start(int num_threads) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) { perror("socket"); return false; }
    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    ::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
    if (::bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return false; }
    if (::listen(listen_fd_, 128) < 0) { perror("listen"); return false; }

    epoll_fd_ = ::epoll_create1(0);
    if (epoll_fd_ < 0) { perror("epoll_create1"); return false; }

    running_ = true;
    accept_thread_ = std::thread([this]() { accept_loop(); });
    for (int i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this]() { worker_loop(); });
    }
    return true;
}

void Server::stop() {
    if (!running_.exchange(false)) return;
    if (listen_fd_ >= 0) { ::shutdown(listen_fd_, SHUT_RDWR); ::close(listen_fd_); listen_fd_ = -1; }
    if (epoll_fd_ >= 0) { ::close(epoll_fd_); epoll_fd_ = -1; }
    if (accept_thread_.joinable()) accept_thread_.join();
    for (auto& t : workers_) if (t.joinable()) t.join();
    workers_.clear();
    std::lock_guard<std::mutex> lk(conns_mtx_);
    for (auto& kv : conns_) delete kv.second;
    conns_.clear();
}

void Server::accept_loop() {
    while (running_) {
        sockaddr_in client_addr;
        socklen_t alen = sizeof(client_addr);
        int fd = ::accept(listen_fd_, (sockaddr*)&client_addr, &alen);
        if (fd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        set_nonblock(fd);

        Conn* c = new Conn;
        c->fd = fd;
        {
            std::lock_guard<std::mutex> lk(conns_mtx_);
            conns_[fd] = c;
        }

        epoll_event ev;
        ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
        ev.data.ptr = c;
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    }
}

void Server::worker_loop() {
    constexpr int kMaxEvents = 64;
    epoll_event events[kMaxEvents];
    while (running_) {
        int n = ::epoll_wait(epoll_fd_, events, kMaxEvents, 500);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < n; ++i) {
            Conn* c = (Conn*)events[i].data.ptr;
            if (!c) continue;
            uint32_t ev = events[i].events;
            if (ev & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
                std::lock_guard<std::mutex> lk(conns_mtx_);
                conns_.erase(c->fd);
                delete c;
                continue;
            }
            if (ev & EPOLLIN) {
                uint8_t buf[4096];
                ssize_t r = ::read(c->fd, buf, sizeof(buf));
                if (r <= 0) {
                    std::lock_guard<std::mutex> lk(conns_mtx_);
                    conns_.erase(c->fd);
                    delete c;
                    continue;
                }
                c->recv_buf.insert(c->recv_buf.end(), buf, buf + r);

                // 解析完整帧
                while (c->recv_buf.size() - c->recv_consumed >= kFrameHeader) {
                    const uint8_t* frame = c->recv_buf.data() + c->recv_consumed;
                    uint32_t type = read_be32(frame);
                    uint32_t len = read_be32(frame + 4);
                    if (c->recv_buf.size() - c->recv_consumed < kFrameHeader + len) break;
                    const uint8_t* payload = frame + kFrameHeader;

                    std::vector<uint8_t> resp = handle_frame(payload, len);
                    if (!resp.empty()) {
                        ::write(c->fd, resp.data(), resp.size());
                    }
                    c->recv_consumed += kFrameHeader + len;
                }

                // 清理已消费数据
                if (c->recv_consumed > 0 && c->recv_consumed == c->recv_buf.size()) {
                    c->recv_buf.clear();
                    c->recv_consumed = 0;
                } else if (c->recv_consumed > 1024) {
                    c->recv_buf.erase(c->recv_buf.begin(), c->recv_buf.begin() + c->recv_consumed);
                    c->recv_consumed = 0;
                }
            }
        }
    }
}

std::vector<uint8_t> Server::make_response(Status s, const uint8_t* payload, size_t plen) {
    std::vector<uint8_t> out(kFrameHeader + plen);
    write_be32(out.data(), (uint32_t)s);
    write_be32(out.data() + 4, (uint32_t)plen);
    if (plen > 0) std::memcpy(out.data() + kFrameHeader, payload, plen);
    return out;
}

std::vector<uint8_t> Server::handle_frame(const uint8_t* data, size_t len) {
    if (len < 4) return make_response(Status::BadRequest);
    RequestType type = (RequestType)read_be32(data);
    const uint8_t* payload = data + 4;
    size_t payload_len = len - 4;

    switch (type) {
        case RequestType::Ping:
            return make_response(Status::Ok);
        case RequestType::GetQuote:
            return handle_get_quote();
        case RequestType::Handshake:
            return handle_handshake(payload, payload_len);
        case RequestType::SubmitData:
            return handle_submit(payload, payload_len);
        case RequestType::GetAggregates:
            return handle_get_aggregates(payload, payload_len);
        case RequestType::CloseSession: {
            if (payload_len < 8) return make_response(Status::BadRequest);
            uint64_t sid = (uint64_t)read_be32(payload) << 32 | read_be32(payload + 4);
            SessionManager::instance().remove_session(sid);
            return make_response(Status::Ok);
        }
        default:
            return make_response(Status::BadRequest);
    }
}

std::vector<uint8_t> Server::handle_get_quote() {
    auto& eh = EnclaveHost::instance();

    // 从 enclave 获取 ECDH 公钥（用于密钥协商）
    uint8_t enclave_pub[65];
    if (!eh.get_enclave_ecdh_pub(enclave_pub))
        return make_response(Status::EnclaveError);

    // 获取 enclave quote（内部自动绑定 enclave 的 ECDH 公钥到 report_data）
    std::vector<uint8_t> quote;
    if (!eh.get_quote(quote))
        return make_response(Status::EnclaveError);

    // 响应 payload: [pubkey(65)][quote...]
    std::vector<uint8_t> payload(65 + quote.size());
    std::memcpy(payload.data(), enclave_pub, 65);
    std::memcpy(payload.data() + 65, quote.data(), quote.size());
    return make_response(Status::Ok, payload.data(), payload.size());
}

std::vector<uint8_t> Server::handle_handshake(const uint8_t* data, size_t len) {
    if (len < 65) return make_response(Status::BadRequest);

    // 客户端 payload: [client_ecdh_pub(65)]  [optional: ttl(4)]
    uint32_t ttl = 0;
    if (len >= 69) ttl = read_be32(data + 65);

    // —— 关键：先分配 session_id，再把 id 传给 enclave 派生密钥 ——
    // 这样 enclave 和客户端（在收到 sid 后）能推导出一致的密钥
    uint64_t sid = SessionManager::instance().create_session_dry();
    if (sid == 0) return make_response(Status::SessionLimitExceeded);

    // 在 enclave 内派生会话密钥（使用已分配的 sid）
    uint8_t derived_key[32];
    if (!EnclaveHost::instance().derive_session_key(data, sid, derived_key)) {
        SessionManager::instance().remove_session(sid);
        return make_response(Status::EnclaveError);
    }

    // 提交密钥到会话
    if (!SessionManager::instance().install_session_key(sid, derived_key, ttl)) {
        return make_response(Status::Internal);
    }

    uint8_t resp[8];
    write_be32(resp, (uint32_t)(sid >> 32));
    write_be32(resp + 4, (uint32_t)(sid & 0xFFFFFFFF));
    return make_response(Status::Ok, resp, sizeof(resp));
}

std::vector<uint8_t> Server::handle_submit(const uint8_t* data, size_t len) {
    // payload: [session_id(8)][AES-GCM(nonce||ct||tag)]
    if (len < 8) return make_response(Status::BadRequest);
    uint64_t sid = (uint64_t)read_be32(data) << 32 | read_be32(data + 4);
    const uint8_t* ct = data + 8;
    size_t ct_len = len - 8;

    Session* s = SessionManager::instance().get_session(sid);
    if (!s) return make_response(Status::SessionNotFound);

    // AAD = session_id 大端 8 字节，防止重放/篡改
    std::vector<uint8_t> plain;
    if (!crypto::aes_gcm_decrypt(s->key.data(), ct, ct_len, plain, data, 8)) {
        return make_response(Status::CryptoError);
    }

    // 提交明文到 enclave 聚合
    if (!EnclaveHost::instance().submit_records(plain.data(), plain.size())) {
        return make_response(Status::EnclaveError);
    }
    return make_response(Status::Ok);
}

std::vector<uint8_t> Server::handle_get_aggregates(const uint8_t* data, size_t len) {
    AggregateResult agg;
    if (!EnclaveHost::instance().get_aggregates(agg)) {
        return make_response(Status::EnclaveError);
    }
    auto plain = serialize_aggregate(agg);

    // 如果客户端指定了 session_id，则加密返回
    if (len >= 8) {
        uint64_t sid = (uint64_t)read_be32(data) << 32 | read_be32(data + 4);
        Session* s = SessionManager::instance().get_session(sid);
        if (s) {
            auto ct = crypto::aes_gcm_encrypt(s->key.data(), plain.data(), plain.size(), data, 8);
            // 响应：[session_id(8)][ciphertext]
            std::vector<uint8_t> payload(8 + ct.size());
            std::memcpy(payload.data(), data, 8);
            std::memcpy(payload.data() + 8, ct.data(), ct.size());
            return make_response(Status::Ok, payload.data(), payload.size());
        }
    }
    return make_response(Status::Ok, plain.data(), plain.size());
}

}} // sgxagg::host
