#include <iostream>
#include <string>
#include <csignal>
#include "server/mosh_server.h"
#include "common/snapshot.h"
#include "common/utils.h"

using namespace moshpp;

MoshServer* g_server = nullptr;

void signal_handler(int signal) {
    Logger::info("Received signal " + std::to_string(signal) + ", shutting down...");
    if (g_server) {
        g_server->stop();
    }
}

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [OPTIONS]" << std::endl;
    std::cout << "Mosh++ Server - Enhanced SSH session manager for mobile environments" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p, --port PORT        Listen on port (default: 60001)" << std::endl;
    std::cout << "  -t, --timeout SEC      Session timeout in seconds (default: 3600)" << std::endl;
    std::cout << "      --enable-roaming   Enable multi-device session roaming" << std::endl;
    std::cout << "      --redis-host HOST  Redis host (default: localhost)" << std::endl;
    std::cout << "      --redis-port PORT  Redis port (default: 6379)" << std::endl;
    std::cout << "  -v, --verbose          Enable verbose logging" << std::endl;
    std::cout << "  -h, --help             Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Session Replay:" << std::endl;
    std::cout << "      --replay SESSION_ID  Replay a session's output" << std::endl;
    std::cout << "      --list-sessions      List all recorded sessions" << std::endl;
    std::cout << "      --snapshot-dir DIR   Directory for snapshots (default: ./mosh_snapshots)" << std::endl;
    std::cout << std::endl;
    std::cout << "Features:" << std::endl;
    std::cout << "  - UDP-based transport for mobile networks" << std::endl;
    std::cout << "  - Seamless roaming (WiFi <-> 4G/5G)" << std::endl;
    std::cout << "  - AES-256-GCM encryption + OTP" << std::endl;
    std::cout << "  - RTT-based congestion control" << std::endl;
    std::cout << "  - tmux/screen integration" << std::endl;
    std::cout << "  - Supports 1000+ concurrent sessions" << std::endl;
    std::cout << "  - Session persistence with 1MB circular buffer" << std::endl;
    std::cout << "  - Delta sync on reconnection" << std::endl;
    std::cout << "  - Session replay for auditing" << std::endl;
    std::cout << "  - Multi-device session roaming" << std::endl;
    std::cout << "  - Session steal/shared/readonly modes" << std::endl;
}

void list_sessions(const std::string& snapshot_dir) {
    SnapshotManager manager(snapshot_dir);
    
    std::string cmd = "ls -1 " + snapshot_dir + " 2>/dev/null";
    try {
        std::string output = exec_command(cmd);
        std::vector<std::string> sessions = split_string(output, '\n');
        
        std::cout << "Available sessions:" << std::endl;
        std::cout << "-------------------" << std::endl;
        
        int count = 0;
        for (const auto& session : sessions) {
            if (session.empty()) continue;
            if (session.length() == 32) {
                std::cout << "  " << session << std::endl;
                count++;
            }
        }
        
        if (count == 0) {
            std::cout << "  No sessions found" << std::endl;
        } else {
            std::cout << std::endl << "Total: " << count << " session(s)" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error listing sessions: " << e.what() << std::endl;
    }
}

void replay_session(const std::string& session_id, const std::string& snapshot_dir) {
    std::cout << "Replaying session: " << session_id << std::endl;
    std::cout << "====================" << std::endl << std::endl;
    
    SnapshotManager manager(snapshot_dir);
    ReplaySession replay(session_id, manager);
    
    if (!replay.load_snapshots()) {
        std::cerr << "Failed to load session snapshots. Session may not exist or is empty." << std::endl;
        return;
    }
    
    std::cout << "Loaded " << replay.get_snapshot_count() << " snapshots" << std::endl;
    
    uint64_t duration = replay.get_duration() / 1000;
    std::cout << "Session duration: " << duration << " seconds" << std::endl;
    std::cout << "-------------------" << std::endl << std::endl;
    
    replay.play([](const std::vector<uint8_t>& data) {
        write(STDOUT_FILENO, data.data(), data.size());
    }, 1.0f);
    
    std::cout << std::endl << "-------------------" << std::endl;
    std::cout << "Replay complete." << std::endl;
}

int main(int argc, char* argv[]) {
    uint16_t port = DEFAULT_PORT;
    uint64_t timeout = 3600;
    bool verbose = false;
    std::string snapshot_dir = "./mosh_snapshots";
    bool replay_mode = false;
    std::string replay_session_id;
    bool list_mode = false;
    bool enable_roaming = false;
    std::string redis_host = "localhost";
    int redis_port = 6379;

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
        } else if (arg == "-t" || arg == "--timeout") {
            if (i + 1 < argc) {
                timeout = std::stoull(argv[++i]);
            } else {
                std::cerr << "Error: --timeout requires a value" << std::endl;
                return 1;
            }
        } else if (arg == "--enable-roaming") {
            enable_roaming = true;
        } else if (arg == "--redis-host") {
            if (i + 1 < argc) {
                redis_host = argv[++i];
            } else {
                std::cerr << "Error: --redis-host requires a value" << std::endl;
                return 1;
            }
        } else if (arg == "--redis-port") {
            if (i + 1 < argc) {
                redis_port = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: --redis-port requires a value" << std::endl;
                return 1;
            }
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "--replay") {
            replay_mode = true;
            if (i + 1 < argc) {
                replay_session_id = argv[++i];
            } else {
                std::cerr << "Error: --replay requires a session ID" << std::endl;
                return 1;
            }
        } else if (arg == "--list-sessions") {
            list_mode = true;
        } else if (arg == "--snapshot-dir") {
            if (i + 1 < argc) {
                snapshot_dir = argv[++i];
            } else {
                std::cerr << "Error: --snapshot-dir requires a directory path" << std::endl;
                return 1;
            }
        }
    }

    if (verbose) {
        Logger::set_level(Logger::Level::DEBUG);
    }

    if (list_mode) {
        list_sessions(snapshot_dir);
        return 0;
    }

    if (replay_mode) {
        replay_session(replay_session_id, snapshot_dir);
        return 0;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    MoshServer server;
    g_server = &server;
    
    server.set_session_timeout(timeout * 1000);
    server.set_snapshot_dir(snapshot_dir);
    
    if (enable_roaming) {
        server.set_redis_host(redis_host);
        server.set_redis_port(redis_port);
        server.enable_roaming();
    }
    
    if (!server.start(port)) {
        std::cerr << "Failed to start server on port " << port << std::endl;
        return 1;
    }

    server.wait();
    return 0;
}
