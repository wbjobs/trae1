#include "background_task.h"
#include "filesystem.h"
#include "user_manager.h"
#include "crypto.h"
#include "config.h"

#include <iostream>

namespace fusefs {

BackgroundTask::BackgroundTask(FileSystem& fs, UserManager& um)
    : fs_(fs), um_(um) {
}

BackgroundTask::~BackgroundTask() {
    Stop();
}

void BackgroundTask::Start() {
    running_ = true;
    thread_ = std::make_unique<std::thread>(&BackgroundTask::WorkerThread, this);
}

void BackgroundTask::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    thread_.reset();
}

void BackgroundTask::SubmitTask(const ReEncryptTask& task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(task);
    }
    cv_.notify_one();
}

size_t BackgroundTask::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

size_t BackgroundTask::GetCompletedCount() const {
    return completed_.load();
}

void BackgroundTask::WorkerThread() {
    while (running_ || !tasks_.empty()) {
        ReEncryptTask task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !tasks_.empty() || !running_; });

            if (tasks_.empty() && !running_) break;

            task = tasks_.front();
            tasks_.pop();
        }

        if (ProcessTask(task)) {
            completed_++;
        }
    }
}

bool BackgroundTask::ProcessTask(const ReEncryptTask& task) {
    std::string real_path = fs_.MapPath(task.fuse_path);

    std::vector<uint8_t> existing;
    if (!fs_.ReadFile(task.fuse_path, existing, 0, SIZE_MAX)) {
        std::cerr << "  [RE-ENCRYPT] Failed to read: " << task.fuse_path << std::endl;
        return false;
    }

    std::vector<uint8_t> new_file_key(KEY_LEN);
    if (!Crypto::GenerateRandomKey(new_file_key.data(), KEY_LEN)) {
        Crypto::SecureClear(existing.data(), existing.size());
        return false;
    }

    size_t ct_capacity = FILE_HEADER_LEN + existing.size();
    std::vector<uint8_t> ciphertext(ct_capacity);
    size_t ct_len = ct_capacity;

    if (!Crypto::EncryptFile(new_file_key.data(), existing.data(), existing.size(),
                             ciphertext.data(), ct_len)) {
        Crypto::SecureClear(existing.data(), existing.size());
        Crypto::SecureClear(new_file_key.data(), new_file_key.size());
        return false;
    }

    std::string tmp_path = real_path + ".reenc_tmp";
    int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        Crypto::SecureClear(existing.data(), existing.size());
        Crypto::SecureClear(new_file_key.data(), new_file_key.size());
        return false;
    }

    ::write(fd, ciphertext.data(), ct_len);
    ::fsync(fd);
    ::close(fd);

    std::vector<std::string> valid_users;
    for (const auto& username : task.remaining_users) {
        EVP_PKEY* pub_key = um_.GetUserPublicKey(username);
        if (!pub_key) continue;

        std::vector<uint8_t> encrypted_key;
        if (Crypto::RSAEncryptKey(pub_key, new_file_key.data(), new_file_key.size(),
                                   encrypted_key)) {
            std::string xattr_name = std::string(XATTR_KEY_PREFIX) + username;
            ::setxattr(tmp_path.c_str(), xattr_name.c_str(),
                       encrypted_key.data(), encrypted_key.size(), 0);
            valid_users.push_back(username);
        }
        EVP_PKEY_free(pub_key);
    }

    std::vector<uint8_t> flag = {'1'};
    ::setxattr(tmp_path.c_str(), XATTR_SHARED_FLAG, flag.data(), flag.size(), 0);

    struct stat st;
    if (::lstat(real_path.c_str(), &st) == 0) {
        ::chmod(tmp_path.c_str(), st.st_mode);
    }

    if (::rename(tmp_path.c_str(), real_path.c_str()) != 0) {
        ::unlink(tmp_path.c_str());
        Crypto::SecureClear(existing.data(), existing.size());
        Crypto::SecureClear(new_file_key.data(), new_file_key.size());
        std::cerr << "  [RE-ENCRYPT] Failed to rename: " << task.fuse_path << std::endl;
        return false;
    }

    std::string revoke_xattr = std::string(XATTR_KEY_PREFIX) + task.revoked_user;
    ::removexattr(real_path.c_str(), revoke_xattr.c_str());

    Crypto::SecureClear(existing.data(), existing.size());
    Crypto::SecureClear(new_file_key.data(), new_file_key.size());

    std::cout << "  [RE-ENCRYPT] Completed: " << task.fuse_path
              << " (users: " << valid_users.size() << ")" << std::endl;
    return true;
}

void BackgroundTask::RevokeUser(const std::string& username) {
    std::function<void(const std::string&)> scan_dir = [&](const std::string& fuse_dir) {
        std::vector<std::string> entries;
        if (!fs_.ReadDir(fuse_dir, entries)) return;

        for (const auto& entry : entries) {
            if (entry == "." || entry == "..") continue;

            std::string fuse_path = (fuse_dir == "/") ?
                "/" + entry : fuse_dir + "/" + entry;

            std::vector<uint8_t> val;
            std::string real_path = fs_.MapPath(fuse_path);

            if (::getxattr(real_path.c_str(), XATTR_SHARED_FLAG, nullptr, 0) > 0) {
                auto file_users = fs_.GetFileUsers(fuse_path);
                bool has_user = false;
                std::vector<std::string> remaining;
                for (const auto& u : file_users) {
                    if (u == username) {
                        has_user = true;
                    } else {
                        remaining.push_back(u);
                    }
                }

                if (has_user && !remaining.empty()) {
                    ReEncryptTask task;
                    task.fuse_path = fuse_path;
                    task.revoked_user = username;
                    task.remaining_users = remaining;
                    SubmitTask(task);
                }
            }

            struct stat st;
            if (fs_.GetAttr(fuse_path, st) && S_ISDIR(st.st_mode)) {
                scan_dir(fuse_path);
            }
        }
    };

    scan_dir("/");
}

}
