#include "common/protocol.h"
#include <random>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <openssl/rand.h>

namespace moshpp {

std::string generate_session_id() {
    std::vector<uint8_t> bytes = generate_random_bytes(SESSION_ID_LENGTH);
    std::stringstream ss;
    for (size_t i = 0; i < bytes.size(); ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]);
    }
    return ss.str();
}

std::vector<uint8_t> generate_random_bytes(size_t length) {
    std::vector<uint8_t> bytes(length);
    if (RAND_bytes(bytes.data(), static_cast<int>(length)) != 1) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for (size_t i = 0; i < length; ++i) {
            bytes[i] = static_cast<uint8_t>(dis(gen));
        }
    }
    return bytes;
}

uint64_t get_timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

}
