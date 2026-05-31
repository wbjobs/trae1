#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace sgxagg {
namespace encl {

constexpr size_t kAesGcmKeyLen = 32;
constexpr size_t kAesGcmNonceLen = 12;
constexpr size_t kAesGcmTagLen = 16;

// Enclave 内的安全随机数
bool random_bytes(uint8_t* buf, size_t n);

// Enclave 内 ECDH P-256 密钥生成（使用 OE crypto）
struct EcdhKeyPair {
    std::vector<uint8_t> priv;   // 私钥 raw
    std::vector<uint8_t> pub;    // 未压缩公钥 65 字节
};

bool ecdh_generate(EcdhKeyPair& kp);

// 基于本地私钥 + 远端公钥 65 字节 -> 共享秘密
std::vector<uint8_t> ecdh_derive(const std::vector<uint8_t>& priv_raw,
                                 const uint8_t* their_pub);

// HKDF-SHA256
std::vector<uint8_t> hkdf_sha256(const uint8_t* ikm, size_t ikm_len,
                                 const uint8_t* info, size_t info_len,
                                 size_t out_len);

// AES-GCM 解密（enclave 内解密客户端提交的加密数据，可选 AAD）
bool aes_gcm_decrypt(const uint8_t* key, const uint8_t* input, size_t in_len,
                     std::vector<uint8_t>& out,
                     const uint8_t* aad = nullptr, size_t aad_len = 0);

// AES-GCM 加密（enclave 内加密聚合结果返回给客户端）
std::vector<uint8_t> aes_gcm_encrypt(const uint8_t* key,
                                     const uint8_t* plaintext, size_t pt_len,
                                     const uint8_t* aad = nullptr, size_t aad_len = 0);

} // namespace encl
} // namespace sgxagg
