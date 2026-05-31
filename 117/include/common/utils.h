#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace moshpp {

std::string bytes_to_hex(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> hex_to_bytes(const std::string& hex);

std::string base64_encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> base64_decode(const std::string& encoded);

void sleep_ms(uint32_t ms);
uint64_t current_time_ms();

std::string get_hostname();
std::string get_username();

bool file_exists(const std::string& path);
bool directory_exists(const std::string& path);
bool create_directory(const std::string& path);

std::string exec_command(const std::string& cmd);
std::vector<std::string> split_string(const std::string& s, char delimiter);

class Logger {
public:
    enum class Level {
        DEBUG,
        INFO,
        WARN,
        ERROR
    };

    static void set_level(Level level);
    static void log(Level level, const std::string& message);
    static void debug(const std::string& message);
    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);

private:
    static Level current_level_;
    static std::string level_to_string(Level level);
};

}
