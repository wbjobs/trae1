#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <chrono>
#include <random>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "protocol.h"
#include "crypto.h"

static int connect_to(const std::string& host, uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        ::close(fd);
        return -1;
    }
    return fd;
}

static bool send_frame(int fd, sgxagg::RequestType type,
                       const uint8_t* payload = nullptr, size_t plen = 0) {
    std::vector<uint8_t> frame(sgxagg::kFrameHeader + 4 + plen);
    sgxagg::write_be32(frame.data(), (uint32_t)type);
    sgxagg::write_be32(frame.data() + 4, (uint32_t)(4 + plen));
    sgxagg::write_be32(frame.data() + 8, (uint32_t)type);  // frame内type镜像
    if (plen > 0) std::memcpy(frame.data() + 12, payload, plen);
    ssize_t w = ::write(fd, frame.data(), frame.size());
    return w == (ssize_t)frame.size();
}

static bool read_response(int fd, sgxagg::Status& status, std::vector<uint8_t>& payload) {
    uint8_t hdr[sgxagg::kFrameHeader];
    ssize_t r = ::read(fd, hdr, sizeof(hdr));
    if (r != (ssize_t)sizeof(hdr)) return false;
    status = (sgxagg::Status)sgxagg::read_be32(hdr);
    uint32_t len = sgxagg::read_be32(hdr + 4);
    payload.resize(len);
    size_t got = 0;
    while (got < len) {
        r = ::read(fd, payload.data() + got, len - got);
        if (r <= 0) return false;
        got += r;
    }
    return true;
}

static std::vector<sgxagg::Record> generate_records(size_t n, uint32_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> inc_dist(50000, 15000);
    std::normal_distribution<double> age_dist(40, 12);
    std::vector<sgxagg::Record> recs(n);
    for (auto& r : recs) {
        r.income = std::max(10000.0, inc_dist(rng));
        int a = (int)age_dist(rng);
        r.age = std::max(18, std::min(90, a));
    }
    return recs;
}

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    uint16_t port = 7788;
    size_t num_batches = 5;
    size_t records_per_batch = 100;
    uint32_t session_ttl = 0;
    uint32_t party_id = 1;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--host" && i + 1 < argc) host = argv[++i];
        else if (a == "--port" && i + 1 < argc) port = (uint16_t)atoi(argv[++i]);
        else if (a == "--batches" && i + 1 < argc) num_batches = (size_t)atoi(argv[++i]);
        else if (a == "--records" && i + 1 < argc) records_per_batch = (size_t)atoi(argv[++i]);
        else if (a == "--party" && i + 1 < argc) party_id = (uint32_t)atoi(argv[++i]);
        else if (a == "--session-ttl" && i + 1 < argc) session_ttl = (uint32_t)atoi(argv[++i]);
        else if (a == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  --host <ip>             Server host (default 127.0.0.1)\n"
                      << "  --port <port>           Server port (default 7788)\n"
                      << "  --batches <n>           Number of batches to submit (default 5)\n"
                      << "  --records <n>           Records per batch (default 100)\n"
                      << "  --party <id>            Party ID for data generation seed (default 1)\n"
                      << "  --session-ttl <sec>     Request custom TTL for session (default uses server default)\n";
            return 0;
        }
    }

    std::cout << "[client] Connecting to " << host << ":" << port << std::endl;
    int fd = connect_to(host, port);
    if (fd < 0) return 1;

    sgxagg::Status st;
    std::vector<uint8_t> payload;

    // ========== 步骤 1：获取 Quote（含 enclave ECDH 公钥） ==========
    std::cout << "[client] Requesting quote..." << std::endl;
    if (!send_frame(fd, sgxagg::RequestType::GetQuote) ||
        !read_response(fd, st, payload)) {
        std::cerr << "Failed to get quote" << std::endl;
        ::close(fd); return 1;
    }
    if (st != sgxagg::Status::Ok || payload.size() < 65) {
        std::cerr << "Bad quote response, status=" << (uint32_t)st << std::endl;
        ::close(fd); return 1;
    }
    const uint8_t* enclave_pub = payload.data();
    std::vector<uint8_t> quote(payload.begin() + 65, payload.end());
    std::cout << "[client] Received enclave ECDH pubkey (65 bytes) and quote ("
              << quote.size() << " bytes)" << std::endl;
    std::cout << "[client] (Production: verify quote via IAS/DCAP before proceeding)" << std::endl;

    // ========== 步骤 2：客户端生成 ECDH 密钥对 ==========
    sgxagg::crypto::EcdhKeyPair client_kp;
    if (!sgxagg::crypto::ecdh_generate(client_kp)) {
        std::cerr << "Failed to generate client ECDH keys" << std::endl;
        ::close(fd); return 1;
    }

    // ========== 步骤 3：握手建立会话 ==========
    std::cout << "[client] Performing handshake to establish session..." << std::endl;
    std::vector<uint8_t> hs_payload(65 + (session_ttl > 0 ? 4 : 0));
    std::memcpy(hs_payload.data(), client_kp.pub.data(), 65);
    if (session_ttl > 0) sgxagg::write_be32(hs_payload.data() + 65, session_ttl);

    if (!send_frame(fd, sgxagg::RequestType::Handshake, hs_payload.data(), hs_payload.size()) ||
        !read_response(fd, st, payload)) {
        std::cerr << "Handshake failed" << std::endl;
        ::close(fd); return 1;
    }
    if (st != sgxagg::Status::Ok || payload.size() < 8) {
        std::cerr << "Bad handshake response, status=" << (uint32_t)st << std::endl;
        ::close(fd); return 1;
    }

    uint64_t session_id = (uint64_t)sgxagg::read_be32(payload.data()) << 32 |
                          sgxagg::read_be32(payload.data() + 4);
    std::cout << "[client] Session established, ID=" << session_id << std::endl;

    // 客户端本地派生会话密钥（和 enclave 派生出的一致）
    auto shared = sgxagg::crypto::ecdh_derive(client_kp.priv, enclave_pub, 65);
    auto session_key = sgxagg::crypto::derive_session_key(shared, session_id);
    std::cout << "[client] Session key derived locally (32 bytes)" << std::endl;

    // ========== 步骤 4：批量提交数据（会话复用，无需每次认证） ==========
    auto t0 = std::chrono::high_resolution_clock::now();
    size_t total_records = 0;

    for (size_t b = 0; b < num_batches; ++b) {
        auto recs = generate_records(records_per_batch, party_id * 10000 + (uint32_t)b);
        auto plain = sgxagg::serialize_records(recs);

        // AAD = session_id(8字节，大端)，保证完整性
        uint8_t aad[8];
        sgxagg::write_be32(aad, (uint32_t)(session_id >> 32));
        sgxagg::write_be32(aad + 4, (uint32_t)(session_id & 0xFFFFFFFF));

        auto ct = sgxagg::crypto::aes_gcm_encrypt(
            session_key.data(), plain.data(), plain.size(), aad, sizeof(aad));

        // 提交 payload = [session_id(8)][ciphertext]
        std::vector<uint8_t> submit_payload(8 + ct.size());
        std::memcpy(submit_payload.data(), aad, 8);
        std::memcpy(submit_payload.data() + 8, ct.data(), ct.size());

        if (!send_frame(fd, sgxagg::RequestType::SubmitData, submit_payload.data(), submit_payload.size()) ||
            !read_response(fd, st, payload)) {
            std::cerr << "Submit batch " << b << " failed" << std::endl;
            ::close(fd); return 1;
        }
        if (st != sgxagg::Status::Ok) {
            std::cerr << "Batch " << b << " rejected, status=" << (uint32_t)st << std::endl;
            ::close(fd); return 1;
        }
        total_records += recs.size();
        std::cout << "[client] Batch " << b << ": submitted " << recs.size() << " records" << std::endl;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "[client] Session-reuse throughput: " << total_records << " records in "
              << ms << " ms = " << (total_records * 1000.0 / ms) << " rec/sec" << std::endl;

    // ========== 步骤 5：获取加密聚合结果 ==========
    std::cout << "[client] Requesting aggregates (encrypted with session key)..." << std::endl;
    uint8_t req_aad[8];
    sgxagg::write_be32(req_aad, (uint32_t)(session_id >> 32));
    sgxagg::write_be32(req_aad + 4, (uint32_t)(session_id & 0xFFFFFFFF));

    if (!send_frame(fd, sgxagg::RequestType::GetAggregates, req_aad, 8) ||
        !read_response(fd, st, payload)) {
        std::cerr << "Get aggregates failed" << std::endl;
        ::close(fd); return 1;
    }
    if (st != sgxagg::Status::Ok || payload.size() < 8) {
        std::cerr << "Bad aggregates response" << std::endl;
        ::close(fd); return 1;
    }

    // payload = [session_id(8)][ciphertext]
    const uint8_t* ct = payload.data() + 8;
    size_t ct_len = payload.size() - 8;

    std::vector<uint8_t> plain_agg;
    if (!sgxagg::crypto::aes_gcm_decrypt(session_key.data(), ct, ct_len, plain_agg,
                                         payload.data(), 8)) {
        std::cerr << "Failed to decrypt aggregates" << std::endl;
        ::close(fd); return 1;
    }

    auto agg = sgxagg::deserialize_aggregate(plain_agg.data(), plain_agg.size());
    std::cout << "\n[client] Aggregate result (total records: " << agg.total_records << ")\n"
              << "  Income:\n"
              << "    Mean     = " << agg.mean_income << "\n"
              << "    Median   = " << agg.median_income << "\n"
              << "    Variance = " << agg.variance_income << "\n"
              << "    P25      = " << agg.quantile25_income << "\n"
              << "    P75      = " << agg.quantile75_income << "\n"
              << "  Age:\n"
              << "    Mean     = " << agg.mean_age << "\n"
              << "    Median   = " << agg.median_age << "\n"
              << "    Variance = " << agg.variance_age << "\n";

    // ========== 步骤 6：优雅关闭会话 ==========
    send_frame(fd, sgxagg::RequestType::CloseSession, req_aad, 8);
    ::close(fd);
    std::cout << "\n[client] Done. Session closed." << std::endl;
    return 0;
}
