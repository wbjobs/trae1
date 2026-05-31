#include "user_manager.h"
#include "config.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include <sys/stat.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fusefs {

static EVP_PKEY* GenerateRSAKey(int bits) {
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx) return nullptr;

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return nullptr;
    }

    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return nullptr;
    }

    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return nullptr;
    }

    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

static std::vector<uint8_t> PKEYToPublicDER(EVP_PKEY* pkey) {
    unsigned char* der = nullptr;
    int len = i2d_PUBKEY(pkey, &der);
    if (len <= 0 || !der) return {};

    std::vector<uint8_t> result(der, der + len);
    OPENSSL_free(der);
    return result;
}

static EVP_PKEY* DERToPublicPKEY(const uint8_t* der, size_t len) {
    const unsigned char* p = der;
    return d2i_PUBKEY(nullptr, &p, static_cast<long>(len));
}

static std::vector<uint8_t> PKEYToPrivatePEM(EVP_PKEY* pkey) {
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return {};

    if (PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        BIO_free(bio);
        return {};
    }

    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(bio, &bptr);

    std::vector<uint8_t> result(bptr->data, bptr->data + bptr->length);
    BIO_free(bio);
    return result;
}

static EVP_PKEY* PEMToPrivatePKEY(const uint8_t* pem, size_t len) {
    BIO* bio = BIO_new_mem_buf(pem, static_cast<int>(len));
    if (!bio) return nullptr;

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    return pkey;
}

static bool AES256GCMEncrypt(const uint8_t* key,
                              const uint8_t* plaintext, size_t plaintext_len,
                              std::vector<uint8_t>& out) {
    out.resize(GCM_IV_LEN + GCM_TAG_LEN + plaintext_len);

    if (RAND_bytes(out.data(), GCM_IV_LEN) != 1) return false;

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
    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, out.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    int outl = 0;
    if (plaintext_len > 0) {
        if (EVP_EncryptUpdate(ctx, out.data() + GCM_IV_LEN, &outl,
                              plaintext, static_cast<int>(plaintext_len)) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
    }

    int finl = 0;
    if (EVP_EncryptFinal_ex(ctx, out.data() + GCM_IV_LEN + outl, &finl) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_LEN,
                            out.data() + GCM_IV_LEN + plaintext_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    EVP_CIPHER_CTX_free(ctx);
    return true;
}

static bool AES256GCMDecrypt(const uint8_t* key,
                              const uint8_t* ciphertext, size_t ciphertext_len,
                              std::vector<uint8_t>& out) {
    if (ciphertext_len < GCM_IV_LEN + GCM_TAG_LEN) return false;

    size_t pt_len = ciphertext_len - GCM_IV_LEN - GCM_TAG_LEN;
    out.resize(pt_len);

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

    int outl = 0;
    if (pt_len > 0) {
        if (EVP_DecryptUpdate(ctx, out.data(), &outl,
                              ciphertext + GCM_IV_LEN, static_cast<int>(pt_len)) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, GCM_TAG_LEN,
                            const_cast<uint8_t*>(ciphertext + GCM_IV_LEN + pt_len)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    int finl = 0;
    int ret = EVP_DecryptFinal_ex(ctx, out.data() + outl, &finl);
    EVP_CIPHER_CTX_free(ctx);

    if (ret != 1) return false;
    out.resize(static_cast<size_t>(outl + finl));
    return true;
}

void UserManager::SecureClear(void* ptr, size_t len) {
    if (!ptr) return;
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    for (size_t i = 0; i < len; ++i) p[i] = 0;
}

UserManager::UserManager() = default;

UserManager::~UserManager() {
    ClearAll();
}

void UserManager::ClearAll() {
    for (auto& [name, pkey] : decrypted_private_keys_) {
        if (pkey) EVP_PKEY_free(pkey);
    }
    decrypted_private_keys_.clear();

    for (auto& [name, info] : users_) {
        SecureClear(info.encrypted_private_key.data(), info.encrypted_private_key.size());
        SecureClear(info.salt.data(), info.salt.size());
    }
    users_.clear();
    current_user_.clear();
}

bool UserManager::Init(const std::string& root_dir, const std::string& owner_password) {
    std::string users_dir = root_dir + "/" + META_DIR + "/" + USERS_DIR;
    mkdir(users_dir.c_str(), 0700);

    std::array<uint8_t, KEY_LEN> derived_key;
    derived_key.fill(0);

    std::array<uint8_t, SALT_LEN> user_salt;
    user_salt.fill(0);
    if (RAND_bytes(user_salt.data(), SALT_LEN) != 1) return false;

    if (PKCS5_PBKDF2_HMAC(
            owner_password.c_str(), static_cast<int>(owner_password.size()),
            user_salt.data(), SALT_LEN,
            PBKDF2_ITERATIONS,
            EVP_sha256(),
            KEY_LEN, derived_key.data()) != 1) {
        SecureClear(derived_key.data(), derived_key.size());
        SecureClear(user_salt.data(), user_salt.size());
        return false;
    }

    EVP_PKEY* rsa_key = GenerateRSAKey(static_cast<int>(RSA_KEY_BITS));
    if (!rsa_key) {
        SecureClear(derived_key.data(), derived_key.size());
        SecureClear(user_salt.data(), user_salt.size());
        return false;
    }

    std::vector<uint8_t> private_pem = PKEYToPrivatePEM(rsa_key);
    if (private_pem.empty()) {
        EVP_PKEY_free(rsa_key);
        SecureClear(derived_key.data(), derived_key.size());
        SecureClear(user_salt.data(), user_salt.size());
        return false;
    }

    std::vector<uint8_t> encrypted_key;
    if (!AES256GCMEncrypt(derived_key.data(), private_pem.data(), private_pem.size(),
                           encrypted_key)) {
        EVP_PKEY_free(rsa_key);
        SecureClear(derived_key.data(), derived_key.size());
        SecureClear(user_salt.data(), user_salt.size());
        SecureClear(private_pem.data(), private_pem.size());
        return false;
    }

    std::vector<uint8_t> public_der = PKEYToPublicDER(rsa_key);

    UserInfo info;
    info.username = OWNER_USER;
    info.encrypted_private_key = encrypted_key;
    info.public_key_der = public_der;
    info.salt.assign(user_salt.begin(), user_salt.end());
    info.pbkdf2_iterations = PBKDF2_ITERATIONS;

    users_[OWNER_USER] = info;

    bool saved = SaveRegistry(root_dir) && SaveUserKey(root_dir, info);

    EVP_PKEY_free(rsa_key);
    SecureClear(derived_key.data(), derived_key.size());
    SecureClear(user_salt.data(), user_salt.size());
    SecureClear(private_pem.data(), private_pem.size());

    return saved;
}

bool UserManager::Load(const std::string& root_dir) {
    ClearAll();
    return LoadRegistry(root_dir);
}

bool UserManager::SaveRegistry(const std::string& root_dir) {
    std::string reg_path = root_dir + "/" + META_DIR + "/" + USERS_DIR + "/" + USER_REGISTRY;
    std::ofstream f(reg_path, std::ios::binary);
    if (!f) return false;

    uint32_t count = static_cast<uint32_t>(users_.size());
    f.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& [name, info] : users_) {
        uint32_t name_len = static_cast<uint32_t>(name.size());
        f.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
        f.write(name.data(), name_len);
    }
    f.close();
    return true;
}

bool UserManager::LoadRegistry(const std::string& root_dir) {
    std::string reg_path = root_dir + "/" + META_DIR + "/" + USERS_DIR + "/" + USER_REGISTRY;
    std::ifstream f(reg_path, std::ios::binary);
    if (!f) return false;

    uint32_t count = 0;
    f.read(reinterpret_cast<char*>(&count), sizeof(count));

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t name_len = 0;
        f.read(reinterpret_cast<char*>(&name_len), sizeof(name_len));
        std::string name(name_len, '\0');
        f.read(&name[0], name_len);

        UserInfo info;
        if (!LoadUserKey(root_dir, name, info)) return false;
        users_[name] = info;
    }

    f.close();
    return true;
}

bool UserManager::SaveUserKey(const std::string& root_dir, const UserInfo& info) {
    std::string key_path = root_dir + "/" + META_DIR + "/" + USERS_DIR + "/" + info.username;
    std::ofstream f(key_path, std::ios::binary);
    if (!f) return false;

    uint32_t salt_len = static_cast<uint32_t>(info.salt.size());
    f.write(reinterpret_cast<const char*>(&salt_len), sizeof(salt_len));
    f.write(reinterpret_cast<const char*>(info.salt.data()), salt_len);

    uint32_t iter = info.pbkdf2_iterations;
    f.write(reinterpret_cast<const char*>(&iter), sizeof(iter));

    uint32_t pub_len = static_cast<uint32_t>(info.public_key_der.size());
    f.write(reinterpret_cast<const char*>(&pub_len), sizeof(pub_len));
    f.write(reinterpret_cast<const char*>(info.public_key_der.data()), pub_len);

    uint32_t priv_len = static_cast<uint32_t>(info.encrypted_private_key.size());
    f.write(reinterpret_cast<const char*>(&priv_len), sizeof(priv_len));
    f.write(reinterpret_cast<const char*>(info.encrypted_private_key.data()), priv_len);

    f.close();
    return true;
}

bool UserManager::LoadUserKey(const std::string& root_dir, const std::string& username,
                               UserInfo& info) {
    std::string key_path = root_dir + "/" + META_DIR + "/" + USERS_DIR + "/" + username;
    std::ifstream f(key_path, std::ios::binary);
    if (!f) return false;

    info.username = username;

    uint32_t salt_len = 0;
    f.read(reinterpret_cast<char*>(&salt_len), sizeof(salt_len));
    info.salt.resize(salt_len);
    f.read(reinterpret_cast<char*>(info.salt.data()), salt_len);

    f.read(reinterpret_cast<char*>(&info.pbkdf2_iterations), sizeof(info.pbkdf2_iterations));

    uint32_t pub_len = 0;
    f.read(reinterpret_cast<char*>(&pub_len), sizeof(pub_len));
    info.public_key_der.resize(pub_len);
    f.read(reinterpret_cast<char*>(info.public_key_der.data()), pub_len);

    uint32_t priv_len = 0;
    f.read(reinterpret_cast<char*>(&priv_len), sizeof(priv_len));
    info.encrypted_private_key.resize(priv_len);
    f.read(reinterpret_cast<char*>(info.encrypted_private_key.data()), priv_len);

    f.close();
    return true;
}

bool UserManager::AddUser(const std::string& root_dir,
                          const std::string& username, const std::string& password) {
    if (HasUser(username)) return false;

    std::array<uint8_t, KEY_LEN> derived_key;
    derived_key.fill(0);

    std::array<uint8_t, SALT_LEN> user_salt;
    user_salt.fill(0);
    if (RAND_bytes(user_salt.data(), SALT_LEN) != 1) return false;

    if (PKCS5_PBKDF2_HMAC(
            password.c_str(), static_cast<int>(password.size()),
            user_salt.data(), SALT_LEN,
            PBKDF2_ITERATIONS,
            EVP_sha256(),
            KEY_LEN, derived_key.data()) != 1) {
        SecureClear(derived_key.data(), derived_key.size());
        SecureClear(user_salt.data(), user_salt.size());
        return false;
    }

    EVP_PKEY* rsa_key = GenerateRSAKey(static_cast<int>(RSA_KEY_BITS));
    if (!rsa_key) {
        SecureClear(derived_key.data(), derived_key.size());
        SecureClear(user_salt.data(), user_salt.size());
        return false;
    }

    std::vector<uint8_t> private_pem = PKEYToPrivatePEM(rsa_key);
    if (private_pem.empty()) {
        EVP_PKEY_free(rsa_key);
        SecureClear(derived_key.data(), derived_key.size());
        SecureClear(user_salt.data(), user_salt.size());
        return false;
    }

    std::vector<uint8_t> encrypted_key;
    if (!AES256GCMEncrypt(derived_key.data(), private_pem.data(), private_pem.size(),
                           encrypted_key)) {
        EVP_PKEY_free(rsa_key);
        SecureClear(derived_key.data(), derived_key.size());
        SecureClear(user_salt.data(), user_salt.size());
        SecureClear(private_pem.data(), private_pem.size());
        return false;
    }

    std::vector<uint8_t> public_der = PKEYToPublicDER(rsa_key);

    UserInfo info;
    info.username = username;
    info.encrypted_private_key = encrypted_key;
    info.public_key_der = public_der;
    info.salt.assign(user_salt.begin(), user_salt.end());
    info.pbkdf2_iterations = PBKDF2_ITERATIONS;

    users_[username] = info;

    bool saved = SaveUserKey(root_dir, info);

    EVP_PKEY_free(rsa_key);
    SecureClear(derived_key.data(), derived_key.size());
    SecureClear(user_salt.data(), user_salt.size());
    SecureClear(private_pem.data(), private_pem.size());

    return saved;
}

bool UserManager::RemoveUser(const std::string& username) {
    auto it = users_.find(username);
    if (it == users_.end()) return false;

    auto pk_it = decrypted_private_keys_.find(username);
    if (pk_it != decrypted_private_keys_.end()) {
        EVP_PKEY_free(pk_it->second);
        decrypted_private_keys_.erase(pk_it);
    }

    SecureClear(it->second.encrypted_private_key.data(),
                 it->second.encrypted_private_key.size());
    SecureClear(it->second.salt.data(), it->second.salt.size());

    users_.erase(it);
    return true;
}

bool UserManager::HasUser(const std::string& username) const {
    return users_.find(username) != users_.end();
}

std::vector<std::string> UserManager::ListUsers() const {
    std::vector<std::string> result;
    for (const auto& [name, info] : users_) {
        result.push_back(name);
    }
    return result;
}

bool UserManager::Authenticate(const std::string& username, const std::string& password) {
    auto it = users_.find(username);
    if (it == users_.end()) return false;

    std::array<uint8_t, KEY_LEN> derived_key;
    derived_key.fill(0);

    if (PKCS5_PBKDF2_HMAC(
            password.c_str(), static_cast<int>(password.size()),
            it->second.salt.data(), it->second.salt.size(),
            it->second.pbkdf2_iterations,
            EVP_sha256(),
            KEY_LEN, derived_key.data()) != 1) {
        SecureClear(derived_key.data(), derived_key.size());
        return false;
    }

    std::vector<uint8_t> private_pem;
    if (!AES256GCMDecrypt(derived_key.data(),
                           it->second.encrypted_private_key.data(),
                           it->second.encrypted_private_key.size(),
                           private_pem)) {
        SecureClear(derived_key.data(), derived_key.size());
        return false;
    }

    EVP_PKEY* pkey = PEMToPrivatePKEY(private_pem.data(), private_pem.size());
    SecureClear(derived_key.data(), derived_key.size());
    SecureClear(private_pem.data(), private_pem.size());

    if (!pkey) return false;

    auto pk_it = decrypted_private_keys_.find(username);
    if (pk_it != decrypted_private_keys_.end()) {
        EVP_PKEY_free(pk_it->second);
    }
    decrypted_private_keys_[username] = pkey;

    current_user_ = username;
    return true;
}

EVP_PKEY* UserManager::GetUserPublicKey(const std::string& username) const {
    auto it = users_.find(username);
    if (it == users_.end()) return nullptr;
    return DERToPublicPKEY(it->second.public_key_der.data(), it->second.public_key_der.size());
}

EVP_PKEY* UserManager::GetUserPrivateKey(const std::string& username) const {
    auto it = decrypted_private_keys_.find(username);
    if (it != decrypted_private_keys_.end()) return it->second;
    return nullptr;
}

EVP_PKEY* UserManager::GetCurrentPrivateKey() const {
    return GetUserPrivateKey(current_user_);
}

EVP_PKEY* UserManager::GetCurrentPublicKey() const {
    return GetUserPublicKey(current_user_);
}

}
