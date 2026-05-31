#include "service/memory_service_impl.h"
#include "monitor/monitor.h"
#include "common/config.h"
#include "common/utils.h"
#include <grpc++/grpc++.h>
#include <iostream>
#include <memory>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>

using namespace dmp;

static std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    DMP_INFO("Received signal {}, shutting down...", signal);
    g_running.store(false, std::memory_order_release);
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  --config <path>    Path to configuration file\n"
              << "  --node_id <id>     Node ID (overrides config)\n"
              << "  --port <port>      gRPC port (default: 50051)\n"
              << "  --rdma_device <dev> RDMA device name (default: mlx5_0)\n"
              << "  --rdma_port <port>  RDMA port (default: 1)\n"
              << "  --memory <size>     Total memory size (default: 64G)\n"
              << "  --hugepages         Use huge pages\n"
              << "  --log_level <level> Log level: DEBUG, INFO, WARN, ERROR\n"
              << "  --help              Show this help\n";
}

Config parse_args(int argc, char* argv[]) {
    Config config;
    config.node_id = 1;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--config" && i + 1 < argc) {
            config = Config::load_from_file(argv[++i]);
        } else if (arg == "--node_id" && i + 1 < argc) {
            config.node_id = std::stoull(argv[++i]);
        } else if (arg == "--port" && i + 1 < argc) {
            config.grpc_port = std::stoul(argv[++i]);
        } else if (arg == "--rdma_device" && i + 1 < argc) {
            config.rdma_device = argv[++i];
        } else if (arg == "--rdma_port" && i + 1 < argc) {
            config.rdma_port = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if (arg == "--memory" && i + 1 < argc) {
            config.total_memory = Config::parse_size(argv[++i]);
        } else if (arg == "--hugepages") {
            config.use_hugepages = true;
        } else if (arg == "--log_level" && i + 1 < argc) {
            std::string level = argv[++i];
            if (level == "DEBUG") {
                Logger::instance().set_level(Logger::Level::DEBUG);
            } else if (level == "INFO") {
                Logger::instance().set_level(Logger::Level::INFO);
            } else if (level == "WARN") {
                Logger::instance().set_level(Logger::Level::WARN);
            } else if (level == "ERROR") {
                Logger::instance().set_level(Logger::Level::ERROR);
            }
        } else if (arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    return config;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    Config config = parse_args(argc, argv);

    if (config.node_id == INVALID_NODE_ID) {
        config.node_id = 1;
    }

    DMP_INFO("Starting Distributed Memory Pool Server...");
    DMP_INFO("Configuration:");
    DMP_INFO("  Node ID: {}", config.node_id);
    DMP_INFO("  gRPC: {}:{}", config.grpc_address, config.grpc_port);
    DMP_INFO("  RDMA device: {}, port: {}, gid_index: {}",
             config.rdma_device, config.rdma_port, config.rdma_gid_index);
    DMP_INFO("  Total memory: {} MB", config.total_memory / (1024 * 1024));
    DMP_INFO("  Use hugepages: {}", config.use_hugepages ? "yes" : "no");

    auto service = std::make_unique<MemoryServiceImpl>();
    if (!service->initialize(config)) {
        DMP_ERROR("Failed to initialize memory service");
        return 1;
    }

    auto monitor = std::make_unique<Monitor>();
    if (!monitor->initialize(&service->memory_pool(), config.monitor_port)) {
        DMP_ERROR("Failed to initialize monitor");
        return 1;
    }

    std::string server_address = config.grpc_address + ":" +
                                  std::to_string(config.grpc_port);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(service.get());

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (!server) {
        DMP_ERROR("Failed to start gRPC server");
        return 1;
    }

    DMP_INFO("Server started on {}", server_address);

    auto stats_printer = std::thread([&]() {
        while (g_running.load(std::memory_order_acquire)) {
            auto stats = monitor->get_stats();
            DMP_INFO("Stats: usage={:.2f}%, free={}MB, fragments={:.2f}%, blocks={}/{}",
                     stats.usage_percent,
                     stats.free_capacity / (1024 * 1024),
                     stats.fragmentation_ratio * 100,
                     stats.allocated_blocks,
                     stats.total_blocks);

            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    });

    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    DMP_INFO("Shutting down server...");

    g_running.store(false, std::memory_order_release);

    if (stats_printer.joinable()) {
        stats_printer.join();
    }

    server->Shutdown();

    monitor->shutdown();
    service->shutdown();

    DMP_INFO("Server stopped");

    return 0;
}
