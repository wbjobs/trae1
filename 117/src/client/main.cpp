#include <iostream>
#include <string>
#include <csignal>
#include "client/mosh_client.h"
#include "common/utils.h"
#include "common/auth.h"

using namespace moshpp;

MoshClient* g_client = nullptr;

void signal_handler(int signal) {
    Logger::info("Received signal " + std::to_string(signal) + ", disconnecting...");
    if (g_client) {
        g_client->disconnect();
    }
}

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [OPTIONS] HOST" << std::endl;
    std::cout << "Mosh++ Client - Enhanced SSH session manager for mobile environments" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p, --port PORT        Server port (default: 60001)" << std::endl;
    std::cout << "      --new-session      Create a new session (default)" << std::endl;
    std::cout << "      --attach SESSION_ID Attach to an existing session" << std::endl;
    std::cout << "      --device-id ID     Device identifier for multi-device roaming" << std::endl;
    std::cout << "      --device-name NAME  Device name (e.g., 'My Phone', 'Work Laptop')" << std::endl;
    std::cout << "      --user-id ID        User identifier for authentication" << std::endl;
    std::cout << "      --steal             Force takeover of session (default)" << std::endl;
    std::cout << "      --shared            Join session in shared mode" << std::endl;
    std::cout << "      --readonly          Join session in read-only mode" << std::endl;
    std::cout << "  -v, --verbose          Enable verbose logging" << std::endl;
    std::cout << "  -h, --help             Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << prog_name << " --new-session --device-name 'My Phone' server.example.com" << std::endl;
    std::cout << "  " << prog_name << " --attach abc123def456 --device-name 'Work Laptop' server.example.com" << std::endl;
    std::cout << "  " << prog_name << " --attach abc123def456 --shared --device-name 'Tablet' server.example.com" << std::endl;
    std::cout << "  " << prog_name << " -p 60002 server.example.com" << std::endl;
    std::cout << std::endl;
    std::cout << "Features:" << std::endl;
    std::cout << "  - UDP-based transport for mobile networks" << std::endl;
    std::cout << "  - Seamless roaming (WiFi <-> 4G/5G)" << std::endl;
    std::cout << "  - AES-256-GCM encryption + OTP" << std::endl;
    std::cout << "  - RTT-based congestion control" << std::endl;
    std::cout << "  - Auto-reconnect on network changes" << std::endl;
    std::cout << "  - Multi-device session roaming" << std::endl;
    std::cout << "  - Session steal/shared/readonly modes" << std::endl;
}

int main(int argc, char* argv[]) {
    uint16_t port = DEFAULT_PORT;
    bool new_session = true;
    std::string session_id;
    bool verbose = false;
    std::string host;
    
    std::string device_id;
    std::string device_name;
    std::string user_id;
    SessionAccessMode access_mode = SessionAccessMode::STEAL;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = static_cast<uint16_t>(std::stoi(argv[++i]));
            } else {
                std::cerr << "Error: --port requires a value" << std::endl;
                return 1;
            }
        } else if (arg == "--new-session") {
            new_session = true;
        } else if (arg == "--attach") {
            new_session = false;
            if (i + 1 < argc) {
                session_id = argv[++i];
            } else {
                std::cerr << "Error: --attach requires a session ID" << std::endl;
                return 1;
            }
        } else if (arg == "--device-id") {
            if (i + 1 < argc) {
                device_id = argv[++i];
            } else {
                std::cerr << "Error: --device-id requires a value" << std::endl;
                return 1;
            }
        } else if (arg == "--device-name") {
            if (i + 1 < argc) {
                device_name = argv[++i];
            } else {
                std::cerr << "Error: --device-name requires a value" << std::endl;
                return 1;
            }
        } else if (arg == "--user-id") {
            if (i + 1 < argc) {
                user_id = argv[++i];
            } else {
                std::cerr << "Error: --user-id requires a value" << std::endl;
                return 1;
            }
        } else if (arg == "--steal") {
            access_mode = SessionAccessMode::STEAL;
        } else if (arg == "--shared") {
            access_mode = SessionAccessMode::SHARED;
        } else if (arg == "--readonly") {
            access_mode = SessionAccessMode::EXCLUSIVE;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg[0] != '-') {
            host = arg;
        }
    }

    if (host.empty()) {
        std::cerr << "Error: HOST is required" << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    if (verbose) {
        Logger::set_level(Logger::Level::DEBUG);
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    MoshClient client;
    g_client = &client;
    
    if (!device_id.empty()) {
        client.set_device_id(device_id);
    }
    if (!device_name.empty()) {
        client.set_device_name(device_name);
    }
    if (!user_id.empty()) {
        client.set_user_id(user_id);
    }
    client.set_access_mode(access_mode);

    bool success = false;
    if (new_session) {
        success = client.new_session(host, port);
        if (success) {
            std::cout << "Session ID: " << bytes_to_hex(std::vector<uint8_t>(
                reinterpret_cast<const uint8_t*>(client.get_session_id().data()),
                reinterpret_cast<const uint8_t*>(client.get_session_id().data()) + client.get_session_id().size()
            )) << std::endl;
            std::cout << "Save this session ID to reconnect later!" << std::endl;
            sleep_ms(1000);
        }
    } else {
        success = client.attach_session(host, port, session_id);
    }

    if (!success) {
        std::cerr << "Failed to connect to server" << std::endl;
        return 1;
    }

    client.run();
    return 0;
}
