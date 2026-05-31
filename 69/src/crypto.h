#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <openssl/evp.h>

namespace fusefs {

class Crypto {
public:
    static bool EncryptFile(const uint8_t* key,
                            const uint8_t* plaintext, size_t plaintext_len,
                            uint8_t* ciphertext, size_t& ciphertext_len);

    static bool DecryptFile(const uint8_t* key,
                            const uint8_t* ciphertext, size_t ciphertext_len,
                            uint8_t* plaintext, size_t& plaintext_len);

    static bool GenerateIV(uint8_t* iv, size_t iv_len);
    static bool GenerateRandomKey(uint8_t* key, size_t key_len);

    static bool RSAEncryptKey(EVP_PKEY* public_key,
                              const uint8_t* file_key, size_t file_key_len,
                              std::vector<uint8_t>& out_encrypted);

    static bool RSADecryptKey(EVP_PKEY* private_key,
                              const uint8_t* encrypted, size_t encrypted_len,
                              std::vector<uint8_t>& out_key);

    static void SecureClear(void* ptr, size_t len);
};

}
