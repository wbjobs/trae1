#include "common/crypto.h"
#include "common/utils.h"
#include <cstring>
#include <stdexcept>

namespace moshpp {

Crypto::Crypto()
    : key_(KEY_LENGTH, 0)
    , iv_(IV_LENGTH, 0)
    , hmac_key_(KEY_LENGTH, 0)
    , encrypt_ctx_(nullptr)
    , decrypt_ctx_(nullptr)
    , otp_counter_(0)
{
    encrypt_ctx_ = EVP_CIPHER_CTX_new();
    decrypt_ctx_ = EVP_CIPHER_CTX_new();
    if (!encrypt_ctx_ || !decrypt_ctx_) {
        throw std::runtime_error("Failed to create cipher contexts");
    }
}

Crypto::~Crypto() {
    if (encrypt_ctx_) {
        EVP_CIPHER_CTX_free(encrypt_ctx_);
    }
    if (decrypt_ctx_) {
        EVP_CIPHER_CTX_free(decrypt_ctx_);
    }
}

bool Crypto::init(const std::vector<uint8_t>& key, const std::vector<uint8_t>& iv) {
    if (key.size() != KEY_LENGTH || iv.size() != IV_LENGTH) {
        return false;
    }
    key_ = key;
    iv_ = iv;
    
    std::vector<uint8_t> otp_key = derive_otp_key(otp_counter_);
    std::vector<uint8_t> combined_key(KEY_LENGTH);
    for (size_t i = 0; i < KEY_LENGTH; ++i) {
        combined_key[i] = key[i] ^ otp_key[i];
    }

    if (EVP_EncryptInit_ex(encrypt_ctx_, EVP_aes_256_gcm(), nullptr, combined_key.data(), iv.data()) != 1) {
        return false;
    }
    if (EVP_DecryptInit_ex(decrypt_ctx_, EVP_aes_256_gcm(), nullptr, combined_key.data(), iv.data()) != 1) {
        return false;
    }

    for (size_t i = 0; i < KEY_LENGTH; ++i) {
        hmac_key_[i] = key[i] ^ (static_cast<uint8_t>(i) + 0x5A);
    }

    return true;
}

bool Crypto::generate_keys() {
    key_ = generate_random_bytes(KEY_LENGTH);
    iv_ = generate_random_bytes(IV_LENGTH);
    hmac_key_ = generate_random_bytes(KEY_LENGTH);
    return init(key_, iv_);
}

std::vector<uint8_t> Crypto::derive_otp_key(uint64_t counter) {
    std::vector<uint8_t> counter_bytes(sizeof(counter));
    std::memcpy(counter_bytes.data(), &counter, sizeof(counter));
    
    std::vector<uint8_t> otp_key(KEY_LENGTH);
    for (size_t i = 0; i < KEY_LENGTH; ++i) {
        uint8_t mix = 0;
        for (size_t j = 0; j < sizeof(counter); ++j) {
            mix ^= counter_bytes[j] + static_cast<uint8_t>(i * 31 + j * 17);
        }
        otp_key[i] = mix ^ hmac_key_[i % hmac_key_.size()];
    }
    return otp_key;
}

std::vector<uint8_t> Crypto::encrypt(const std::vector<uint8_t>& plaintext) {
    if (plaintext.empty()) {
        return {};
    }

    std::vector<uint8_t> new_iv = generate_random_bytes(IV_LENGTH);
    otp_counter_++;
    std::vector<uint8_t> otp_key = derive_otp_key(otp_counter_);
    
    std::vector<uint8_t> combined_key(KEY_LENGTH);
    for (size_t i = 0; i < KEY_LENGTH; ++i) {
        combined_key[i] = key_[i] ^ otp_key[i];
    }

    if (EVP_EncryptInit_ex(encrypt_ctx_, nullptr, nullptr, combined_key.data(), new_iv.data()) != 1) {
        throw std::runtime_error("Failed to initialize encryption");
    }

    std::vector<uint8_t> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
    int len;
    int ciphertext_len;

    if (EVP_EncryptUpdate(encrypt_ctx_, ciphertext.data(), &len, plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
        throw std::runtime_error("Failed to encrypt data");
    }
    ciphertext_len = len;

    if (EVP_EncryptFinal_ex(encrypt_ctx_, ciphertext.data() + len, &len) != 1) {
        throw std::runtime_error("Failed to finalize encryption");
    }
    ciphertext_len += len;

    ciphertext.resize(ciphertext_len);

    std::vector<uint8_t> auth_tag(AUTH_TAG_LENGTH);
    if (EVP_CIPHER_CTX_ctrl(encrypt_ctx_, EVP_CTRL_GCM_GET_TAG, AUTH_TAG_LENGTH, auth_tag.data()) != 1) {
        throw std::runtime_error("Failed to get authentication tag");
    }

    std::vector<uint8_t> result;
    result.insert(result.end(), new_iv.begin(), new_iv.end());
    result.insert(result.end(), auth_tag.begin(), auth_tag.end());
    result.insert(result.end(), ciphertext.begin(), ciphertext.end());

    return result;
}

std::vector<uint8_t> Crypto::decrypt(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& iv) {
    if (ciphertext.size() <= AUTH_TAG_LENGTH) {
        return {};
    }

    std::vector<uint8_t> auth_tag(ciphertext.begin(), ciphertext.begin() + AUTH_TAG_LENGTH);
    std::vector<uint8_t> encrypted_data(ciphertext.begin() + AUTH_TAG_LENGTH, ciphertext.end());

    otp_counter_++;
    std::vector<uint8_t> otp_key = derive_otp_key(otp_counter_);
    
    std::vector<uint8_t> combined_key(KEY_LENGTH);
    for (size_t i = 0; i < KEY_LENGTH; ++i) {
        combined_key[i] = key_[i] ^ otp_key[i];
    }

    if (EVP_DecryptInit_ex(decrypt_ctx_, nullptr, nullptr, combined_key.data(), iv.data()) != 1) {
        throw std::runtime_error("Failed to initialize decryption");
    }

    std::vector<uint8_t> plaintext(encrypted_data.size());
    int len;
    int plaintext_len;

    if (EVP_DecryptUpdate(decrypt_ctx_, plaintext.data(), &len, encrypted_data.data(), static_cast<int>(encrypted_data.size())) != 1) {
        throw std::runtime_error("Failed to decrypt data");
    }
    plaintext_len = len;

    if (EVP_CIPHER_CTX_ctrl(decrypt_ctx_, EVP_CTRL_GCM_SET_TAG, AUTH_TAG_LENGTH, auth_tag.data()) != 1) {
        throw std::runtime_error("Failed to set authentication tag");
    }

    if (EVP_DecryptFinal_ex(decrypt_ctx_, plaintext.data() + len, &len) != 1) {
        throw std::runtime_error("Authentication failed or data corrupted");
    }
    plaintext_len += len;

    plaintext.resize(plaintext_len);
    return plaintext;
}

std::vector<uint8_t> Crypto::hmac(const std::vector<uint8_t>& data) {
    unsigned int len = EVP_MAX_MD_SIZE;
    std::vector<uint8_t> result(len);

    HMAC_CTX* ctx = HMAC_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create HMAC context");
    }

    if (HMAC_Init_ex(ctx, hmac_key_.data(), static_cast<int>(hmac_key_.size()), EVP_sha256(), nullptr) != 1) {
        HMAC_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize HMAC");
    }

    if (HMAC_Update(ctx, data.data(), data.size()) != 1) {
        HMAC_CTX_free(ctx);
        throw std::runtime_error("Failed to update HMAC");
    }

    if (HMAC_Final(ctx, result.data(), &len) != 1) {
        HMAC_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize HMAC");
    }

    HMAC_CTX_free(ctx);
    result.resize(len);
    return result;
}

bool Crypto::verify_hmac(const std::vector<uint8_t>& data, const std::vector<uint8_t>& expected_hmac) {
    std::vector<uint8_t> computed = hmac(data);
    if (computed.size() != expected_hmac.size()) {
        return false;
    }
    return std::memcmp(computed.data(), expected_hmac.data(), computed.size()) == 0;
}

void Crypto::rotate_iv() {
    iv_ = generate_random_bytes(IV_LENGTH);
}

void Crypto::rekey() {
    key_ = generate_random_bytes(KEY_LENGTH);
    hmac_key_ = generate_random_bytes(KEY_LENGTH);
    otp_counter_ = 0;
    init(key_, iv_);
}

}
