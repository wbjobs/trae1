#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace moshpp {

constexpr uint16_t DEFAULT_PORT = 60001;
constexpr size_t MAX_PACKET_SIZE = 1400;
constexpr size_t MAX_SESSIONS = 1000;
constexpr uint32_t SESSION_ID_LENGTH = 16;
constexpr uint32_t KEY_LENGTH = 32;
constexpr uint32_t IV_LENGTH = 16;
constexpr uint32_t AUTH_TAG_LENGTH = 16;

enum class PacketType : uint8_t {
    HELLO = 0x01,
    HELLO_ACK = 0x02,
    DATA = 0x03,
    ACK = 0x04,
    SYNC = 0x05,
    SYNC_ACK = 0x06,
    REKEY = 0x07,
    REKEY_ACK = 0x08,
    PING = 0x09,
    PONG = 0x0A,
    FIN = 0x0B,
    FIN_ACK = 0x0C,
    RESUME = 0x0D,
    RESUME_ACK = 0x0E,
    DELTA_REQUEST = 0x0F,
    DELTA_RESPONSE = 0x10,
    ROAMING_JOIN = 0x11,
    ROAMING_LEAVE = 0x12,
    ROAMING_SYNC = 0x13,
    ROAMING_NOTIFICATION = 0x14,
    ROAMING_STATE = 0x15,
    ERROR = 0xFF
};

struct PacketHeader {
    PacketType type;
    uint8_t flags;
    uint16_t length;
    uint32_t seq_num;
    uint32_t ack_num;
    uint32_t timestamp;
    uint8_t session_id[SESSION_ID_LENGTH];
} __attribute__((packed));

struct HelloPacket {
    PacketHeader header;
    uint8_t client_key[KEY_LENGTH];
    uint8_t iv[IV_LENGTH];
    uint32_t protocol_version;
} __attribute__((packed));

struct ResumePacket {
    PacketHeader header;
    uint8_t session_id[SESSION_ID_LENGTH];
    uint32_t last_seq_num;
    uint32_t last_ack_num;
} __attribute__((packed));

struct DataPacket {
    PacketHeader header;
    uint8_t iv[IV_LENGTH];
    uint8_t auth_tag[AUTH_TAG_LENGTH];
    uint8_t encrypted_data[MAX_PACKET_SIZE - sizeof(PacketHeader) - IV_LENGTH - AUTH_TAG_LENGTH];
} __attribute__((packed));

struct AckPacket {
    PacketHeader header;
    uint32_t rtt_estimate;
    uint32_t window_size;
} __attribute__((packed));

struct SyncPacket {
    PacketHeader header;
    uint32_t frame_num;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t screen_rows;
    uint32_t screen_cols;
} __attribute__((packed));

struct DeltaRequestPacket {
    PacketHeader header;
    uint64_t last_snapshot_id;
    uint64_t last_output_offset;
} __attribute__((packed));

struct DeltaResponsePacket {
    PacketHeader header;
    uint64_t current_snapshot_id;
    uint64_t current_output_offset;
    uint8_t has_delta;
    uint8_t full_sync_required;
    uint32_t delta_length;
    uint32_t snapshot_length;
    uint8_t data[];
} __attribute__((packed));

struct RoamingJoinPacket {
    PacketHeader header;
    uint8_t device_id[32];
    uint8_t user_id[64];
    uint8_t device_name[64];
    uint8_t access_mode;
    uint32_t auth_token_length;
    uint8_t auth_token[];
} __attribute__((packed));

struct RoamingLeavePacket {
    PacketHeader header;
    uint8_t device_id[32];
    uint8_t user_id[64];
} __attribute__((packed));

struct RoamingSyncPacket {
    PacketHeader header;
    uint8_t device_id[32];
    uint64_t base_version;
    uint64_t target_version;
    uint32_t operation_count;
    uint8_t operations[];
} __attribute__((packed));

struct RoamingNotificationPacket {
    PacketHeader header;
    uint8_t device_id[32];
    uint8_t notification_type;
    uint32_t message_length;
    uint8_t message[];
} __attribute__((packed));

struct RoamingStatePacket {
    PacketHeader header;
    uint8_t device_id[32];
    int32_t cursor_x;
    int32_t cursor_y;
    int32_t terminal_width;
    int32_t terminal_height;
    uint32_t directory_length;
    uint8_t current_directory[];
} __attribute__((packed));

std::string generate_session_id();
std::vector<uint8_t> generate_random_bytes(size_t length);
uint64_t get_timestamp_ms();

}
