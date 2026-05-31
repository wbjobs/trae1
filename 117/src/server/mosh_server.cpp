#include "server/mosh_server.h"
#include "server/pty.h"
#include "common/utils.h"
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sstream>

namespace moshpp {

MoshServer::MoshServer()
    : running_(false)
    , port_(DEFAULT_PORT)
    , snapshot_manager_("./mosh_snapshots")
    , session_timeout_(3600000)
    , snapshot_interval_(30000)
    , redis_host_("localhost")
    , redis_port_(6379)
    , roaming_enabled_(false)
{
    auth_manager_ = std::make_shared<AuthManager>();
    permission_manager_ = std::make_shared<PermissionManager>();
    session_store_ = std::make_shared<RedisSessionStore>();
    roaming_manager_ = std::make_shared<RoamingManager>();
    crdt_manager_ = std::make_shared<CRDTSyncManager>();
    terminal_sync_manager_ = std::make_shared<TerminalSyncManager>();
}

MoshServer::~MoshServer() {
    stop();
}

bool MoshServer::init_roaming() {
    session_store_->set_ttl(86400);
    session_store_->use_local_storage(true);
    
    if (!session_store_->connect(redis_host_, redis_port_)) {
        Logger::warn("Failed to connect to Redis, using local storage");
    }
    
    roaming_manager_->set_session_store(session_store_);
    roaming_manager_->set_permission_manager(permission_manager_);
    terminal_sync_manager_->set_crdt_manager(crdt_manager_);
    
    roaming_enabled_ = true;
    Logger::info("Multi-device roaming enabled");
    return true;
}

bool MoshServer::start(uint16_t port) {
    port_ = port;
    
    if (!transport_.bind(port_)) {
        return false;
    }

    transport_.set_data_callback([this](const std::vector<uint8_t>& data, const NetworkEndpoint& from) {
        on_data_received(data, from);
    });

    transport_.set_connect_callback([this](const std::string& session_id, const NetworkEndpoint& endpoint) {
        on_client_connected(session_id, endpoint);
    });

    transport_.set_disconnect_callback([this](const std::string& session_id) {
        on_client_disconnected(session_id);
    });

    transport_.start_receive_thread();
    
    running_ = true;
    cleanup_thread_ = std::thread(&MoshServer::cleanup_loop, this);
    snapshot_thread_ = std::thread(&MoshServer::snapshot_loop, this);

    Logger::info("Mosh++ server started on port " + std::to_string(port_));
    Logger::info("Maximum concurrent sessions: " + std::to_string(MAX_SESSIONS));
    Logger::info("Session timeout: " + std::to_string(session_timeout_ / 1000) + " seconds");
    Logger::info("Snapshot interval: " + std::to_string(snapshot_interval_ / 1000) + " seconds");
    
    return true;
}

void MoshServer::stop() {
    running_ = false;
    transport_.stop_receive_thread();
    
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    if (snapshot_thread_.joinable()) {
        snapshot_thread_.join();
    }
    
    Logger::info("Mosh++ server stopped");
}

void MoshServer::wait() {
    while (running_) {
        sleep_ms(1000);
    }
}

void MoshServer::on_data_received(const std::vector<uint8_t>& data, const NetworkEndpoint& from) {
    if (data.size() < sizeof(PacketHeader)) {
        return;
    }

    const PacketHeader* header = reinterpret_cast<const PacketHeader*>(data.data());
    std::string session_id(reinterpret_cast<const char*>(header->session_id), SESSION_ID_LENGTH);

    switch (header->type) {
        case PacketType::HELLO:
            handle_hello(*header, data, from);
            break;
        case PacketType::RESUME:
            handle_resume(*header, data, from);
            break;
        case PacketType::DATA:
            handle_data(*header, data, from);
            break;
        case PacketType::ACK:
            handle_ack(*header, data, from);
            break;
        case PacketType::PING:
            handle_ping(*header, data, from);
            break;
        case PacketType::FIN:
            handle_fin(*header, data, from);
            break;
        case PacketType::DELTA_REQUEST:
            handle_delta_request(*header, data, from);
            break;
        case PacketType::ROAMING_JOIN:
            handle_roaming_join(*header, data, from);
            break;
        case PacketType::ROAMING_LEAVE:
            handle_roaming_leave(*header, data, from);
            break;
        case PacketType::ROAMING_SYNC:
            handle_roaming_sync(*header, data, from);
            break;
        default:
            Logger::warn("Unknown packet type: " + std::to_string(static_cast<int>(header->type)));
            break;
    }
}

void MoshServer::on_client_connected(const std::string& session_id, const NetworkEndpoint& endpoint) {
    Logger::info("Client connected: " + bytes_to_hex(std::vector<uint8_t>(
        reinterpret_cast<const uint8_t*>(session_id.data()),
        reinterpret_cast<const uint8_t*>(session_id.data()) + SESSION_ID_LENGTH
    )).substr(0, 16) + " from " + endpoint.ip + ":" + std::to_string(endpoint.port));
}

void MoshServer::on_client_disconnected(const std::string& session_id) {
    Logger::info("Client disconnected: " + bytes_to_hex(std::vector<uint8_t>(
        reinterpret_cast<const uint8_t*>(session_id.data()),
        reinterpret_cast<const uint8_t*>(session_id.data()) + SESSION_ID_LENGTH
    )).substr(0, 16));
}

void MoshServer::handle_hello(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from) {
    if (data.size() < sizeof(HelloPacket)) {
        Logger::warn("Invalid HELLO packet size");
        return;
    }

    const HelloPacket* hello = reinterpret_cast<const HelloPacket*>(data.data());
    
    std::string session_id(reinterpret_cast<const char*>(hello->header.session_id), SESSION_ID_LENGTH);
    std::string session_id_hex = bytes_to_hex(std::vector<uint8_t>(
        hello->header.session_id, 
        hello->header.session_id + SESSION_ID_LENGTH
    ));
    
    Logger::info("HELLO from " + from.ip + ":" + std::to_string(from.port) + ", session: " + session_id_hex.substr(0, 16));

    auto session = session_manager_.get_session(session_id);
    if (!session) {
        session = session_manager_.create_session(session_id);
        if (!session) {
            send_error(from, "Server full");
            return;
        }
        session->set_endpoint(from);
        
        std::vector<uint8_t> client_key(hello->client_key, hello->client_key + KEY_LENGTH);
        std::vector<uint8_t> client_iv(hello->iv, hello->iv + IV_LENGTH);
        
        session->get_crypto().init(client_key, client_iv);
        
        if (!spawn_shell(session)) {
            session_manager_.remove_session(session_id);
            send_error(from, "Failed to spawn shell");
            return;
        }
    }

    transport_.register_session(session_id, from);
    session->set_state(SessionState::CONNECTED);
    session->set_last_activity(current_time_ms());

    send_hello_ack(session_id, from, session->get_crypto().get_key());
}

void MoshServer::handle_resume(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from) {
    if (data.size() < sizeof(ResumePacket)) {
        Logger::warn("Invalid RESUME packet size");
        return;
    }

    const ResumePacket* resume = reinterpret_cast<const ResumePacket*>(data.data());
    std::string session_id(reinterpret_cast<const char*>(resume->header.session_id), SESSION_ID_LENGTH);
    std::string session_id_hex = bytes_to_hex(std::vector<uint8_t>(
        resume->header.session_id, 
        resume->header.session_id + SESSION_ID_LENGTH
    ));

    Logger::info("RESUME request from " + from.ip + ":" + std::to_string(from.port) + ", session: " + session_id_hex.substr(0, 16));

    auto session = session_manager_.get_session(session_id);
    if (!session) {
        auto snapshot = snapshot_manager_.get_latest_snapshot(session_id);
        if (snapshot) {
            Logger::info("Restoring session from snapshot: " + session_id_hex.substr(0, 16));
            session = session_manager_.create_session(session_id);
            if (session) {
                session->get_terminal_state().rows = snapshot->terminal_rows;
                session->get_terminal_state().cols = snapshot->terminal_cols;
                session->get_terminal_state().cursor_x = snapshot->cursor_x;
                session->get_terminal_state().cursor_y = snapshot->cursor_y;
                session->set_last_snapshot_id(snapshot->snapshot_id);
                session->set_client_last_offset(snapshot->output_offset);
            }
        }
        
        if (!session) {
            Logger::warn("Resume request for unknown session: " + session_id_hex.substr(0, 16));
            send_error(from, "Session not found");
            return;
        }
    }

    transport_.update_session_endpoint(session_id, from);
    session->set_endpoint(from);
    session->set_state(SessionState::CONNECTED);
    session->set_last_activity(current_time_ms());
    
    session->set_last_seq_num(resume->last_seq_num);
    session->set_last_ack_num(resume->last_ack_num);

    Logger::info("Session resumed: " + session_id_hex.substr(0, 16) + " from new endpoint " + from.ip + ":" + std::to_string(from.port));
    send_resume_ack(session_id, from);
}

void MoshServer::handle_data(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from) {
    if (data.size() < sizeof(DataPacket)) {
        return;
    }

    std::string session_id(reinterpret_cast<const char*>(header.session_id), SESSION_ID_LENGTH);
    auto session = session_manager_.get_session(session_id);
    if (!session) {
        return;
    }

    session->set_last_activity(current_time_ms());

    const DataPacket* packet = reinterpret_cast<const DataPacket*>(data.data());
    std::vector<uint8_t> iv(packet->iv, packet->iv + IV_LENGTH);
    size_t encrypted_len = ntohs(packet->header.length) - sizeof(PacketHeader) - IV_LENGTH - AUTH_TAG_LENGTH;
    std::vector<uint8_t> encrypted_data(packet->auth_tag, packet->auth_tag + AUTH_TAG_LENGTH + encrypted_len);

    try {
        std::vector<uint8_t> plaintext = session->get_crypto().decrypt(encrypted_data, iv);
        forward_network_to_pty(session, plaintext);
    } catch (const std::exception& e) {
        Logger::warn("Decryption failed: " + std::string(e.what()));
    }

    send_ack(session_id, from, header.seq_num);
}

void MoshServer::handle_ack(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from) {
    std::string session_id(reinterpret_cast<const char*>(header.session_id), SESSION_ID_LENGTH);
    
    transport_.on_packet_ack(session_id, header.ack_num);
    
    if (data.size() >= sizeof(AckPacket)) {
        const AckPacket* ack = reinterpret_cast<const AckPacket*>(data.data());
        if (ack->rtt_estimate > 0) {
            transport_.update_rtt(session_id, ack->rtt_estimate);
        }
    }
}

void MoshServer::handle_ping(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from) {
    std::string session_id(reinterpret_cast<const char*>(header.session_id), SESSION_ID_LENGTH);
    send_pong(session_id, from);
    
    auto session = session_manager_.get_session(session_id);
    if (session) {
        session->set_last_activity(current_time_ms());
    }
}

void MoshServer::handle_fin(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from) {
    std::string session_id(reinterpret_cast<const char*>(header.session_id), SESSION_ID_LENGTH);
    
    send_fin_ack(session_id, from);
    snapshot_manager_.delete_snapshots(session_id);
    session_manager_.remove_session(session_id);
}

void MoshServer::handle_delta_request(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from) {
    if (data.size() < sizeof(DeltaRequestPacket)) {
        Logger::warn("Invalid DELTA_REQUEST packet size");
        return;
    }

    const DeltaRequestPacket* req = reinterpret_cast<const DeltaRequestPacket*>(data.data());
    std::string session_id(reinterpret_cast<const char*>(req->header.session_id), SESSION_ID_LENGTH);
    
    auto session = session_manager_.get_session(session_id);
    if (!session) {
        send_error(from, "Session not found");
        return;
    }

    uint64_t last_snapshot_id = be64toh(req->last_snapshot_id);
    uint64_t last_output_offset = be64toh(req->last_output_offset);

    Logger::debug("Delta request: last_snapshot=" + std::to_string(last_snapshot_id) + 
                  ", last_offset=" + std::to_string(last_output_offset));

    DeltaSyncRequest sync_req;
    sync_req.session_id = session_id;
    sync_req.last_snapshot_id = last_snapshot_id;
    sync_req.last_output_offset = last_output_offset;

    DeltaSyncResponse response = snapshot_manager_.compute_delta(sync_req, session->get_output_buffer());
    
    if (response.full_sync_required) {
        auto snapshot = snapshot_manager_.get_latest_snapshot(session_id);
        if (snapshot) {
            response.snapshot_data = snapshot->output_data;
        }
    }

    session->set_client_last_offset(response.current_output_offset);
    send_delta_response(session_id, from, response);
}

void MoshServer::send_hello_ack(const std::string& session_id, const NetworkEndpoint& endpoint, const std::vector<uint8_t>& key) {
    std::vector<uint8_t> packet(sizeof(HelloPacket));
    HelloPacket* hello_ack = reinterpret_cast<HelloPacket*>(packet.data());
    
    hello_ack->header.type = PacketType::HELLO_ACK;
    hello_ack->header.flags = 0;
    hello_ack->header.length = htons(sizeof(HelloPacket));
    hello_ack->header.seq_num = htonl(1);
    hello_ack->header.ack_num = htonl(0);
    hello_ack->header.timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(hello_ack->header.session_id, session_id.data(), SESSION_ID_LENGTH);
    
    std::memcpy(hello_ack->client_key, key.data(), std::min<size_t>(key.size(), KEY_LENGTH));
    std::memcpy(hello_ack->iv, generate_random_bytes(IV_LENGTH).data(), IV_LENGTH);
    hello_ack->protocol_version = htonl(0x01000000);

    transport_.send_packet(endpoint, packet);
}

void MoshServer::send_resume_ack(const std::string& session_id, const NetworkEndpoint& endpoint) {
    std::vector<uint8_t> packet(sizeof(PacketHeader));
    PacketHeader* header = reinterpret_cast<PacketHeader*>(packet.data());
    
    header->type = PacketType::RESUME_ACK;
    header->flags = 0;
    header->length = htons(sizeof(PacketHeader));
    header->seq_num = htonl(1);
    header->ack_num = htonl(0);
    header->timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(header->session_id, session_id.data(), SESSION_ID_LENGTH);

    transport_.send_packet(endpoint, packet);
}

void MoshServer::send_pong(const std::string& session_id, const NetworkEndpoint& endpoint) {
    std::vector<uint8_t> packet(sizeof(PacketHeader));
    PacketHeader* header = reinterpret_cast<PacketHeader*>(packet.data());
    
    header->type = PacketType::PONG;
    header->flags = 0;
    header->length = htons(sizeof(PacketHeader));
    header->seq_num = htonl(0);
    header->ack_num = htonl(0);
    header->timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(header->session_id, session_id.data(), SESSION_ID_LENGTH);

    transport_.send_packet(endpoint, packet);
}

void MoshServer::send_fin_ack(const std::string& session_id, const NetworkEndpoint& endpoint) {
    std::vector<uint8_t> packet(sizeof(PacketHeader));
    PacketHeader* header = reinterpret_cast<PacketHeader*>(packet.data());
    
    header->type = PacketType::FIN_ACK;
    header->flags = 0;
    header->length = htons(sizeof(PacketHeader));
    header->seq_num = htonl(0);
    header->ack_num = htonl(0);
    header->timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(header->session_id, session_id.data(), SESSION_ID_LENGTH);

    transport_.send_packet(endpoint, packet);
}

void MoshServer::send_error(const NetworkEndpoint& endpoint, const std::string& message) {
    std::vector<uint8_t> packet(sizeof(PacketHeader) + message.size() + 1);
    PacketHeader* header = reinterpret_cast<PacketHeader*>(packet.data());
    
    header->type = PacketType::ERROR;
    header->flags = 0;
    header->length = htons(static_cast<uint16_t>(packet.size()));
    header->seq_num = htonl(0);
    header->ack_num = htonl(0);
    header->timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memset(header->session_id, 0, SESSION_ID_LENGTH);
    
    std::memcpy(packet.data() + sizeof(PacketHeader), message.c_str(), message.size() + 1);

    transport_.send_packet(endpoint, packet);
}

void MoshServer::send_delta_response(const std::string& session_id, const NetworkEndpoint& endpoint, 
                                      const DeltaSyncResponse& response) {
    size_t total_size = sizeof(DeltaResponsePacket) + response.delta_data.size() + response.snapshot_data.size();
    std::vector<uint8_t> packet(total_size);
    
    DeltaResponsePacket* resp = reinterpret_cast<DeltaResponsePacket*>(packet.data());
    
    resp->header.type = PacketType::DELTA_RESPONSE;
    resp->header.flags = 0;
    resp->header.length = htons(static_cast<uint16_t>(total_size));
    resp->header.seq_num = htonl(0);
    resp->header.ack_num = htonl(0);
    resp->header.timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(resp->header.session_id, session_id.data(), SESSION_ID_LENGTH);
    
    resp->current_snapshot_id = htobe64(response.current_snapshot_id);
    resp->current_output_offset = htobe64(response.current_output_offset);
    resp->has_delta = response.has_delta ? 1 : 0;
    resp->full_sync_required = response.full_sync_required ? 1 : 0;
    resp->delta_length = htobe32(static_cast<uint32_t>(response.delta_data.size()));
    resp->snapshot_length = htobe32(static_cast<uint32_t>(response.snapshot_data.size()));
    
    size_t offset = sizeof(DeltaResponsePacket);
    if (!response.delta_data.empty()) {
        std::memcpy(packet.data() + offset, response.delta_data.data(), response.delta_data.size());
        offset += response.delta_data.size();
    }
    if (!response.snapshot_data.empty()) {
        std::memcpy(packet.data() + offset, response.snapshot_data.data(), response.snapshot_data.size());
    }

    Logger::debug("Sending delta response: " + std::to_string(response.delta_data.size()) + 
                  " bytes delta, " + std::to_string(response.snapshot_data.size()) + " bytes snapshot");
    
    transport_.send_packet(endpoint, packet);
}

void MoshServer::send_ack(const std::string& session_id, const NetworkEndpoint& endpoint, uint32_t seq_num) {
    std::vector<uint8_t> packet(sizeof(AckPacket));
    AckPacket* ack = reinterpret_cast<AckPacket*>(packet.data());
    
    ack->header.type = PacketType::ACK;
    ack->header.flags = 0;
    ack->header.length = htons(sizeof(AckPacket));
    ack->header.seq_num = htonl(0);
    ack->header.ack_num = htonl(seq_num);
    ack->header.timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(ack->header.session_id, session_id.data(), SESSION_ID_LENGTH);
    
    ack->rtt_estimate = htonl(transport_.get_rtt(session_id));
    ack->window_size = htonl(transport_.get_cwnd(session_id));

    transport_.send_packet(endpoint, packet);
}

void MoshServer::cleanup_loop() {
    while (running_) {
        sleep_ms(30000);
        session_manager_.cleanup_inactive_sessions(session_timeout_);
        snapshot_manager_.cleanup_old_snapshots(session_timeout_ / 1000);
    }
}

void MoshServer::snapshot_loop() {
    while (running_) {
        sleep_ms(snapshot_interval_);
        
        auto session_ids = session_manager_.get_all_session_ids();
        for (const auto& session_id : session_ids) {
            auto session = session_manager_.get_session(session_id);
            if (session && session->get_state() == SessionState::CONNECTED) {
                auto& ts = session->get_terminal_state();
                snapshot_manager_.create_snapshot(
                    session_id,
                    session->get_output_buffer(),
                    ts.rows,
                    ts.cols,
                    ts.cursor_x,
                    ts.cursor_y,
                    session->get_tmux_session()
                );
                session->set_last_snapshot_id(session->get_last_snapshot_id() + 1);
            }
        }
    }
}

void MoshServer::forward_network_to_pty(std::shared_ptr<Session> session, const std::vector<uint8_t>& data) {
    int pty_fd = session->get_pty_fd();
    if (pty_fd < 0) {
        return;
    }
    
    ssize_t written = write(pty_fd, data.data(), data.size());
    if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        Logger::warn("Failed to write to PTY: " + std::string(strerror(errno)));
    }
}

bool MoshServer::spawn_shell(std::shared_ptr<Session> session) {
    PTY* pty = new PTY();
    if (!pty->open()) {
        delete pty;
        return false;
    }

    const char* shell = getenv("SHELL");
    if (!shell) {
        shell = "/bin/bash";
    }

    if (!pty->spawn_shell(shell)) {
        delete pty;
        return false;
    }

    session->set_pty_fd(pty->get_master_fd());
    
    std::thread([this, session, pty]() {
        std::vector<uint8_t> buffer(4096);
        while (session->get_state() != SessionState::CLOSED) {
            ssize_t n = pty->read(buffer.data(), buffer.size());
            if (n > 0) {
                std::vector<uint8_t> data(buffer.begin(), buffer.begin() + n);
                session->append_output(data);
                
                std::vector<uint8_t> encrypted = session->get_crypto().encrypt(data);
                size_t max_data_size = MAX_PACKET_SIZE - sizeof(DataPacket);
                
                for (size_t offset = 0; offset < encrypted.size(); offset += max_data_size) {
                    size_t chunk_size = std::min(max_data_size, encrypted.size() - offset);
                    
                    std::vector<uint8_t> packet(sizeof(DataPacket) + chunk_size - (MAX_PACKET_SIZE - sizeof(DataPacket) - IV_LENGTH - AUTH_TAG_LENGTH));
                    DataPacket* pkt = reinterpret_cast<DataPacket*>(packet.data());
                    
                    pkt->header.type = PacketType::DATA;
                    pkt->header.flags = 0;
                    pkt->header.length = htons(static_cast<uint16_t>(sizeof(PacketHeader) + IV_LENGTH + AUTH_TAG_LENGTH + chunk_size));
                    pkt->header.seq_num = htonl(session->get_next_seq_num());
                    pkt->header.ack_num = htonl(0);
                    pkt->header.timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
                    std::memcpy(pkt->header.session_id, session->get_id().data(), SESSION_ID_LENGTH);
                    
                    std::memcpy(pkt->iv, session->get_crypto().get_iv().data(), IV_LENGTH);
                    std::memcpy(pkt->auth_tag, encrypted.data() + offset, AUTH_TAG_LENGTH);
                    if (chunk_size > AUTH_TAG_LENGTH) {
                        std::memcpy(pkt->encrypted_data, encrypted.data() + offset + AUTH_TAG_LENGTH, chunk_size - AUTH_TAG_LENGTH);
                    }
                    
                    transport_.send_data(session->get_id(), packet);
                }
            } else if (n == 0) {
                break;
            } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }
            sleep_ms(10);
        }
        delete pty;
    }).detach();

    return true;
}

void MoshServer::attach_to_tmux(std::shared_ptr<Session> session) {
}

void MoshServer::create_tmux_session(std::shared_ptr<Session> session) {
}

void MoshServer::handle_roaming_join(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from) {
    if (!roaming_enabled_) {
        send_error(from, "Roaming not enabled");
        return;
    }

    if (data.size() < sizeof(RoamingJoinPacket)) {
        Logger::warn("Invalid ROAMING_JOIN packet size");
        return;
    }

    const RoamingJoinPacket* join = reinterpret_cast<const RoamingJoinPacket*>(data.data());
    std::string session_id(reinterpret_cast<const char*>(join->header.session_id), SESSION_ID_LENGTH);
    std::string device_id(reinterpret_cast<const char*>(join->device_id), 32);
    std::string user_id(reinterpret_cast<const char*>(join->user_id), 64);
    std::string device_name(reinterpret_cast<const char*>(join->device_name), 64);
    SessionAccessMode access_mode = static_cast<SessionAccessMode>(join->access_mode);

    device_id = device_id.substr(0, device_id.find('\0'));
    user_id = user_id.substr(0, user_id.find('\0'));
    device_name = device_name.substr(0, device_name.find('\0'));

    Logger::info("ROAMING_JOIN from " + from.ip + ":" + std::to_string(from.port) + 
                 ", device: " + device_name + ", mode: " + std::to_string(static_cast<int>(access_mode)));

    auto session = session_manager_.get_session(session_id);
    if (!session) {
        send_error(from, "Session not found");
        return;
    }

    if (permission_manager_ && !permission_manager_->check_permission(session_id, user_id, "attach")) {
        send_error(from, "Permission denied");
        return;
    }

    DeviceInfo device;
    device.device_id = device_id;
    device.device_name = device_name;
    device.device_type = "unknown";
    device.ip_address = from.ip;
    device.connected_at = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    device.last_active = device.connected_at;
    device.is_active = true;
    device.is_readonly = (access_mode == SessionAccessMode::EXCLUSIVE);

    if (roaming_manager_) {
        if (!roaming_manager_->can_accept_new_device(session_id, user_id, access_mode)) {
            send_error(from, "Cannot accept new device. Session may be full or access denied.");
            return;
        }
        roaming_manager_->handle_device_join(session_id, device, user_id, access_mode);
    }

    session_device_ids_[session_id] = device_id;
    session_user_ids_[session_id] = user_id;

    transport_.register_session(session_id, from);
    session->set_endpoint(from);
    session->set_state(SessionState::CONNECTED);
    session->set_last_activity(current_time_ms());

    auto state = roaming_manager_ ? roaming_manager_->get_current_state(session_id) : ShellState{};
    terminal_sync_manager_->update_current_directory(state.current_directory);
    terminal_sync_manager_->update_terminal_size(state.terminal_cols, state.terminal_rows);
    terminal_sync_manager_->update_cursor(state.state_cursor_x, state.state_cursor_y);

    std::vector<uint8_t> response(sizeof(PacketHeader));
    PacketHeader* resp = reinterpret_cast<PacketHeader*>(response.data());
    resp->type = PacketType::ROAMING_JOIN;
    resp->flags = device.is_readonly ? 1 : 0;
    resp->length = htons(sizeof(PacketHeader));
    resp->seq_num = htonl(1);
    resp->ack_num = htonl(0);
    resp->timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(resp->session_id, session_id.data(), SESSION_ID_LENGTH);

    transport_.send_packet(from, response);

    auto devices = roaming_manager_ ? roaming_manager_->get_active_device_count(session_id) : 0;
    Logger::info("Device joined: " + device_name + ", active devices: " + std::to_string(devices));
}

void MoshServer::handle_roaming_leave(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from) {
    if (!roaming_enabled_) {
        return;
    }

    if (data.size() < sizeof(RoamingLeavePacket)) {
        Logger::warn("Invalid ROAMING_LEAVE packet size");
        return;
    }

    const RoamingLeavePacket* leave = reinterpret_cast<const RoamingLeavePacket*>(data.data());
    std::string session_id(reinterpret_cast<const char*>(leave->header.session_id), SESSION_ID_LENGTH);
    std::string device_id(reinterpret_cast<const char*>(leave->device_id), 32);
    
    device_id = device_id.substr(0, device_id.find('\0'));

    Logger::info("ROAMING_LEAVE from " + from.ip + ":" + std::to_string(from.port) + 
                 ", device: " + device_id);

    if (roaming_manager_) {
        roaming_manager_->handle_device_leave(session_id, device_id);
    }

    session_device_ids_.erase(session_id);
    session_user_ids_.erase(session_id);

    auto devices = roaming_manager_ ? roaming_manager_->get_active_device_count(session_id) : 0;
    Logger::info("Device left: " + device_id + ", active devices: " + std::to_string(devices));
}

void MoshServer::handle_roaming_sync(const PacketHeader& header, const std::vector<uint8_t>& data, const NetworkEndpoint& from) {
    if (!roaming_enabled_) {
        return;
    }

    if (data.size() < sizeof(RoamingSyncPacket)) {
        Logger::warn("Invalid ROAMING_SYNC packet size");
        return;
    }

    const RoamingSyncPacket* sync = reinterpret_cast<const RoamingSyncPacket*>(data.data());
    std::string session_id(reinterpret_cast<const char*>(sync->header.session_id), SESSION_ID_LENGTH);
    std::string device_id(reinterpret_cast<const char*>(sync->device_id), 32);
    
    device_id = device_id.substr(0, device_id.find('\0'));

    uint64_t base_version = be64toh(sync->base_version);
    uint64_t target_version = be64toh(sync->target_version);
    uint32_t op_count = ntohl(sync->operation_count);

    auto session = session_manager_.get_session(session_id);
    if (!session) {
        return;
    }

    session->set_last_activity(current_time_ms());

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

    handle_roaming_sync(session_id);
}

void MoshServer::handle_roaming_sync(const std::string& session_id) {
    if (!roaming_enabled_ || !crdt_manager_) {
        return;
    }

    auto devices = session_store_ ? session_store_->get_devices(session_id) : std::vector<DeviceInfo>();
    
    for (const auto& device : devices) {
        if (!device.is_active) continue;

        CRDTSyncPacket sync_packet = crdt_manager_->create_sync_packet(device.device_id);
        std::string serialized = sync_packet.serialize();

        size_t total_size = sizeof(RoamingSyncPacket) + serialized.size();
        std::vector<uint8_t> packet(total_size);
        
        RoamingSyncPacket* resp = reinterpret_cast<RoamingSyncPacket*>(packet.data());
        resp->header.type = PacketType::ROAMING_SYNC;
        resp->header.flags = 0;
        resp->header.length = htons(static_cast<uint16_t>(total_size));
        resp->header.seq_num = htonl(0);
        resp->header.ack_num = htonl(0);
        resp->header.timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
        std::memcpy(resp->header.session_id, session_id.data(), SESSION_ID_LENGTH);
        
        std::memcpy(resp->device_id, device.device_id.c_str(), std::min<size_t>(device.device_id.size(), 32));
        resp->base_version = htobe64(sync_packet.base_version);
        resp->target_version = htobe64(sync_packet.target_version);
        resp->operation_count = htonl(sync_packet.operations.size());
        
        std::memcpy(resp->operations, serialized.data(), serialized.size());

        auto session = session_manager_.get_session(session_id);
        if (session) {
            transport_.send_data(session_id, packet);
        }
    }
}

void MoshServer::sync_session_state(const std::string& session_id) {
    if (!roaming_enabled_ || !terminal_sync_manager_) {
        return;
    }

    auto session = session_manager_.get_session(session_id);
    if (!session) return;

    auto& ts = session->get_terminal_state();
    terminal_sync_manager_->update_cursor(ts.cursor_x, ts.cursor_y);
    terminal_sync_manager_->update_terminal_size(ts.cols, ts.rows);

    auto state = terminal_sync_manager_->get_sync_state();
    
    if (roaming_manager_) {
        ShellState shell_state;
        shell_state.current_directory = state.current_directory;
        shell_state.terminal_rows = state.terminal_height;
        shell_state.terminal_cols = state.terminal_width;
        shell_state.state_cursor_x = state.cursor_x;
        shell_state.state_cursor_y = state.cursor_y;
        roaming_manager_->handle_state_update(session_id, shell_state);
    }

    auto devices = session_store_ ? session_store_->get_devices(session_id) : std::vector<DeviceInfo>();
    
    for (const auto& device : devices) {
        if (!device.is_active) continue;

        std::string serialized = state.serialize();
        size_t total_size = sizeof(RoamingStatePacket) + serialized.size();
        std::vector<uint8_t> packet(total_size);
        
        RoamingStatePacket* resp = reinterpret_cast<RoamingStatePacket*>(packet.data());
        resp->header.type = PacketType::ROAMING_STATE;
        resp->header.flags = 0;
        resp->header.length = htons(static_cast<uint16_t>(total_size));
        resp->header.seq_num = htonl(0);
        resp->header.ack_num = htonl(0);
        resp->header.timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
        std::memcpy(resp->header.session_id, session_id.data(), SESSION_ID_LENGTH);
        
        std::memcpy(resp->device_id, device.device_id.c_str(), std::min<size_t>(device.device_id.size(), 32));
        resp->cursor_x = htonl(state.cursor_x);
        resp->cursor_y = htonl(state.cursor_y);
        resp->terminal_width = htonl(state.terminal_width);
        resp->terminal_height = htonl(state.terminal_height);
        resp->directory_length = htonl(state.current_directory.size());
        std::memcpy(resp->current_directory, state.current_directory.data(), state.current_directory.size());

        transport_.send_data(session_id, packet);
    }
}

void MoshServer::send_roaming_notification(const std::string& session_id, const std::string& device_id, const std::string& message) {
    auto session = session_manager_.get_session(session_id);
    if (!session) return;

    size_t total_size = sizeof(RoamingNotificationPacket) + message.size();
    std::vector<uint8_t> packet(total_size);
    
    RoamingNotificationPacket* notif = reinterpret_cast<RoamingNotificationPacket*>(packet.data());
    notif->header.type = PacketType::ROAMING_NOTIFICATION;
    notif->header.flags = 0;
    notif->header.length = htons(static_cast<uint16_t>(total_size));
    notif->header.seq_num = htonl(0);
    notif->header.ack_num = htonl(0);
    notif->header.timestamp = htonl(static_cast<uint32_t>(current_time_ms()));
    std::memcpy(notif->header.session_id, session_id.data(), SESSION_ID_LENGTH);
    
    std::memcpy(notif->device_id, device_id.c_str(), std::min<size_t>(device_id.size(), 32));
    notif->notification_type = 0;
    notif->message_length = htonl(message.size());
    std::memcpy(notif->message, message.data(), message.size());

    transport_.send_data(session_id, packet);
}

bool MoshServer::list_sessions(std::vector<std::string>& sessions) {
    if (!roaming_enabled_ || !session_store_) {
        return false;
    }
    
    auto user_sessions = session_store_->get_user_sessions("");
    sessions = user_sessions;
    return true;
}

bool MoshServer::replay_session(const std::string& session_id) {
    if (!roaming_enabled_) {
        return false;
    }
    
    Logger::info("Replaying session: " + session_id);
    
    auto snapshot = snapshot_manager_.get_latest_snapshot(session_id);
    if (!snapshot) {
        Logger::warn("No snapshot found for session: " + session_id);
        return false;
    }
    
    Logger::info("Session replay complete. Output size: " + std::to_string(snapshot->output_data.size()));
    return true;
}

}
