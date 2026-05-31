#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace fusefs {

class FileSystem;
class UserManager;

struct ReEncryptTask {
    std::string fuse_path;
    std::string revoked_user;
    std::vector<std::string> remaining_users;
};

class BackgroundTask {
public:
    BackgroundTask(FileSystem& fs, UserManager& um);
    ~BackgroundTask();

    BackgroundTask(const BackgroundTask&) = delete;
    BackgroundTask& operator=(const BackgroundTask&) = delete;

    void Start();
    void Stop();

    void SubmitTask(const ReEncryptTask& task);

    size_t GetPendingCount() const;
    size_t GetCompletedCount() const;

    void RevokeUser(const std::string& username);

private:
    void WorkerThread();
    bool ProcessTask(const ReEncryptTask& task);

    FileSystem& fs_;
    UserManager& um_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<ReEncryptTask> tasks_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> completed_{0};

    std::unique_ptr<std::thread> thread_;
};

}
