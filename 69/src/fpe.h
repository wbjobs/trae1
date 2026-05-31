#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fusefs {

class FPE {
public:
    static bool Encrypt(const uint8_t* key,
                        const uint8_t* tweak, size_t tweak_len,
                        const uint8_t* input, size_t input_len,
                        uint8_t* output);

    static bool Decrypt(const uint8_t* key,
                        const uint8_t* tweak, size_t tweak_len,
                        const uint8_t* input, size_t input_len,
                        uint8_t* output);

    static std::string EncryptFilename(const uint8_t* key,
                                       const std::string& plaintext,
                                       const std::string& parent_path);

    static std::string DecryptFilename(const uint8_t* key,
                                       const std::string& ciphertext,
                                       const std::string& parent_path);

private:
    static bool FF1(bool encrypt,
                    const uint8_t* key,
                    const uint8_t* tweak, size_t tweak_len,
                    const uint8_t* input, size_t input_len,
                    uint8_t* output);

    static void PRF(const uint8_t* key, const uint8_t* data, size_t data_len, uint8_t* output);

    static void Num(const uint8_t* data, size_t len, uint8_t* out, size_t out_len);

    static void Str(const uint8_t* num, size_t num_len, uint8_t* out, size_t out_len);

    static void BigNumMod(const uint8_t* a, size_t a_len,
                          const uint8_t* b, size_t b_len,
                          uint8_t* result, size_t result_len);

    static void BigNumAdd(const uint8_t* a, size_t a_len,
                          const uint8_t* b, size_t b_len,
                          uint8_t* result, size_t result_len);

    static void BigNumModAdd(const uint8_t* a, size_t a_len,
                             const uint8_t* b, size_t b_len,
                             const uint8_t* mod, size_t mod_len,
                             uint8_t* result, size_t result_len);

    static void BigNumModSub(const uint8_t* a, size_t a_len,
                             const uint8_t* b, size_t b_len,
                             const uint8_t* mod, size_t mod_len,
                             uint8_t* result, size_t result_len);

    static void ModPowRadix(size_t exponent, uint8_t* result, size_t result_len);
};

}
