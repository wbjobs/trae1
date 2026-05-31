#include "fpe.h"
#include "config.h"

#include <openssl/bn.h>
#include <openssl/evp.h>

#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace fusefs {

struct BNDeleter {
    void operator()(BIGNUM* bn) const { BN_free(bn); }
};

using BNPtr = std::unique_ptr<BIGNUM, BNDeleter>;

struct BN_CTXDeleter {
    void operator()(BN_CTX* ctx) const { BN_CTX_free(ctx); }
};

using BN_CTXPtr = std::unique_ptr<BN_CTX, BN_CTXDeleter>;

static void secure_bzero(void* ptr, size_t len) {
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    for (size_t i = 0; i < len; ++i) p[i] = 0;
}

static BNPtr BytesToBN(const uint8_t* data, size_t len) {
    BIGNUM* bn = BN_new();
    if (!bn) throw std::runtime_error("BN_new failed");
    if (BN_bin2bn(data, static_cast<int>(len), bn) == nullptr) {
        BN_free(bn);
        throw std::runtime_error("BN_bin2bn failed");
    }
    return BNPtr(bn);
}

static void BNToBytes(const BIGNUM* bn, uint8_t* out, size_t out_len) {
    int num_bytes = BN_num_bytes(bn);
    if (num_bytes > static_cast<int>(out_len)) {
        BN_bn2bin(bn, out);
    } else {
        std::memset(out, 0, out_len - num_bytes);
        BN_bn2bin(bn, out + (out_len - num_bytes));
    }
}

void FPE::PRF(const uint8_t* key, const uint8_t* data, size_t data_len, uint8_t* output) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to create cipher context");

    uint8_t iv[16];
    std::memset(iv, 0, 16);

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize AES-CBC");
    }

    int out_len = 0;
    std::vector<uint8_t> buf(data_len + 16);
    if (EVP_EncryptUpdate(ctx, buf.data(), &out_len, data, static_cast<int>(data_len)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AES-CBC update failed");
    }

    int final_len = 0;
    if (EVP_EncryptFinal_ex(ctx, buf.data() + out_len, &final_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AES-CBC final failed");
    }

    int total = out_len + final_len;
    if (total < 16) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("PRF output too short");
    }

    std::memcpy(output, buf.data() + total - 16, 16);
    secure_bzero(buf.data(), buf.size());
    EVP_CIPHER_CTX_free(ctx);
}

bool FPE::FF1(bool encrypt,
              const uint8_t* key,
              const uint8_t* tweak, size_t tweak_len,
              const uint8_t* input, size_t input_len,
              uint8_t* output) {
    if (input_len == 0) {
        return true;
    }

    BN_CTXPtr bn_ctx(BN_CTX_new());
    if (!bn_ctx) return false;

    size_t n = input_len;
    size_t u = n / 2;
    size_t v = n - u;

    BNPtr radix_pow_u(BN_new());
    BNPtr radix_pow_v(BN_new());
    BN_set_word(radix_pow_u.get(), 256);
    BN_set_word(radix_pow_v.get(), 256);

    BNPtr exp_u(BN_new());
    BNPtr exp_v(BN_new());
    BN_set_word(exp_u.get(), u);
    BN_set_word(exp_v.get(), v);

    BN_exp(radix_pow_u.get(), radix_pow_u.get(), exp_u.get(), bn_ctx.get());
    BN_exp(radix_pow_v.get(), radix_pow_v.get(), exp_v.get(), bn_ctx.get());

    BNPtr A(BN_new());
    BNPtr B(BN_new());

    A = BytesToBN(input, u);
    B = BytesToBN(input + u, v);

    size_t b_len = v;
    size_t m_len = (b_len > 4) ? b_len : 4;

    for (int round = 0; round < FPE_ROUNDS; ++round) {
        int round_idx = encrypt ? round : (FPE_ROUNDS - 1 - round);

        size_t q_len = tweak_len + 1 + b_len + b_len;
        std::vector<uint8_t> Q(q_len, 0);

        size_t offset = 0;
        std::memcpy(Q.data() + offset, tweak, tweak_len);
        offset += tweak_len;

        Q[offset++] = static_cast<uint8_t>(round_idx);

        BNToBytes(radix_pow_v.get(), Q.data() + offset, b_len);
        offset += b_len;

        if (encrypt) {
            BNToBytes(B.get(), Q.data() + offset, b_len);
        } else {
            BNToBytes(A.get(), Q.data() + offset, b_len);
        }

        uint8_t R[16];
        PRF(key, Q.data(), q_len, R);

        size_t y_len = (m_len > 16) ? m_len : 16;
        std::vector<uint8_t> Y(y_len, 0);
        std::memcpy(Y.data(), R, 16);

        if (y_len > 16) {
            size_t remaining = y_len - 16;
            size_t blocks = (remaining + 15) / 16;

            for (size_t i = 0; i < blocks && remaining > 0; ++i) {
                uint8_t counter[16];
                std::memset(counter, 0, 16);
                counter[15] = static_cast<uint8_t>(i + 1);

                uint8_t enc_input[16];
                for (size_t j = 0; j < 16; ++j) {
                    enc_input[j] = R[j] ^ counter[j];
                }

                EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
                uint8_t zero_iv[16];
                std::memset(zero_iv, 0, 16);
                EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), nullptr, key, zero_iv);
                int ol = 0;
                uint8_t enc_out[16];
                EVP_EncryptUpdate(ctx, enc_out, &ol, enc_input, 16);
                EVP_CIPHER_CTX_free(ctx);

                size_t copy_len = (remaining > 16) ? 16 : remaining;
                std::memcpy(Y.data() + 16 + i * 16, enc_out, copy_len);
                remaining -= copy_len;
            }
        }

        BNPtr y_bn = BytesToBN(Y.data() + (y_len - b_len), b_len);
        BNPtr y_mod(BN_new());
        BN_mod(y_mod.get(), y_bn.get(), radix_pow_v.get(), bn_ctx.get());

        BNPtr c(BN_new());
        if (encrypt) {
            BN_add(c.get(), A.get(), y_mod.get());
            BN_mod(c.get(), c.get(), radix_pow_u.get(), bn_ctx.get());

            BIGNUM* old_B = BN_dup(B.get());
            if (!old_B) return false;
            B.reset(c.release());
            A.reset(old_B);
        } else {
            BN_sub(c.get(), B.get(), y_mod.get());
            BIGNUM* tmp = c.get();
            while (BN_is_negative(tmp)) {
                BN_add(tmp, tmp, radix_pow_u.get());
            }
            BN_mod(c.get(), c.get(), radix_pow_u.get(), bn_ctx.get());

            BIGNUM* old_A = BN_dup(A.get());
            if (!old_A) return false;
            A.reset(c.release());
            B.reset(old_A);
        }
    }

    BNToBytes(A.get(), output, u);
    BNToBytes(B.get(), output + u, v);

    secure_bzero(R, sizeof(R));
    secure_bzero(Y.data(), Y.size());

    return true;
}

bool FPE::Encrypt(const uint8_t* key,
                  const uint8_t* tweak, size_t tweak_len,
                  const uint8_t* input, size_t input_len,
                  uint8_t* output) {
    return FF1(true, key, tweak, tweak_len, input, input_len, output);
}

bool FPE::Decrypt(const uint8_t* key,
                  const uint8_t* tweak, size_t tweak_len,
                  const uint8_t* input, size_t input_len,
                  uint8_t* output) {
    return FF1(false, key, tweak, tweak_len, input, input_len, output);
}

std::string FPE::EncryptFilename(const uint8_t* key,
                                 const std::string& plaintext,
                                 const std::string& parent_path) {
    if (plaintext.empty()) return plaintext;

    std::vector<uint8_t> tweak(parent_path.begin(), parent_path.end());
    std::vector<uint8_t> input(plaintext.begin(), plaintext.end());
    std::vector<uint8_t> output(plaintext.size());

    if (!Encrypt(key, tweak.data(), tweak.size(), input.data(), input.size(), output.data())) {
        throw std::runtime_error("FPE encryption failed");
    }

    secure_bzero(input.data(), input.size());

    return std::string(output.begin(), output.end());
}

std::string FPE::DecryptFilename(const uint8_t* key,
                                 const std::string& ciphertext,
                                 const std::string& parent_path) {
    if (ciphertext.empty()) return ciphertext;

    std::vector<uint8_t> tweak(parent_path.begin(), parent_path.end());
    std::vector<uint8_t> input(ciphertext.begin(), ciphertext.end());
    std::vector<uint8_t> output(ciphertext.size());

    if (!Decrypt(key, tweak.data(), tweak.size(), input.data(), input.size(), output.data())) {
        throw std::runtime_error("FPE decryption failed");
    }

    secure_bzero(input.data(), input.size());

    return std::string(output.begin(), output.end());
}

}
