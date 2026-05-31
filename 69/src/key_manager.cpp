#include "key_manager.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sys/stat.h>

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace fusefs {

KeyManager::KeyManager()
    : initialized_(false) {
    salt_.fill(0);
    master_key_.fill(0);
    data_key_.fill(0);
    fname_key_.fill(0);
}

KeyManager::~KeyManager() {
    ClearKeys();
}

void KeyManager::SecureClear(void* ptr, size_t len) {
    if (!ptr) return;
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0;
    }
}

void KeyManager::ClearKeys() {
    SecureClear(master_key_.data(), master_key_.size());
    SecureClear(data_key_.data(), data_key_.size());
    SecureClear(fname_key_.data(), fname_key_.size());
    SecureClear(salt_.data(), salt_.size());
    initialized_ = false;
}

bool KeyManager::IsInitialized() const {
    return initialized_;
}

const uint8_t* KeyManager::GetMasterKey() const {
    return initialized_ ? master_key_.data() : nullptr;
}

const uint8_t* KeyManager::GetDataKey() const {
    return initialized_ ? data_key_.data() : nullptr;
}

const uint8_t* KeyManager::GetFnameKey() const {
    return initialized_ ? fname_key_.data() : nullptr;
}

const uint8_t* KeyManager::GetSalt() const {
    return initialized_ ? salt_.data() : nullptr;
}

bool KeyManager::Init(const std::string& root_dir, const std::string& password) {
    std::string meta_dir = root_dir + "/" + META_DIR;
    mkdir(meta_dir.c_str(), 0700);

    std::array<uint8_t, SALT_LEN> salt;
    if (RAND_bytes(salt.data(), SALT_LEN) != 1) {
        return false;
    }

    std::string salt_path = meta_dir + "/" + SALT_FILE;
    std::ofstream salt_file(salt_path, std::ios::binary);
    if (!salt_file) {
        SecureClear(salt.data(), salt.size());
        return false;
    }
    salt_file.write(reinterpret_cast<const char*>(salt.data()), SALT_LEN);
    salt_file.close();

    std::array<uint8_t, KEY_LEN> master_key;
    master_key.fill(0);

    if (PKCS5_PBKDF2_HMAC(
            password.c_str(), static_cast<int>(password.size()),
            salt.data(), SALT_LEN,
            PBKDF2_ITERATIONS,
            EVP_sha256(),
            KEY_LEN, master_key.data()) != 1) {
        SecureClear(salt.data(), salt.size());
        return false;
    }

    std::string config_path = meta_dir + "/" + CONFIG_FILE;
    std::ofstream config_file(config_path, std::ios::binary);
    if (!config_file) {
        SecureClear(salt.data(), salt.size());
        SecureClear(master_key.data(), master_key.size());
        return false;
    }

    uint32_t iterations = PBKDF2_ITERATIONS;
    config_file.write(reinterpret_cast<const char*>(&iterations), sizeof(iterations));

    std::array<uint8_t, KEY_LEN> verify_key;
    verify_key.fill(0);
    std::array<uint8_t, SALT_LEN> verify_salt;
    verify_salt.fill(0xAB);
    if (PKCS5_PBKDF2_HMAC(
            password.c_str(), static_cast<int>(password.size()),
            verify_salt.data(), SALT_LEN,
            1,
            EVP_sha256(),
            KEY_LEN, verify_key.data()) != 1) {
        SecureClear(salt.data(), salt.size());
        SecureClear(master_key.data(), master_key.size());
        SecureClear(verify_key.data(), verify_key.size());
        return false;
    }
    config_file.write(reinterpret_cast<const char*>(verify_key.data()), KEY_LEN);
    config_file.close();

    SecureClear(verify_key.data(), verify_key.size());
    SecureClear(master_key.data(), master_key.size());
    SecureClear(salt.data(), salt.size());

    return true;
}

bool KeyManager::LoadSalt(const std::string& root_dir) {
    std::string salt_path = root_dir + "/" + META_DIR + "/" + SALT_FILE;
    std::ifstream salt_file(salt_path, std::ios::binary);
    if (!salt_file) return false;
    salt_file.read(reinterpret_cast<char*>(salt_.data()), SALT_LEN);
    if (salt_file.gcount() != SALT_LEN) return false;
    return true;
}

bool KeyManager::Load(const std::string& root_dir, const std::string& password) {
    ClearKeys();

    if (!LoadSalt(root_dir)) return false;

    std::array<uint8_t, KEY_LEN> master_key;
    master_key.fill(0);

    if (PKCS5_PBKDF2_HMAC(
            password.c_str(), static_cast<int>(password.size()),
            salt_.data(), SALT_LEN,
            PBKDF2_ITERATIONS,
            EVP_sha256(),
            KEY_LEN, master_key.data()) != 1) {
        SecureClear(master_key.data(), master_key.size());
        return false;
    }

    std::string config_path = root_dir + "/" + META_DIR + "/" + CONFIG_FILE;
    std::ifstream config_file(config_path, std::ios::binary);
    if (!config_file) {
        SecureClear(master_key.data(), master_key.size());
        return false;
    }

    uint32_t stored_iterations;
    config_file.read(reinterpret_cast<char*>(&stored_iterations), sizeof(stored_iterations));
    if (stored_iterations != PBKDF2_ITERATIONS) {
        SecureClear(master_key.data(), master_key.size());
        return false;
    }

    std::array<uint8_t, KEY_LEN> stored_verify;
    config_file.read(reinterpret_cast<char*>(stored_verify.data()), KEY_LEN);
    if (config_file.gcount() != KEY_LEN) {
        SecureClear(master_key.data(), master_key.size());
        return false;
    }

    std::array<uint8_t, KEY_LEN> verify_key;
    verify_key.fill(0);
    std::array<uint8_t, SALT_LEN> verify_salt;
    verify_salt.fill(0xAB);
    if (PKCS5_PBKDF2_HMAC(
            password.c_str(), static_cast<int>(password.size()),
            verify_salt.data(), SALT_LEN,
            1,
            EVP_sha256(),
            KEY_LEN, verify_key.data()) != 1) {
        SecureClear(master_key.data(), master_key.size());
        SecureClear(verify_key.data(), verify_key.size());
        return false;
    }

    if (CRYPTO_memcmp(stored_verify.data(), verify_key.data(), KEY_LEN) != 0) {
        SecureClear(master_key.data(), master_key.size());
        SecureClear(verify_key.data(), verify_key.size());
        return false;
    }

    SecureClear(verify_key.data(), verify_key.size());

    master_key_ = master_key;
    SecureClear(master_key.data(), master_key.size());

    DeriveSubKeys();
    initialized_ = true;
    return true;
}

void KeyManager::DeriveSubKeys() {
    std::array<uint8_t, SALT_LEN> salt_data;
    salt_data.fill(0);
    salt_data[0] = 0x01;
    if (PKCS5_PBKDF2_HMAC(
            reinterpret_cast<const char*>(master_key_.data()), KEY_LEN,
            salt_data.data(), SALT_LEN,
            1,
            EVP_sha256(),
            KEY_LEN, data_key_.data()) != 1) {
        throw std::runtime_error("Failed to derive data key");
    }

    salt_data[0] = 0x02;
    if (PKCS5_PBKDF2_HMAC(
            reinterpret_cast<const char*>(master_key_.data()), KEY_LEN,
            salt_data.data(), SALT_LEN,
            1,
            EVP_sha256(),
            KEY_LEN, fname_key_.data()) != 1) {
        throw std::runtime_error("Failed to derive filename key");
    }

    SecureClear(salt_data.data(), salt_data.size());
}

}
