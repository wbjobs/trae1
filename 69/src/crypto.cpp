#include "crypto.h"
#include "config.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include <cstring>
#include <stdexcept>

namespace fusefs {

void Crypto::SecureClear(void* ptr, size_t len) {
    if (!ptr) return;
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0;
    }
}

bool Crypto::GenerateIV(uint8_t* iv, size_t iv_len) {
    return RAND_bytes(iv, static_cast<int>(iv_len)) == 1;
}

bool Crypto::EncryptFile(const uint8_t* key,
                         const uint8_t* plaintext, size_t plaintext_len,
                         uint8_t* ciphertext, size_t& ciphertext_len) {
    if (!key || !ciphertext) return false;
    if (plaintext_len > 0 && !plaintext) return false;

    size_t required = GCM_IV_LEN + GCM_TAG_LEN + plaintext_len;
    if (ciphertext_len < required) {
        ciphertext_len = required;
        return false;
    }

    if (!GenerateIV(ciphertext, GCM_IV_LEN)) {
        return false;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, GCM_IV_LEN, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, ciphertext) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    uint8_t* ct_ptr = ciphertext + GCM_IV_LEN;
    int out_len = 0;
    if (plaintext_len > 0) {
        if (EVP_EncryptUpdate(ctx, ct_ptr, &out_len, plaintext, static_cast<int>(plaintext_len)) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
    }

    int final_len = 0;
    if (EVP_EncryptFinal_ex(ctx, ct_ptr + out_len, &final_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    uint8_t* tag_ptr = ciphertext + GCM_IV_LEN + plaintext_len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_LEN, tag_ptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    EVP_CIPHER_CTX_free(ctx);
    ciphertext_len = GCM_IV_LEN + GCM_TAG_LEN + plaintext_len;
    return true;
}

bool Crypto::DecryptFile(const uint8_t* key,
                         const uint8_t* ciphertext, size_t ciphertext_len,
                         uint8_t* plaintext, size_t& plaintext_len) {
    if (!key || !ciphertext || !plaintext) return false;

    if (ciphertext_len < GCM_IV_LEN + GCM_TAG_LEN) {
        return false;
    }

    size_t ct_data_len = ciphertext_len - GCM_IV_LEN - GCM_TAG_LEN;
    if (plaintext_len < ct_data_len) {
        plaintext_len = ct_data_len;
        return false;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, GCM_IV_LEN, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, ciphertext) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    const uint8_t* ct_data = ciphertext + GCM_IV_LEN;
    const uint8_t* tag = ciphertext + GCM_IV_LEN + ct_data_len;

    int out_len = 0;
    if (EVP_DecryptUpdate(ctx, plaintext, &out_len, ct_data, static_cast<int>(ct_data_len)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, GCM_TAG_LEN,
                            const_cast<uint8_t*>(tag)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    int final_len = 0;
    int ret = EVP_DecryptFinal_ex(ctx, plaintext + out_len, &final_len);
    EVP_CIPHER_CTX_free(ctx);

    if (ret != 1) {
        return false;
    }

    plaintext_len = static_cast<size_t>(out_len + final_len);
    return true;
}

bool Crypto::GenerateRandomKey(uint8_t* key, size_t key_len) {
    return RAND_bytes(key, static_cast<int>(key_len)) == 1;
}

bool Crypto::RSAEncryptKey(EVP_PKEY* public_key,
                           const uint8_t* file_key, size_t file_key_len,
                           std::vector<uint8_t>& out_encrypted) {
    if (!public_key || !file_key || file_key_len == 0) return false;

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(public_key, nullptr);
    if (!ctx) return false;

    if (EVP_PKEY_encrypt_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    size_t out_len = 0;
    if (EVP_PKEY_encrypt(ctx, nullptr, &out_len, file_key, file_key_len) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    out_encrypted.resize(out_len);
    if (EVP_PKEY_encrypt(ctx, out_encrypted.data(), &out_len, file_key, file_key_len) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    out_encrypted.resize(out_len);
    EVP_PKEY_CTX_free(ctx);
    return true;
}

bool Crypto::RSADecryptKey(EVP_PKEY* private_key,
                           const uint8_t* encrypted, size_t encrypted_len,
                           std::vector<uint8_t>& out_key) {
    if (!private_key || !encrypted || encrypted_len == 0) return false;

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(private_key, nullptr);
    if (!ctx) return false;

    if (EVP_PKEY_decrypt_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    size_t out_len = 0;
    if (EVP_PKEY_decrypt(ctx, nullptr, &out_len, encrypted, encrypted_len) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    out_key.resize(out_len);
    if (EVP_PKEY_decrypt(ctx, out_key.data(), &out_len, encrypted, encrypted_len) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    out_key.resize(out_len);
    EVP_PKEY_CTX_free(ctx);
    return true;
}

}
