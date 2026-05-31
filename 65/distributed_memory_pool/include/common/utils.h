#pragma once

#include <cstdint>
#include <cstring>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <shared_mutex>
#include <atomic>

namespace dmp {

class Logger {
public:
    enum class Level : uint8_t {
        DEBUG = 0,
        INFO = 1,
        WARN = 2,
        ERROR = 3,
        FATAL = 4
    };

    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void set_level(Level level) {
        level_.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
    }

    template<typename... Args>
    void log(Level level, const char* file, int line, const char* fmt, Args&&... args) {
        if (static_cast<uint8_t>(level) < level_.load(std::memory_order_relaxed)) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_buf);
#endif

        std::cerr << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
                  << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
                  << "[" << level_string(level) << "] "
                  << "[" << file << ":" << line << "] ";

        log_impl(fmt, std::forward<Args>(args)...);
        std::cerr << std::endl;
    }

private:
    Logger() = default;

    static const char* level_string(Level level) {
        switch (level) {
            case Level::DEBUG: return "DEBUG";
            case Level::INFO:  return "INFO ";
            case Level::WARN:  return "WARN ";
            case Level::ERROR: return "ERROR";
            case Level::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }

    void log_impl(const char* fmt) {
        std::cerr << fmt;
    }

    template<typename T, typename... Args>
    void log_impl(const char* fmt, T&& value, Args&&... args) {
        while (*fmt) {
            if (*fmt == '{' && *(fmt + 1) == '}') {
                std::cerr << value;
                log_impl(fmt + 2, std::forward<Args>(args)...);
                return;
            }
            std::cerr << *fmt++;
        }
    }

    std::mutex mutex_;
    std::atomic<uint8_t> level_{static_cast<uint8_t>(Level::INFO)};
};

#define DMP_LOG(level, ...) \
    ::dmp::Logger::instance().log(level, __FILE__, __LINE__, __VA_ARGS__)

#define DMP_DEBUG(...) DMP_LOG(::dmp::Logger::Level::DEBUG, __VA_ARGS__)
#define DMP_INFO(...)  DMP_LOG(::dmp::Logger::Level::INFO, __VA_ARGS__)
#define DMP_WARN(...)  DMP_LOG(::dmp::Logger::Level::WARN, __VA_ARGS__)
#define DMP_ERROR(...) DMP_LOG(::dmp::Logger::Level::ERROR, __VA_ARGS__)
#define DMP_FATAL(...) DMP_LOG(::dmp::Logger::Level::FATAL, __VA_ARGS__)

inline uint64_t current_timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline uint64_t current_timestamp_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

class ScopedTimer {
public:
    ScopedTimer(const char* name)
        : name_(name)
        , start_(std::chrono::high_resolution_clock::now())
    {}

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
        DMP_DEBUG("{} took {} us", name_, duration);
    }

private:
    const char* name_;
    std::chrono::high_resolution_clock::time_point start_;
};

class ReadWriteLock {
public:
    void lock_read() { mutex_.lock_shared(); }
    void unlock_read() { mutex_.unlock_shared(); }
    void lock_write() { mutex_.lock(); }
    void unlock_write() { mutex_.unlock(); }

private:
    std::shared_mutex mutex_;
};

class ReadLock {
public:
    explicit ReadLock(ReadWriteLock& rwlock) : rwlock_(rwlock) { rwlock_.lock_read(); }
    ~ReadLock() { rwlock_.unlock_read(); }
private:
    ReadWriteLock& rwlock_;
};

class WriteLock {
public:
    explicit WriteLock(ReadWriteLock& rwlock) : rwlock_(rwlock) { rwlock_.lock_write(); }
    ~WriteLock() { rwlock_.unlock_write(); }
private:
    ReadWriteLock& rwlock_;
};

}
