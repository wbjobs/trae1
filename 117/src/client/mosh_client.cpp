#include "client/mosh_client.h"
#include "client/terminal.h"
#include "common/utils.h"
#include "common/protocol.h"
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sstream>

namespace moshpp {

MoshClient::MoshClient()
    : running_(false)
    , connected_(false)
    , port_(DEFAULT_PORT)
    , terminal_rows_(24)
    , terminal_cols_(80)
    , last_snapshot_id_(0)
    , last_output_offset_(0)
    , access_mode_(SessionAccessMode::STEAL)
    , is_readonly_(false)
{
    crdt_manager_ = std::make_shared<CRDTSyncManager>();
    terminal_sync_manager_ = std::make_shared<TerminalSyncManager>();
    terminal_sync_manager_->set_crdt_manager(crdt_manager_);
}

MoshClient::~MoshClient() {
    disconnect();
}

bool MoshClient::connect(const std::string& host, uint16_t port, const std::string& session_id) {
    host_ = host;
    port_ = port;
    session_id_ = session_id.empty() ? generate_session_id() : session_id;

    server_endpoint_.ip = host;
    server_endpoint_.port = port;
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    server_endpoint_.addr = addr;

    if (!transport_.connect(host, port)) {
        return false;
    }

    transport_.set_data_callback([this](const std::vector<uint8_t>& data, const NetworkEndpoint& from) {
        on_data_received(data, from);
    });

    transport_.start_receive_thread();
    
    if (!device_id_.empty()) {
        crdt_manager_->add_device(device_id_);
        crdt_manager_->set_session_id(session_id_);
        terminal_sync_manager_->set_session_id(session_id_);
    }
    
    return true;
}

bool MoshClient::new_session(const std::string& host, uint16_t port) {
    session_id_ = generate_session_id();
    Logger::info("Creating new session: " + bytes_to_hex(std::vector<uint8_t>(
        reinterpret_cast<const uint8_t*>(session_id_.data()), 
        reinterpret_cast<const uint8_t*>(session_id_.data()) + session_id_.size()
    )));

    last_snapshot_id_ = 0;
    last_output_offset_ = 0;
    output_buffer_.clear();

    if (!connect(host, port, session_id_)) {
        return false;
    }

    crypto_.generate_keys();
    return perform_handshake();
}

bool MoshClient::attach_session(const std::string& host, uint16_t port, const std::string& session_id) {
    Logger::info("Attaching to session: " + session_id);
    
    std::vector<uint8_t> session_bytes = hex_to_bytes(session_id);
    if (session_bytes.size() != SESSION_ID_LENGTH) {
        Logger::error("Invalid session ID format");
        return false;
    }
    
    std::string raw_session_id(session_bytes.begin(), session_bytes.end());
    
    if (!connect(host, port, raw_session_id)) {
        return false;
    }

    return resume_session();
}

bool MoshClient::perform_handshake() {
    Logger::info("Performing handshake with " + host_ + ":" + std::to_string(port_));

    crypto_.generate_keys();
    send_hello();

    const int max_attempts = 10;
    for (int i = 0; i < max_attempts && !connected_; ++i) {
        sleep_ms(500);
    }

    if (!connected_) {
        Logger::error("Handshake timeout");
        return false;
    }

    Logger::info("Handshake successful");
    
    if (!device_id_.empty()) {
        perform_roaming_join();
    }
    
    return true;
}

bool MoshClient::resume_session() {
    Logger::info("Resuming session...");
    connected_ = false;

    const int max_attempts = 5;
    for (int i = 0; i < max_attempts; ++i) {
        send_resume();
        sleep_ms(500);
        if (connected_) {
            Logger::info("Session resumed, requesting delta sync...");
            send_delta_request();
            
            if (!device_id_.empty()) {
                perform_roaming_join();
            }
            
            return true;
        }
    }

    Logger::error("Failed to resume session");
    return false;
}

bool MoshClient::perform_roaming_join() {
    Logger::info("Performing roaming join, device: " + device_name_);
    send_roaming_join();
    return true;
}

void MoshClient::disconnect() {
    if (connected_) {
        if (!device_id_.empty()) {
            send_roaming_leave();
        }
        send_fin();
    }
    running_ = false;
    connected_ = false;
    
    if (input_thread_.joinable()) {
        input_thread_.join();
    }
    if (output_thread_.joinable()) {
        output_thread_.join();
    }
    if (reconnect_thread_.joinable()) {
        reconnect_thread_.join();
    }
    
    transport_.stop_receive_thread();
}

void MoshClient::run() {
    running_ = true;

    Terminal terminal;
    terminal.set_raw_mode();
    terminal.get_size(terminal_rows_, terminal_cols_);

    input_thread_ = std::thread(&MoshClient::input_thread_func, this);
    output_thread_ = std::thread(&MoshClient::output_thread_func, this);
    reconnect_thread_ = std::thread(&MoshClient::reconnect_thread_func, this);

    while (running_ && connected_) {
        sleep_ms(100);
    }

    terminal.restore_mode();
}

void MoshClient::set_terminal_size(uint32_t rows, uint32_t cols) {
    terminal_rows_ = rows;
    terminal_cols_ = cols;
    if (connected_) {
        send_sync();
    }
}

void MoshClient::send_input(const std::vector<uint8_t>& data) {
    if (!connected_) {
        return;
    }

    std::vector<uint8_t> encrypted = crypto_.encrypt(data);
    
    size_t max_data_size = MAX_PACKET_SIZE - sizeof(DataPacket);
    for (size_t offset = 0; offset < encrypted.size(); offset += max_data_size) {
        size_t chunk_size = std::min(max_data_size, encrypted.size() - offset);
        
        std::vector<uint8_t> packet(sizeof(DataPacket));
        DataPacket* pkt = reinterpret_cast<DataPacket*>(packet.data());
        
        pkt->header.type = PacketType::DATA;
        pkt->header.flags = 0;
        pkt->header.length = htons(static_cast<uint16_t>(sizeof(PacketHeader) + IV_LENGTH + AUTH_TAG_LENGTH + chunk_size));
        pkt->header.seq_num = 0;
        pkt->header.ack_num = 0;
        pkt->header.timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
        std::memcpy(pkt->header.session_id, session_id_.data(), SESSION_ID_LENGTH);

        std::memcpy(pkt->iv, crypto_.get_iv().data(), IV_LENGTH);
        std::memcpy(pkt->auth_tag, encrypted.data() + offset, AUTH_TAG_LENGTH);
        if (chunk_size > AUTH_TAG_LENGTH) {
            std::memcpy(pkt->encrypted_data, encrypted.data() + offset + AUTH_TAG_LENGTH, chunk_size - AUTH_TAG_LENGTH);
        }

        transport_.send_packet(server_endpoint_, packet);
    }
}

void MoshClient::on_data_received(const std::vector<uint8_t>& data, const NetworkEndpoint& from) {
    if (data.size() < sizeof(PacketHeader)) {
        return;
    }

    const PacketHeader* header = reinterpret_cast<const PacketHeader*>(data.data());
    
    switch (header->type) {
        case PacketType::HELLO_ACK:
            handle_hello_ack(*header, data);
            break;
        case PacketType::RESUME_ACK:
            handle_resume_ack(*header, data);
            break;
        case PacketType::DATA:
            handle_data(*header, data);
            break;
        case PacketType::ACK:
            handle_ack(*header, data);
            break;
        case PacketType::PONG:
            handle_pong(*header, data);
            break;
        case PacketType::FIN_ACK:
            handle_fin_ack(*header, data);
            break;
        case PacketType::SYNC:
            handle_sync(*header, data);
            break;
        case PacketType::DELTA_RESPONSE:
            handle_delta_response(*header, data);
            break;
        case PacketType::ROAMING_JOIN:
            handle_roaming_join_ack(*header, data);
            break;
        case PacketType::ROAMING_SYNC:
            handle_roaming_sync(*header, data);
            break;
        case PacketType::ROAMING_NOTIFICATION:
            handle_roaming_notification(*header, data);
            break;
        case PacketType::ROAMING_STATE:
            handle_roaming_state(*header, data);
            break;
        case PacketType::ERROR:
            if (data.size() > sizeof(PacketHeader)) {
                const char* msg = reinterpret_cast<const char*>(data.data() + sizeof(PacketHeader));
                Logger::error("Server error: " + std::string(msg));
            }
            connected_ = false;
            running_ = false;
            break;
        default:
            break;
    }
}

void MoshClient::handle_roaming_join_ack(const PacketHeader& header, const std::vector<uint8_t>& data) {
    is_readonly_ = (header.flags & 1) != 0;
    if (is_readonly_) {
        Logger::info("Connected in read-only mode");
    } else {
        Logger::info("Roaming join successful");
    }
}

void MoshClient::handle_roaming_sync(const PacketHeader& header, const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(RoamingSyncPacket)) {
        return;
    }

    const RoamingSyncPacket* sync = reinterpret_cast<const RoamingSyncPacket*>(data.data());
    std::string device_id(reinterpret_cast<const char*>(sync->device_id), 32);
    device_id = device_id.substr(0, device_id.find('\0'));
    
    uint32_t op_count = ntohl(sync->operation_count);
    
    if (crdt_manager_ && op_count > 0) {
        std::vector<CRDTOperation> operations;
        const uint8_t* op_data = sync->operations;
        
        for (uint32_t i = 0; i < op_count && op_data < data.data() + data.size(); i++) {
            size_t op_len = strlen(reinterpret_cast<const char*>(op_data));
            std::string op_str(reinterpret_cast<const char*>(op_data), op_len);
            operations.push_back(CRDTOperation::deserialize(op_str));
            op_data += op_len + 1;
        }

        crdt_manager_->apply_remote_operations(device_id, operations);
    }
}

void MoshClient::handle_roaming_notification(const PacketHeader& header, const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(RoamingNotificationPacket)) {
        return;
    }

    const RoamingNotificationPacket* notif = reinterpret_cast<const RoamingNotificationPacket*>(data.data());
    uint32_t msg_len = ntohl(notif->message_length);
    
    if (data.size() >= sizeof(RoamingNotificationPacket) + msg_len) {
        std::string message(reinterpret_cast<const char*>(notif->message), msg_len);
        Logger::info("Roaming notification: " + message);
        
        if (message.find("stolen") != std::string::npos) {
            is_readonly_ = true;
        }
    }
}

void MoshClient::handle_roaming_state(const PacketHeader& header, const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(RoamingStatePacket)) {
        return;
    }

    const RoamingStatePacket* state = reinterpret_cast<const RoamingStatePacket*>(data.data());
    
    if (terminal_sync_manager_) {
        terminal_sync_manager_->update_cursor(ntohl(state->cursor_x), ntohl(state->cursor_y));
        terminal_sync_manager_->update_terminal_size(ntohl(state->terminal_width), ntohl(state->terminal_height));
        
        uint32_t dir_len = ntohl(state->directory_length);
        if (data.size() >= sizeof(RoamingStatePacket) + dir_len) {
            std::string dir(reinterpret_cast<const char*>(state->current_directory), dir_len);
            terminal_sync_manager_->update_current_directory(dir);
        }
    }
}

void MoshClient::handle_hello_ack(const PacketHeader& header, const std::vector<uint8_t>& data) {
    if (data.size() >= sizeof(HelloPacket)) {
        const HelloPacket* hello = reinterpret_cast<const HelloPacket*>(data.data());
        std::vector<uint8_t> server_key(hello->client_key, hello->client_key + KEY_LENGTH);
        std::vector<uint8_t> server_iv(hello->iv, hello->iv + IV_LENGTH);
        crypto_.init(server_key, server_iv);
    }
    connected_ = true;
    Logger::info("Connected to server");
}

void MoshClient::handle_resume_ack(const PacketHeader& header, const std::vector<uint8_t>& data) {
    connected_ = true;
    Logger::info("Session resumed");
}

void MoshClient::handle_data(const PacketHeader& header, const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(DataPacket)) {
        return;
    }

    const DataPacket* packet = reinterpret_cast<const DataPacket*>(data.data());
    std::vector<uint8_t> iv(packet->iv, packet->iv + IV_LENGTH);
    size_t encrypted_len = ntohs(packet->header.length) - sizeof(PacketHeader) - IV_LENGTH - AUTH_TAG_LENGTH;
    std::vector<uint8_t> encrypted_data(packet->auth_tag, packet->auth_tag + AUTH_TAG_LENGTH + encrypted_len);

    try {
        std::vector<uint8_t> plaintext = crypto_.decrypt(encrypted_data, iv);
        output_buffer_.insert(output_buffer_.end(), plaintext.begin(), plaintext.end());
        last_output_offset_ += plaintext.size();
        render_screen(plaintext);
    } catch (const std::exception& e) {
        Logger::warn("Decryption failed: " + std::string(e.what()));
    }
}

void MoshClient::handle_ack(const PacketHeader& header, const std::vector<uint8_t>& data) {
    transport_.on_packet_ack(session_id_, header.ack_num);
}

void MoshClient::handle_pong(const PacketHeader& header, const std::vector<uint8_t>& data) {
}

void MoshClient::handle_fin_ack(const PacketHeader& header, const std::vector<uint8_t>& data) {
    connected_ = false;
    running_ = false;
}

void MoshClient::handle_sync(const PacketHeader& header, const std::vector<uint8_t>& data) {
}

void MoshClient::handle_delta_response(const PacketHeader& header, const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(DeltaResponsePacket)) {
        Logger::warn("Invalid DELTA_RESPONSE packet size");
        return;
    }

    const DeltaResponsePacket* resp = reinterpret_cast<const DeltaResponsePacket*>(data.data());
    
    uint64_t current_snapshot_id = be64toh(resp->current_snapshot_id);
    uint64_t current_output_offset = be64toh(resp->current_output_offset);
    bool has_delta = resp->has_delta != 0;
    bool full_sync_required = resp->full_sync_required != 0;
    uint32_t delta_length = be32toh(resp->delta_length);
    uint32_t snapshot_length = be32toh(resp->snapshot_length);

    Logger::info("Delta response received: snapshot=" + std::to_string(current_snapshot_id) + 
                 ", offset=" + std::to_string(current_output_offset) +
                 ", delta=" + std::to_string(delta_length) + " bytes" +
                 ", full_sync=" + (full_sync_required ? "yes" : "no"));

    last_snapshot_id_ = current_snapshot_id;

    size_t data_offset = sizeof(DeltaResponsePacket);
    
    if (full_sync_required && snapshot_length > 0 && data.size() >= data_offset + snapshot_length) {
        Logger::info("Performing full sync, restoring " + std::to_string(snapshot_length) + " bytes...");
        std::vector<uint8_t> snapshot_data(data.begin() + data_offset, data.begin() + data_offset + snapshot_length);
        output_buffer_ = snapshot_data;
        last_output_offset_ = current_output_offset;
        render_screen(snapshot_data);
        data_offset += snapshot_length;
    }
    
    if (has_delta && delta_length > 0 && data.size() >= data_offset + delta_length) {
        Logger::info("Applying delta: " + std::to_string(delta_length) + " bytes...");
        std::vector<uint8_t> delta_data(data.begin() + data_offset, data.begin() + data_offset + delta_length);
        output_buffer_.insert(output_buffer_.end(), delta_data.begin(), delta_data.end());
        last_output_offset_ = current_output_offset;
        render_screen(delta_data);
    }

    Logger::info("Sync complete, current output offset: " + std::to_string(last_output_offset_));
}

void MoshClient::send_hello() {
    std::vector<uint8_t> packet(sizeof(HelloPacket));
    HelloPacket* hello = reinterpret_cast<HelloPacket*>(packet.data());
    
    hello->header.type = PacketType::HELLO;
    hello->header.flags = 0;
    hello->header.length = htons(sizeof(HelloPacket));
    hello->header.seq_num = htonl(1);
    hello->header.ack_num = htonl(0);
    hello->header.timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(hello->header.session_id, session_id_.data(), SESSION_ID_LENGTH);
    
    std::memcpy(hello->client_key, crypto_.get_key().data(), KEY_LENGTH);
    std::memcpy(hello->iv, crypto_.get_iv().data(), IV_LENGTH);
    hello->protocol_version = htonl(0x01000000);

    transport_.send_packet(server_endpoint_, packet);
}

void MoshClient::send_resume() {
    std::vector<uint8_t> packet(sizeof(ResumePacket));
    ResumePacket* resume = reinterpret_cast<ResumePacket*>(packet.data());
    
    resume->header.type = PacketType::RESUME;
    resume->header.flags = 0;
    resume->header.length = htons(sizeof(ResumePacket));
    resume->header.seq_num = htonl(0);
    resume->header.ack_num = htonl(0);
    resume->header.timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(resume->header.session_id, session_id_.data(), SESSION_ID_LENGTH);
    
    resume->last_seq_num = htonl(0);
    resume->last_ack_num = htonl(0);

    transport_.send_packet(server_endpoint_, packet);
}

void MoshClient::send_ack(uint32_t ack_num) {
    std::vector<uint8_t> packet(sizeof(AckPacket));
    AckPacket* ack = reinterpret_cast<AckPacket*>(packet.data());
    
    ack->header.type = PacketType::ACK;
    ack->header.flags = 0;
    ack->header.length = htons(sizeof(AckPacket));
    ack->header.seq_num = htonl(0);
    ack->header.ack_num = htonl(ack_num);
    ack->header.timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(ack->header.session_id, session_id_.data(), SESSION_ID_LENGTH);
    
    ack->rtt_estimate = htonl(transport_.get_rtt(session_id_));
    ack->window_size = htonl(transport_.get_cwnd(session_id_));

    transport_.send_packet(server_endpoint_, packet);
}

void MoshClient::send_ping() {
    std::vector<uint8_t> packet(sizeof(PacketHeader));
    PacketHeader* header = reinterpret_cast<PacketHeader*>(packet.data());
    
    header->type = PacketType::PING;
    header->flags = 0;
    header->length = htons(sizeof(PacketHeader));
    header->seq_num = htonl(0);
    header->ack_num = htonl(0);
    header->timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(header->session_id, session_id_.data(), SESSION_ID_LENGTH);

    transport_.send_packet(server_endpoint_, packet);
}

void MoshClient::send_fin() {
    std::vector<uint8_t> packet(sizeof(PacketHeader));
    PacketHeader* header = reinterpret_cast<PacketHeader*>(packet.data());
    
    header->type = PacketType::FIN;
    header->flags = 0;
    header->length = htons(sizeof(PacketHeader));
    header->seq_num = htonl(0);
    header->ack_num = htonl(0);
    header->timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(header->session_id, session_id_.data(), SESSION_ID_LENGTH);

    transport_.send_packet(server_endpoint_, packet);
}

void MoshClient::send_sync() {
    std::vector<uint8_t> packet(sizeof(SyncPacket));
    SyncPacket* sync = reinterpret_cast<SyncPacket*>(packet.data());
    
    sync->header.type = PacketType::SYNC;
    sync->header.flags = 0;
    sync->header.length = htons(sizeof(SyncPacket));
    sync->header.seq_num = htonl(0);
    sync->header.ack_num = htonl(0);
    sync->header.timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(sync->header.session_id, session_id_.data(), SESSION_ID_LENGTH);
    
    sync->frame_num = htonl(0);
    sync->cursor_x = htonl(0);
    sync->cursor_y = htonl(0);
    sync->screen_rows = htonl(terminal_rows_);
    sync->screen_cols = htonl(terminal_cols_);

    transport_.send_packet(server_endpoint_, packet);
}

void MoshClient::send_delta_request() {
    Logger::info("Sending delta request: last_snapshot=" + std::to_string(last_snapshot_id_) + 
                 ", last_offset=" + std::to_string(last_output_offset_));

    std::vector<uint8_t> packet(sizeof(DeltaRequestPacket));
    DeltaRequestPacket* req = reinterpret_cast<DeltaRequestPacket*>(packet.data());
    
    req->header.type = PacketType::DELTA_REQUEST;
    req->header.flags = 0;
    req->header.length = htons(sizeof(DeltaRequestPacket));
    req->header.seq_num = htonl(0);
    req->header.ack_num = htonl(0);
    req->header.timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(req->header.session_id, session_id_.data(), SESSION_ID_LENGTH);
    
    req->last_snapshot_id = htobe64(last_snapshot_id_);
    req->last_output_offset = htobe64(last_output_offset_);

    transport_.send_packet(server_endpoint_, packet);
}

void MoshClient::send_roaming_join() {
    if (device_id_.empty()) return;
    
    Logger::info("Sending roaming join: device=" + device_name_);
    
    size_t total_size = sizeof(RoamingJoinPacket);
    std::vector<uint8_t> packet(total_size);
    
    RoamingJoinPacket* join = reinterpret_cast<RoamingJoinPacket*>(packet.data());
    
    join->header.type = PacketType::ROAMING_JOIN;
    join->header.flags = 0;
    join->header.length = htons(static_cast<uint16_t>(total_size));
    join->header.seq_num = htonl(0);
    join->header.ack_num = htonl(0);
    join->header.timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(join->header.session_id, session_id_.data(), SESSION_ID_LENGTH);
    
    std::memset(join->device_id, 0, 32);
    std::memcpy(join->device_id, device_id_.c_str(), std::min<size_t>(device_id_.size(), 32));
    
    std::memset(join->user_id, 0, 64);
    std::memcpy(join->user_id, user_id_.c_str(), std::min<size_t>(user_id_.size(), 64));
    
    std::memset(join->device_name, 0, 64);
    std::memcpy(join->device_name, device_name_.c_str(), std::min<size_t>(device_name_.size(), 64));
    
    join->access_mode = static_cast<uint8_t>(access_mode_);
    join->auth_token_length = 0;

    transport_.send_packet(server_endpoint_, packet);
}

void MoshClient::send_roaming_leave() {
    if (device_id_.empty()) return;
    
    Logger::info("Sending roaming leave: device=" + device_name_);
    
    std::vector<uint8_t> packet(sizeof(RoamingLeavePacket));
    
    RoamingLeavePacket* leave = reinterpret_cast<RoamingLeavePacket*>(packet.data());
    
    leave->header.type = PacketType::ROAMING_LEAVE;
    leave->header.flags = 0;
    leave->header.length = htons(sizeof(RoamingLeavePacket));
    leave->header.seq_num = htonl(0);
    leave->header.ack_num = htonl(0);
    leave->header.timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(leave->header.session_id, session_id_.data(), SESSION_ID_LENGTH);
    
    std::memset(leave->device_id, 0, 32);
    std::memcpy(leave->device_id, device_id_.c_str(), std::min<size_t>(device_id_.size(), 32));
    
    std::memset(leave->user_id, 0, 64);
    std::memcpy(leave->user_id, user_id_.c_str(), std::min<size_t>(user_id_.size(), 64));

    transport_.send_packet(server_endpoint_, packet);
}

void MoshClient::input_thread_func() {
    Terminal terminal;
    std::vector<uint8_t> buffer(1024);
    
    while (running_) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        
        timeval tv{0, 100000};
        int ret = select(STDIN_FILENO + 1, &read_fds, nullptr, nullptr, &tv);
        
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &read_fds)) {
            ssize_t n = read(STDIN_FILENO, buffer.data(), buffer.size());
            if (n > 0) {
                std::vector<uint8_t> data(buffer.begin(), buffer.begin() + n);
                send_input(data);
            } else if (n == 0) {
                break;
            }
        }
    }
}

void MoshClient::output_thread_func() {
    uint64_t last_ping = current_time_ms();
    
    while (running_) {
        sleep_ms(100);
        
        uint64_t now = current_time_ms();
        if (connected_ && now - last_ping > 5000) {
            send_ping();
            last_ping = now;
        }
    }
}

void MoshClient::reconnect_thread_func() {
    std::string last_ip = transport_.get_current_ip();
    
    while (running_) {
        sleep_ms(1000);
        
        if (!connected_) {
            continue;
        }

        std::string current_ip = transport_.get_current_ip();
        if (!current_ip.empty() && current_ip != last_ip) {
            Logger::info("Network change detected: " + last_ip + " -> " + current_ip);
            last_ip = current_ip;
            
            Logger::info("Attempting to reconnect...");
            attempt_reconnect();
        }
    }
}

void MoshClient::setup_terminal() {
}

void MoshClient::restore_terminal() {
}

void MoshClient::render_screen(const std::vector<uint8_t>& data) {
    write(STDOUT_FILENO, data.data(), data.size());
}

bool MoshClient::check_network_change() {
    return false;
}

void MoshClient::attempt_reconnect() {
    connected_ = false;
    
    transport_.close();
    sleep_ms(500);
    
    if (transport_.connect(host_, port_)) {
        transport_.start_receive_thread();
        resume_session();
    }
}

void MoshClient::sync_terminal_state() {
}

}
