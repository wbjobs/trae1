#include "filesystem.h"
#include "crypto.h"
#include "fpe.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <cstring>
#include <functional>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace fusefs {

static constexpr const char* TMP_SUFFIX = ".tmp";

FileSystem::FileSystem(const std::string& root_dir, KeyManager& key_manager,
                       UserManager& user_manager)
    : root_dir_(root_dir), key_manager_(key_manager), user_manager_(user_manager) {
}

FileSystem::~FileSystem() = default;

std::vector<std::string> FileSystem::SplitPath(const std::string& path) const {
    std::vector<std::string> components;
    std::stringstream ss(path);
    std::string item;
    while (std::getline(ss, item, '/')) {
        if (!item.empty()) {
            components.push_back(item);
        }
    }
    return components;
}

std::string FileSystem::JoinPath(const std::vector<std::string>& components) const {
    std::string result = "/";
    for (size_t i = 0; i < components.size(); ++i) {
        if (i > 0) result += "/";
        result += components[i];
    }
    return result;
}

bool FileSystem::IsMetaPath(const std::string& fuse_path) const {
    return fuse_path.find(std::string("/") + META_DIR) == 0 ||
           fuse_path == std::string("/") + META_DIR;
}

std::string FileSystem::EncryptComponent(const std::string& component,
                                         const std::string& parent_path) const {
    const uint8_t* fname_key = key_manager_.GetFnameKey();
    if (!fname_key) {
        throw std::runtime_error("Key manager not initialized");
    }
    return FPE::EncryptFilename(fname_key, component, parent_path);
}

std::string FileSystem::DecryptComponent(const std::string& encrypted,
                                         const std::string& parent_path) const {
    const uint8_t* fname_key = key_manager_.GetFnameKey();
    if (!fname_key) {
        throw std::runtime_error("Key manager not initialized");
    }
    return FPE::DecryptFilename(fname_key, encrypted, parent_path);
}

std::string FileSystem::MapPath(const std::string& fuse_path) const {
    if (fuse_path == "/") return root_dir_;

    std::string path = fuse_path;
    if (!path.empty() && path.front() == '/') {
        path = path.substr(1);
    }

    std::vector<std::string> components = SplitPath("/" + path);
    std::string result = root_dir_;
    std::string parent_enc = "";

    for (size_t i = 0; i < components.size(); ++i) {
        std::string enc = EncryptComponent(components[i], parent_enc);
        result += "/" + enc;
        parent_enc = enc;
    }

    return result;
}

std::string FileSystem::EncryptPath(const std::string& fuse_path) const {
    return MapPath(fuse_path);
}

std::string FileSystem::DecryptPath(const std::string& real_path) const {
    if (real_path == root_dir_ || real_path == root_dir_ + "/") return "/";

    std::string relative = real_path;
    if (relative.find(root_dir_ + "/") == 0) {
        relative = relative.substr(root_dir_.size() + 1);
    }

    std::vector<std::string> components = SplitPath("/" + relative);
    std::string result = "/";
    std::string parent_enc = "";

    for (size_t i = 0; i < components.size(); ++i) {
        if (i > 0) result += "/";
        std::string dec = DecryptComponent(components[i], parent_enc);
        result += dec;
        parent_enc = components[i];
    }

    return result;
}

bool FileSystem::GetAttr(const std::string& fuse_path, struct stat& stbuf) const {
    if (IsMetaPath(fuse_path)) return false;

    std::string real_path = MapPath(fuse_path);

    if (::lstat(real_path.c_str(), &stbuf) != 0) {
        return false;
    }

    if (S_ISREG(stbuf.st_mode)) {
        if (stbuf.st_size >= static_cast<off_t>(FILE_HEADER_LEN)) {
            stbuf.st_size -= FILE_HEADER_LEN;
        } else {
            stbuf.st_size = 0;
        }
    }

    return true;
}

bool FileSystem::ReadDir(const std::string& fuse_path,
                         std::vector<std::string>& entries) const {
    if (IsMetaPath(fuse_path)) return false;

    std::string real_path = MapPath(fuse_path);

    DIR* dir = ::opendir(real_path.c_str());
    if (!dir) return false;

    std::string parent_enc = "";
    if (fuse_path != "/") {
        std::vector<std::string> comps = SplitPath(fuse_path);
        if (!comps.empty()) {
            parent_enc = EncryptComponent(comps.back(),
                fuse_path.substr(0, fuse_path.size() - comps.back().size()));
        }
    }

    struct dirent* entry;
    while ((entry = ::readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name == "." || name == "..") {
            entries.push_back(name);
            continue;
        }

        if (name == META_DIR) continue;

        if (name.size() > 4 && name.substr(name.size() - 4) == TMP_SUFFIX) {
            continue;
        }

        try {
            std::string decrypted = DecryptComponent(name, parent_enc);
            entries.push_back(decrypted);
        } catch (...) {
            entries.push_back(name);
        }
    }

    ::closedir(dir);
    return true;
}

bool FileSystem::IsFileShared(const std::string& fuse_path) const {
    if (IsMetaPath(fuse_path)) return false;
    std::string real_path = MapPath(fuse_path);

    std::vector<uint8_t> val;
    return GetXattr(real_path, XATTR_SHARED_FLAG, val);
}

bool FileSystem::GetXattr(const std::string& real_path, const std::string& name,
                           std::vector<uint8_t>& value) const {
    ssize_t len = ::getxattr(real_path.c_str(), name.c_str(), nullptr, 0);
    if (len <= 0) return false;

    value.resize(static_cast<size_t>(len));
    ssize_t r = ::getxattr(real_path.c_str(), name.c_str(), value.data(), value.size());
    if (r <= 0) {
        value.clear();
        return false;
    }
    value.resize(static_cast<size_t>(r));
    return true;
}

bool FileSystem::SetXattr(const std::string& real_path, const std::string& name,
                           const std::vector<uint8_t>& value) {
    int ret = ::setxattr(real_path.c_str(), name.c_str(), value.data(), value.size(), 0);
    return ret == 0;
}

bool FileSystem::RemoveXattr(const std::string& real_path, const std::string& name) {
    return ::removexattr(real_path.c_str(), name.c_str()) == 0;
}

std::vector<std::string> FileSystem::ListXattrs(const std::string& real_path) const {
    std::vector<std::string> result;
    ssize_t len = ::listxattr(real_path.c_str(), nullptr, 0);
    if (len <= 0) return result;

    std::vector<char> buf(static_cast<size_t>(len));
    ssize_t r = ::listxattr(real_path.c_str(), buf.data(), buf.size());
    if (r <= 0) return result;

    size_t pos = 0;
    while (pos < static_cast<size_t>(r)) {
        std::string name(buf.data() + pos);
        if (!name.empty()) result.push_back(name);
        pos += name.size() + 1;
    }

    return result;
}

bool FileSystem::SetSharedFlag(const std::string& real_path) {
    std::vector<uint8_t> flag = {'1'};
    return SetXattr(real_path, XATTR_SHARED_FLAG, flag);
}

bool FileSystem::ClearSharedFlag(const std::string& real_path) {
    return RemoveXattr(real_path, XATTR_SHARED_FLAG);
}

bool FileSystem::ReadFileKey(const std::string& real_path,
                              std::vector<uint8_t>& file_key) const {
    const std::string& current_user = user_manager_.GetCurrentUser();
    EVP_PKEY* private_key = user_manager_.GetCurrentPrivateKey();
    if (!private_key) return false;

    std::string xattr_name = std::string(XATTR_KEY_PREFIX) + current_user;
    std::vector<uint8_t> encrypted_key;
    if (!GetXattr(real_path, xattr_name, encrypted_key)) return false;

    return Crypto::RSADecryptKey(private_key, encrypted_key.data(), encrypted_key.size(),
                                  file_key);
}

bool FileSystem::WriteFileKeys(const std::string& real_path,
                                const std::vector<uint8_t>& file_key,
                                const std::vector<std::string>& usernames) {
    for (const auto& username : usernames) {
        EVP_PKEY* pub_key = user_manager_.GetUserPublicKey(username);
        if (!pub_key) continue;

        std::vector<uint8_t> encrypted_key;
        if (!Crypto::RSAEncryptKey(pub_key, file_key.data(), file_key.size(),
                                    encrypted_key)) {
            EVP_PKEY_free(pub_key);
            return false;
        }

        std::string xattr_name = std::string(XATTR_KEY_PREFIX) + username;
        if (!SetXattr(real_path, xattr_name, encrypted_key)) {
            EVP_PKEY_free(pub_key);
            return false;
        }
        EVP_PKEY_free(pub_key);
    }
    return true;
}

bool FileSystem::RemoveFileKey(const std::string& real_path, const std::string& username) {
    std::string xattr_name = std::string(XATTR_KEY_PREFIX) + username;
    return RemoveXattr(real_path, xattr_name);
}

bool FileSystem::ReadFile(const std::string& fuse_path,
                          std::vector<uint8_t>& out_data,
                          size_t offset, size_t size) const {
    if (IsMetaPath(fuse_path)) return false;

    std::string real_path = MapPath(fuse_path);

    std::ifstream file(real_path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::string tmp_path = GetTempPath(real_path);
        file.open(tmp_path, std::ios::binary | std::ios::ate);
        if (!file) return false;
    }

    auto file_size = file.tellg();
    if (file_size <= 0) {
        out_data.clear();
        return true;
    }

    size_t encrypted_size = static_cast<size_t>(file_size);
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> encrypted(encrypted_size);
    file.read(reinterpret_cast<char*>(encrypted.data()), encrypted_size);
    file.close();

    if (encrypted_size < FILE_HEADER_LEN) {
        return false;
    }

    size_t plaintext_capacity = encrypted_size - FILE_HEADER_LEN;
    std::vector<uint8_t> plaintext(plaintext_capacity);
    size_t plaintext_len = plaintext_capacity;

    bool is_shared = IsFileShared(fuse_path);
    bool decrypt_success = false;

    if (is_shared) {
        std::vector<uint8_t> file_key;
        if (ReadFileKey(real_path, file_key)) {
            decrypt_success = Crypto::DecryptFile(file_key.data(), encrypted.data(),
                                                   encrypted_size, plaintext.data(), plaintext_len);
            Crypto::SecureClear(file_key.data(), file_key.size());
        }
    } else {
        const uint8_t* data_key = key_manager_.GetDataKey();
        if (data_key) {
            decrypt_success = Crypto::DecryptFile(data_key, encrypted.data(),
                                                   encrypted_size, plaintext.data(), plaintext_len);
        }
    }

    Crypto::SecureClear(encrypted.data(), encrypted.size());

    if (!decrypt_success) {
        Crypto::SecureClear(plaintext.data(), plaintext.size());
        return false;
    }

    if (offset >= plaintext_len) {
        out_data.clear();
    } else {
        size_t available = plaintext_len - offset;
        size_t to_read = (size < available) ? size : available;
        out_data.assign(plaintext.begin() + offset, plaintext.begin() + offset + to_read);
    }

    Crypto::SecureClear(plaintext.data(), plaintext.size());
    return true;
}

std::string FileSystem::GetTempPath(const std::string& real_path) const {
    return real_path + TMP_SUFFIX;
}

bool FileSystem::AtomicWrite(const std::string& real_path,
                             const std::vector<uint8_t>& ciphertext) {
    std::string tmp_path = GetTempPath(real_path);

    std::string dir_path = real_path;
    size_t last_slash = dir_path.find_last_of('/');
    if (last_slash != std::string::npos) {
        dir_path = dir_path.substr(0, last_slash);
    } else {
        dir_path = ".";
    }

    int tmp_fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (tmp_fd < 0) {
        return false;
    }

    ssize_t written = ::write(tmp_fd, ciphertext.data(), ciphertext.size());
    if (written < 0 || static_cast<size_t>(written) != ciphertext.size()) {
        ::close(tmp_fd);
        ::unlink(tmp_path.c_str());
        return false;
    }

    if (::fsync(tmp_fd) != 0) {
        ::close(tmp_fd);
        ::unlink(tmp_path.c_str());
        return false;
    }

    ::close(tmp_fd);

    if (::rename(tmp_path.c_str(), real_path.c_str()) != 0) {
        ::unlink(tmp_path.c_str());
        return false;
    }

    int dfd = ::open(dir_path.c_str(), O_RDONLY);
    if (dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }

    return true;
}

bool FileSystem::WriteFile(const std::string& fuse_path,
                           const std::vector<uint8_t>& data,
                           size_t offset, bool truncate) {
    if (IsMetaPath(fuse_path)) return false;

    std::string real_path = MapPath(fuse_path);
    bool is_shared = IsFileShared(fuse_path);

    std::vector<uint8_t> existing_plaintext;
    if (!truncate) {
        std::vector<uint8_t> read_result;
        if (ReadFile(fuse_path, read_result, 0, SIZE_MAX)) {
            existing_plaintext = std::move(read_result);
        }
    }

    std::vector<uint8_t> new_plaintext;
    if (offset > existing_plaintext.size()) {
        new_plaintext.resize(offset, 0);
        new_plaintext.insert(new_plaintext.end(), data.begin(), data.end());
    } else {
        new_plaintext = existing_plaintext;
        size_t end = offset + data.size();
        if (end > new_plaintext.size()) {
            new_plaintext.resize(end);
        }
        std::memcpy(new_plaintext.data() + offset, data.data(), data.size());
    }

    Crypto::SecureClear(existing_plaintext.data(), existing_plaintext.size());

    size_t ciphertext_capacity = FILE_HEADER_LEN + new_plaintext.size();
    std::vector<uint8_t> ciphertext(ciphertext_capacity);
    size_t ciphertext_len = ciphertext_capacity;

    bool encrypt_success = false;

    if (is_shared) {
        std::vector<uint8_t> file_key;
        if (ReadFileKey(real_path, file_key)) {
            encrypt_success = Crypto::EncryptFile(file_key.data(), new_plaintext.data(),
                                                   new_plaintext.size(), ciphertext.data(), ciphertext_len);
            Crypto::SecureClear(file_key.data(), file_key.size());
        }
    } else {
        const uint8_t* data_key = key_manager_.GetDataKey();
        if (data_key) {
            encrypt_success = Crypto::EncryptFile(data_key, new_plaintext.data(),
                                                   new_plaintext.size(), ciphertext.data(), ciphertext_len);
        }
    }

    Crypto::SecureClear(new_plaintext.data(), new_plaintext.size());

    if (!encrypt_success) {
        Crypto::SecureClear(ciphertext.data(), ciphertext.size());
        return false;
    }

    bool success = AtomicWrite(real_path, ciphertext);
    Crypto::SecureClear(ciphertext.data(), ciphertext.size());
    return success;
}

bool FileSystem::FlushFile(const std::string& fuse_path) {
    if (IsMetaPath(fuse_path)) return false;

    std::string real_path = MapPath(fuse_path);

    int fd = ::open(real_path.c_str(), O_RDONLY);
    if (fd < 0) {
        std::string tmp_path = GetTempPath(real_path);
        fd = ::open(tmp_path.c_str(), O_RDONLY);
        if (fd < 0) return false;
    }

    int ret = ::fsync(fd);
    ::close(fd);
    return ret == 0;
}

bool FileSystem::CreateFile(const std::string& fuse_path, mode_t mode) {
    if (IsMetaPath(fuse_path)) return false;

    std::string real_path = MapPath(fuse_path);
    const uint8_t* data_key = key_manager_.GetDataKey();
    if (!data_key) return false;

    std::vector<uint8_t> ciphertext(FILE_HEADER_LEN);
    size_t ciphertext_len = FILE_HEADER_LEN;

    if (!Crypto::EncryptFile(data_key, nullptr, 0,
                             ciphertext.data(), ciphertext_len)) {
        return false;
    }

    bool success = AtomicWrite(real_path, ciphertext);
    if (success) {
        ::chmod(real_path.c_str(), mode);
    }
    Crypto::SecureClear(ciphertext.data(), ciphertext.size());
    return success;
}

bool FileSystem::CreateSharedFile(const std::string& fuse_path, mode_t mode) {
    if (IsMetaPath(fuse_path)) return false;

    std::string real_path = MapPath(fuse_path);

    std::vector<uint8_t> file_key(KEY_LEN);
    if (!Crypto::GenerateRandomKey(file_key.data(), KEY_LEN)) {
        return false;
    }

    std::vector<uint8_t> ciphertext(FILE_HEADER_LEN);
    size_t ciphertext_len = FILE_HEADER_LEN;
    if (!Crypto::EncryptFile(file_key.data(), nullptr, 0,
                             ciphertext.data(), ciphertext_len)) {
        Crypto::SecureClear(file_key.data(), file_key.size());
        return false;
    }

    if (!AtomicWrite(real_path, ciphertext)) {
        Crypto::SecureClear(file_key.data(), file_key.size());
        return false;
    }

    ::chmod(real_path.c_str(), mode);

    const std::string& current_user = user_manager_.GetCurrentUser();
    if (!WriteFileKeys(real_path, file_key, {current_user})) {
        Crypto::SecureClear(file_key.data(), file_key.size());
        ::unlink(real_path.c_str());
        return false;
    }

    SetSharedFlag(real_path);
    Crypto::SecureClear(file_key.data(), file_key.size());
    return true;
}

bool FileSystem::CreateDirectory(const std::string& fuse_path, mode_t mode) {
    if (IsMetaPath(fuse_path)) return false;

    std::string real_path = MapPath(fuse_path);
    return ::mkdir(real_path.c_str(), mode) == 0;
}

bool FileSystem::RemoveFile(const std::string& fuse_path) {
    if (IsMetaPath(fuse_path)) return false;

    std::string real_path = MapPath(fuse_path);
    if (::unlink(real_path.c_str()) != 0) return false;

    std::string tmp_path = GetTempPath(real_path);
    ::unlink(tmp_path.c_str());

    return true;
}

bool FileSystem::RemoveDirectory(const std::string& fuse_path) {
    if (IsMetaPath(fuse_path)) return false;

    std::string real_path = MapPath(fuse_path);
    return ::rmdir(real_path.c_str()) == 0;
}

bool FileSystem::Rename(const std::string& old_fuse_path,
                        const std::string& new_fuse_path) {
    if (IsMetaPath(old_fuse_path) || IsMetaPath(new_fuse_path)) return false;

    std::string old_real = MapPath(old_fuse_path);
    std::string new_real = MapPath(new_fuse_path);

    if (::rename(old_real.c_str(), new_real.c_str()) != 0) return false;

    std::string old_tmp = GetTempPath(old_real);
    std::string new_tmp = GetTempPath(new_real);
    ::rename(old_tmp.c_str(), new_tmp.c_str());

    return true;
}

bool FileSystem::TruncateFile(const std::string& fuse_path, off_t size) {
    if (IsMetaPath(fuse_path)) return false;

    std::vector<uint8_t> data;
    if (size > 0) {
        std::vector<uint8_t> existing;
        if (ReadFile(fuse_path, existing, 0, SIZE_MAX)) {
            if (size < static_cast<off_t>(existing.size())) {
                data.assign(existing.begin(), existing.begin() + size);
            } else {
                data = existing;
                data.resize(size, 0);
            }
        } else {
            data.resize(size, 0);
        }
    }

    return WriteFile(fuse_path, data, 0, true);
}

bool FileSystem::ShareFile(const std::string& fuse_path, const std::string& username) {
    if (IsMetaPath(fuse_path)) return false;
    if (!user_manager_.HasUser(username)) return false;

    std::string real_path = MapPath(fuse_path);

    if (!IsFileShared(fuse_path)) {
        std::vector<uint8_t> existing;
        if (!ReadFile(fuse_path, existing, 0, SIZE_MAX)) return false;

        std::vector<uint8_t> file_key(KEY_LEN);
        if (!Crypto::GenerateRandomKey(file_key.data(), KEY_LEN)) return false;

        size_t ct_capacity = FILE_HEADER_LEN + existing.size();
        std::vector<uint8_t> ciphertext(ct_capacity);
        size_t ct_len = ct_capacity;

        if (!Crypto::EncryptFile(file_key.data(), existing.data(), existing.size(),
                                 ciphertext.data(), ct_len)) {
            Crypto::SecureClear(file_key.data(), file_key.size());
            return false;
        }

        if (!AtomicWrite(real_path, ciphertext)) {
            Crypto::SecureClear(file_key.data(), file_key.size());
            return false;
        }

        const std::string& current_user = user_manager_.GetCurrentUser();
        if (!WriteFileKeys(real_path, file_key, {current_user, username})) {
            Crypto::SecureClear(file_key.data(), file_key.size());
            return false;
        }

        SetSharedFlag(real_path);
        Crypto::SecureClear(file_key.data(), file_key.size());
    } else {
        std::vector<uint8_t> file_key;
        if (!ReadFileKey(real_path, file_key)) return false;

        EVP_PKEY* pub_key = user_manager_.GetUserPublicKey(username);
        if (!pub_key) {
            Crypto::SecureClear(file_key.data(), file_key.size());
            return false;
        }

        std::vector<uint8_t> encrypted_key;
        if (!Crypto::RSAEncryptKey(pub_key, file_key.data(), file_key.size(),
                                    encrypted_key)) {
            EVP_PKEY_free(pub_key);
            Crypto::SecureClear(file_key.data(), file_key.size());
            return false;
        }
        EVP_PKEY_free(pub_key);

        std::string xattr_name = std::string(XATTR_KEY_PREFIX) + username;
        if (!SetXattr(real_path, xattr_name, encrypted_key)) {
            Crypto::SecureClear(file_key.data(), file_key.size());
            return false;
        }

        Crypto::SecureClear(file_key.data(), file_key.size());
    }

    return true;
}

bool FileSystem::UnshareFile(const std::string& fuse_path, const std::string& username) {
    if (IsMetaPath(fuse_path)) return false;

    std::string real_path = MapPath(fuse_path);
    RemoveFileKey(real_path, username);

    auto users = GetFileUsers(fuse_path);
    if (users.size() <= 1) {
        ClearSharedFlag(real_path);
    }

    return true;
}

std::vector<std::string> FileSystem::GetFileUsers(const std::string& fuse_path) const {
    std::vector<std::string> result;
    if (IsMetaPath(fuse_path)) return result;

    std::string real_path = MapPath(fuse_path);
    auto xattrs = ListXattrs(real_path);

    for (const auto& attr : xattrs) {
        if (attr.find(XATTR_KEY_PREFIX) == 0) {
            std::string username = attr.substr(std::strlen(XATTR_KEY_PREFIX));
            result.push_back(username);
        }
    }

    return result;
}

void FileSystem::ScanDirAndRepair(const std::string& real_dir,
                                  const std::string& fuse_parent,
                                  size_t& recovered, size_t& failed, size_t& total_tmp) {
    DIR* dir = ::opendir(real_dir.c_str());
    if (!dir) return;

    struct dirent* entry;
    while ((entry = ::readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name == "." || name == ".." || name == META_DIR) continue;

        std::string full_path = real_dir + "/" + name;

        struct stat st;
        if (::lstat(full_path.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            std::string sub_fuse_parent;
            try {
                sub_fuse_parent = fuse_parent.empty() ? "/" :
                    fuse_parent + "/" + DecryptComponent(name, fuse_parent);
            } catch (...) {
                sub_fuse_parent = fuse_parent + "/" + name;
            }
            ScanDirAndRepair(full_path, sub_fuse_parent, recovered, failed, total_tmp);
        } else if (S_ISREG(st.st_mode)) {
            if (name.size() > 4 && name.substr(name.size() - 4) == TMP_SUFFIX) {
                total_tmp++;

                std::string real_path = full_path.substr(0, full_path.size() - 4);
                std::string real_name = name.substr(0, name.size() - 4);

                bool valid = false;
                {
                    std::ifstream file(full_path, std::ios::binary | std::ios::ate);
                    if (file) {
                        auto fsize = file.tellg();
                        if (fsize > static_cast<std::streamoff>(FILE_HEADER_LEN)) {
                            file.seekg(0, std::ios::beg);
                            std::vector<uint8_t> encrypted(static_cast<size_t>(fsize));
                            file.read(reinterpret_cast<char*>(encrypted.data()), fsize);
                            file.close();

                            const uint8_t* data_key = key_manager_.GetDataKey();
                            if (data_key) {
                                std::vector<uint8_t> plaintext(static_cast<size_t>(fsize) - FILE_HEADER_LEN);
                                size_t pt_len = plaintext.size();
                                if (Crypto::DecryptFile(data_key, encrypted.data(), encrypted.size(),
                                                         plaintext.data(), pt_len)) {
                                    valid = true;
                                    Crypto::SecureClear(plaintext.data(), plaintext.size());
                                }
                                Crypto::SecureClear(encrypted.data(), encrypted.size());
                            }
                        }
                    }
                }

                if (valid) {
                    if (::rename(full_path.c_str(), real_path.c_str()) == 0) {
                        recovered++;
                        std::cout << "  [RECOVERED] "
                                  << (fuse_parent.empty() ? "/" : fuse_parent)
                                  << " -> " << real_name << std::endl;
                    } else {
                        failed++;
                        std::cout << "  [FAILED]    rename failed: " << full_path << std::endl;
                    }
                } else {
                    failed++;
                    std::cout << "  [CORRUPT]   " << full_path
                              << " (decryption failed, cannot recover)" << std::endl;
                }
            }
        }
    }

    ::closedir(dir);
}

bool FileSystem::ScanAndRepair(size_t& recovered, size_t& failed, size_t& total_tmp) {
    recovered = 0;
    failed = 0;
    total_tmp = 0;

    std::cout << "Scanning for temporary files..." << std::endl;
    ScanDirAndRepair(root_dir_, "", recovered, failed, total_tmp);

    std::cout << "\nRepair summary:" << std::endl;
    std::cout << "  Temporary files found: " << total_tmp << std::endl;
    std::cout << "  Successfully recovered: " << recovered << std::endl;
    std::cout << "  Failed/corrupt:        " << failed << std::endl;

    return true;
}

bool FileSystem::ReadFile(const std::string& fuse_path,
                          std::vector<uint8_t>& out_data,
                          const std::string& username) const {
    if (IsMetaPath(fuse_path)) return false;

    std::string real_path = MapPath(fuse_path);

    std::ifstream file(real_path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::string tmp_path = GetTempPath(real_path);
        file.open(tmp_path, std::ios::binary | std::ios::ate);
        if (!file) return false;
    }

    auto file_size = file.tellg();
    if (file_size <= 0) {
        out_data.clear();
        return true;
    }

    size_t encrypted_size = static_cast<size_t>(file_size);
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> encrypted(encrypted_size);
    file.read(reinterpret_cast<char*>(encrypted.data()), encrypted_size);
    file.close();

    if (encrypted_size < FILE_HEADER_LEN) {
        return false;
    }

    size_t plaintext_capacity = encrypted_size - FILE_HEADER_LEN;
    std::vector<uint8_t> plaintext(plaintext_capacity);
    size_t plaintext_len = plaintext_capacity;

    bool is_shared = IsFileShared(fuse_path);
    bool decrypt_success = false;

    if (is_shared) {
        std::vector<uint8_t> file_key;
        if (ReadFileKey(real_path, file_key)) {
            decrypt_success = Crypto::DecryptFile(file_key.data(), encrypted.data(),
                                                   encrypted_size, plaintext.data(), plaintext_len);
            Crypto::SecureClear(file_key.data(), file_key.size());
        }
    } else {
        const uint8_t* data_key = key_manager_.GetDataKey();
        if (data_key) {
            decrypt_success = Crypto::DecryptFile(data_key, encrypted.data(),
                                                   encrypted_size, plaintext.data(), plaintext_len);
        }
    }

    Crypto::SecureClear(encrypted.data(), encrypted.size());

    if (!decrypt_success) {
        Crypto::SecureClear(plaintext.data(), plaintext.size());
        return false;
    }

    out_data.assign(plaintext.begin(), plaintext.begin() + plaintext_len);
    Crypto::SecureClear(plaintext.data(), plaintext.size());
    return true;
}

bool FileSystem::WriteFile(const std::string& fuse_path,
                           const std::vector<uint8_t>& data,
                           const std::string& username) {
    if (IsMetaPath(fuse_path)) return false;

    std::string real_path = MapPath(fuse_path);
    bool is_shared = IsFileShared(fuse_path);

    size_t ciphertext_capacity = FILE_HEADER_LEN + data.size();
    std::vector<uint8_t> ciphertext(ciphertext_capacity);
    size_t ciphertext_len = ciphertext_capacity;

    bool encrypt_success = false;

    if (is_shared) {
        std::vector<uint8_t> file_key;
        if (ReadFileKey(real_path, file_key)) {
            encrypt_success = Crypto::EncryptFile(file_key.data(), data.data(),
                                                   data.size(), ciphertext.data(), ciphertext_len);
            Crypto::SecureClear(file_key.data(), file_key.size());
        }
    } else {
        const uint8_t* data_key = key_manager_.GetDataKey();
        if (data_key) {
            encrypt_success = Crypto::EncryptFile(data_key, data.data(),
                                                   data.size(), ciphertext.data(), ciphertext_len);
        }
    }

    if (!encrypt_success) {
        Crypto::SecureClear(ciphertext.data(), ciphertext.size());
        return false;
    }

    bool success = AtomicWrite(real_path, ciphertext);
    Crypto::SecureClear(ciphertext.data(), ciphertext.size());
    return success;
}

bool FileSystem::IsShared(const std::string& real_path) const {
    std::vector<uint8_t> value;
    return GetXattr(real_path, XATTR_SHARED_FLAG, value);
}

std::vector<std::string> FileSystem::GetSharedUsers(const std::string& real_path) const {
    std::vector<std::string> result;
    auto xattrs = ListXattrs(real_path);
    for (const auto& attr : xattrs) {
        if (attr.find(XATTR_KEY_PREFIX) == 0) {
            std::string username = attr.substr(std::strlen(XATTR_KEY_PREFIX));
            result.push_back(username);
        }
    }
    return result;
}

std::vector<std::string> FileSystem::ScanSharedFiles(const std::string& username) const {
    std::vector<std::string> result;
    std::string xattr_name = XATTR_KEY_PREFIX + username;

    std::function<void(const std::string&, const std::string&)> scan =
        [&](const std::string& real_dir, const std::string& fuse_parent) {
            DIR* dir = ::opendir(real_dir.c_str());
            if (!dir) return;

            struct dirent* entry;
            while ((entry = ::readdir(dir)) != nullptr) {
                std::string name(entry->d_name);
                if (name == "." || name == ".." || name == META_DIR) continue;

                std::string full_path = real_dir + "/" + name;
                struct stat st;
                if (::lstat(full_path.c_str(), &st) != 0) continue;

                if (S_ISDIR(st.st_mode)) {
                    std::string sub_fuse;
                    try {
                        sub_fuse = fuse_parent.empty() ? "/" :
                            fuse_parent + "/" + DecryptComponent(name, fuse_parent);
                    } catch (...) {
                        sub_fuse = fuse_parent + "/" + name;
                    }
                    scan(full_path, sub_fuse);
                } else if (S_ISREG(st.st_mode)) {
                    std::vector<uint8_t> value;
                    if (GetXattr(full_path, xattr_name, value)) {
                        std::string fuse_path;
                        try {
                            std::string decrypted_name = DecryptComponent(name, fuse_parent);
                            fuse_path = fuse_parent.empty() ? "/" + decrypted_name :
                                fuse_parent + "/" + decrypted_name;
                        } catch (...) {
                            fuse_path = fuse_parent.empty() ? "/" + name :
                                fuse_parent + "/" + name;
                        }
                        result.push_back(fuse_path);
                    }
                }
            }
            ::closedir(dir);
        };

    scan(root_dir_, "");
    return result;
}

}
