#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace sgxagg {
namespace crypto {

// —— ECDH P-256 密钥生成与协商（OpenSSL 实现，客户端/主机使用）——
struct EcdhKeyPair {
    std::vector<uint8_t> priv;   // DER 格式私钥
    std::vector<uint8_t> pub;    // 未压缩公钥 65 字节 (0x04 || X || Y)
};

bool ecdh_generate(EcdhKeyPair& kp);

// 从公钥字节（65 字节）+ 私钥 DER 派生共享秘密
std::vector<uint8_t> ecdh_derive(const std::vector<uint8_t>& our_priv_der,
                                 const uint8_t* their_pub, size_t their_pub_len);

// HKDF-SHA256: 基于共享秘密派生任意长度密钥
std::vector<uint8_t> hkdf_sha256(const uint8_t* ikm, size_t ikm_len,
                                 const uint8_t* info, size_t info_len,
                                 size_t out_len);

// 便捷：把共享秘密 + session_id + "aggregator-session-v1" 派生 32 字节会话密钥
std::vector<uint8_t> derive_session_key(const std::vector<uint8_t>& shared,
                                        uint64_t session_id);

// —— AES-256-GCM ——
// nonce 必须 12 字节
constexpr size_t kAesGcmNonceLen = 12;
constexpr size_t kAesGcmTagLen = 16;
constexpr size_t kAesGcmKeyLen = 32;

// 输出格式：nonce(12) || ciphertext || tag(16)
std::vector<uint8_t> aes_gcm_encrypt(const uint8_t* key, const uint8_t* plaintext, size_t pt_len,
                                     const uint8_t* aad = nullptr, size_t aad_len = 0);
bool aes_gcm_decrypt(const uint8_t* key, const uint8_t* input, size_t in_len,
                     std::vector<uint8_t>& out,
                     const uint8_t* aad = nullptr, size_t aad_len = 0);

// 随机字节
bool random_bytes(uint8_t* buf, size_t n);

} // namespace crypto
} // namespace sgxagg
