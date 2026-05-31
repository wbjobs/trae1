#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

#include <openenclave/host.h>
#include "protocol.h"
#include "crypto.h"

namespace sgxagg {
namespace host {

// Enclave 宿主管理器：负责创建 / 销毁 enclave，封装 ecall
class EnclaveHost
{
public:
    static EnclaveHost& instance();

    bool create(const std::string& enclave_signed_path);
    void destroy();

    // —— ecall 包装 ——
    // 获取 enclave quote（内部自动绑定 enclave 的 ECDH 公钥）
    bool get_quote(std::vector<uint8_t>& out_quote);

    // 获取 enclave 的 ECDH 公钥（65 字节，未压缩）
    bool get_enclave_ecdh_pub(uint8_t out_pub[65]);

    // 派生会话密钥（在 enclave 内完成，派生后存到 host 侧会话管理器）
    bool derive_session_key(const uint8_t client_pub[65],
                            uint64_t session_id,
                            uint8_t out_key[32]);

    // 提交明文记录（已经由 host 用会话密钥解密）到 enclave 聚合
    bool submit_records(const uint8_t* plain, size_t len);

    // 获取聚合结果（明文）
    bool get_aggregates(AggregateResult& out);

    oe_enclave_t* enclave() const { return enclave_; }

private:
    EnclaveHost() = default;
    ~EnclaveHost() { destroy(); }

    oe_enclave_t* enclave_ = nullptr;
    bool created_ = false;
};

} // namespace host
} // namespace sgxagg
