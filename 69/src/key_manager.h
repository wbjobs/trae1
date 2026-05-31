#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "config.h"

namespace fusefs {

class KeyManager {
public:
    KeyManager();
    ~KeyManager();

    KeyManager(const KeyManager&) = delete;
    KeyManager& operator=(const KeyManager&) = delete;
    KeyManager(KeyManager&&) = delete;
    KeyManager& operator=(KeyManager&&) = delete;

    static bool Init(const std::string& root_dir, const std::string& password);

    static bool Load(const std::string& root_dir, const std::string& password);

    void DeriveSubKeys();

    void ClearKeys();

    bool IsInitialized() const;

    const uint8_t* GetMasterKey() const;
    const uint8_t* GetDataKey() const;
    const uint8_t* GetFnameKey() const;

    const uint8_t* GetSalt() const;

    bool LoadSalt(const std::string& root_dir);

    static void SecureClear(void* ptr, size_t len);

private:
    bool initialized_;
    std::array<uint8_t, SALT_LEN> salt_;
    std::array<uint8_t, KEY_LEN> master_key_;
    std::array<uint8_t, KEY_LEN> data_key_;
    std::array<uint8_t, KEY_LEN> fname_key_;
};

}
