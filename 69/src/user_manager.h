#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/rsa.h>

namespace fusefs {

struct UserInfo {
    std::string username;
    std::vector<uint8_t> encrypted_private_key;
    std::vector<uint8_t> public_key_der;
    std::vector<uint8_t> salt;
    uint32_t pbkdf2_iterations;
};

class UserManager {
public:
    UserManager();
    ~UserManager();

    UserManager(const UserManager&) = delete;
    UserManager& operator=(const UserManager&) = delete;

    static bool Init(const std::string& root_dir, const std::string& owner_password);

    bool Load(const std::string& root_dir);

    bool AddUser(const std::string& root_dir,
                 const std::string& username, const std::string& password);

    bool RemoveUser(const std::string& username);

    bool HasUser(const std::string& username) const;

    std::vector<std::string> ListUsers() const;

    bool Authenticate(const std::string& username, const std::string& password);

    EVP_PKEY* GetUserPublicKey(const std::string& username) const;

    EVP_PKEY* GetUserPrivateKey(const std::string& username) const;

    const std::string& GetCurrentUser() const { return current_user_; }
    void SetCurrentUser(const std::string& user) { current_user_ = user; }

    EVP_PKEY* GetCurrentPrivateKey() const;
    EVP_PKEY* GetCurrentPublicKey() const;

    bool SaveRegistry(const std::string& root_dir);

    void ClearAll();

    static void SecureClear(void* ptr, size_t len);

private:
    bool LoadRegistry(const std::string& root_dir);
    bool SaveUserKey(const std::string& root_dir, const UserInfo& info);
    bool LoadUserKey(const std::string& root_dir, const std::string& username, UserInfo& info);

    std::map<std::string, UserInfo> users_;
    std::string current_user_;
    std::map<std::string, EVP_PKEY*> decrypted_private_keys_;
};

}
