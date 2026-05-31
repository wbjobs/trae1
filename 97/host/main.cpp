#include <iostream>
#include <string>
#include <cstdlib>
#include <signal.h>
#include <atomic>

#include "enclave_host.h"
#include "session_manager.h"
#include "server.h"
#include "protocol.h"

namespace {
std::atomic<bool> g_running{true};
void on_signal(int) { g_running = false; }
}

int main(int argc, char** argv) {
    std::string enclave_path = ENCLAVE_SIGNED_PATH;
    std::string listen_host = "0.0.0.0";
    uint16_t port = 7788;
    uint32_t session_ttl = sgxagg::kDefaultSessionTTL;
    int num_threads = 4;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--enclave" && i + 1 < argc) { enclave_path = argv[++i]; }
        else if (arg == "--host" && i + 1 < argc) { listen_host = argv[++i]; }
        else if (arg == "--port" && i + 1 < argc) { port = (uint16_t)atoi(argv[++i]); }
        else if (arg == "--session-ttl" && i + 1 < argc) { session_ttl = (uint32_t)atoi(argv[++i]); }
        else if (arg == "--threads" && i + 1 < argc) { num_threads = atoi(argv[++i]); }
        else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  --enclave <path>      Path to signed enclave\n"
                      << "  --host <ip>           Listen IP (default 0.0.0.0)\n"
                      << "  --port <port>         Listen port (default 7788)\n"
                      << "  --session-ttl <sec>   Session TTL in seconds (default 86400 = 24h)\n"
                      << "  --threads <n>         Worker thread count (default 4)\n";
            return 0;
        } else {
            std::cerr << "Unknown arg: " << arg << std::endl;
            return 1;
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    std::cout << "[host] Loading enclave: " << enclave_path << std::endl;
    if (!sgxagg::host::EnclaveHost::instance().create(enclave_path)) {
        std::cerr << "Failed to create enclave" << std::endl;
        return 1;
    }

    sgxagg::host::SessionManager::instance().set_default_ttl(session_ttl);
    sgxagg::host::SessionManager::instance().start_reaper();

    sgxagg::host::Server server(listen_host, port);
    if (!server.start(num_threads)) {
        std::cerr << "Failed to start server" << std::endl;
        sgxagg::host::EnclaveHost::instance().destroy();
        return 1;
    }

    std::cout << "[host] Listening on " << listen_host << ":" << port
              << " with session TTL=" << session_ttl << "s, max sessions="
              << sgxagg::kMaxSessions << std::endl;

    while (g_running) {
        usleep(200000);  // 200ms
        static int cnt = 0;
        if (++cnt % 5 == 0) {
            std::cout << "[host] sessions=" << sgxagg::host::SessionManager::instance().size() << "\r" << std::flush;
        }
    }

    std::cout << "\n[host] Shutting down..." << std::endl;
    server.stop();
    sgxagg::host::SessionManager::instance().stop_reaper();
    sgxagg::host::EnclaveHost::instance().destroy();
    std::cout << "[host] Done." << std::endl;
    return 0;
}
