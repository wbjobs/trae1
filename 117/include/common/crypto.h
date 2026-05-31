#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>

namespace moshpp {

class Crypto {
public:
    Crypto();
    ~Crypto();

    bool init(const std::vector<uint8_t>& key, const std::vector<uint8_t>& iv);
    bool generate_keys();

    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext);
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& iv);

    std::vector<uint8_t> hmac(const std::vector<uint8_t>& data);
    bool verify_hmac(const std::vector<uint8_t>& data, const std::vector<uint8_t>& expected_hmac);

    const std::vector<uint8_t>& get_key() const { return key_; }
    const std::vector<uint8_t>& get_iv() const { return iv_; }

    void rotate_iv();
    void rekey();

private:
    std::vector<uint8_t> key_;
    std::vector<uint8_t> iv_;
    std::vector<uint8_t> hmac_key_;
    EVP_CIPHER_CTX* encrypt_ctx_;
    EVP_CIPHER_CTX* decrypt_ctx_;
    uint64_t otp_counter_;

    std::vector<uint8_t> derive_otp_key(uint64_t counter);
};

}
