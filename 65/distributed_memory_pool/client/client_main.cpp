#include "memory_client.h"
#include "common/types.h"
#include "common/utils.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>

using namespace dmp;

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  --server <address>  Server address (default: localhost)\n"
              << "  --port <port>       Server port (default: 50051)\n"
              << "  --node_id <id>      Client node ID (default: 100)\n"
              << "  --size <size>       Block size to allocate (default: 4K)\n"
              << "  --count <count>     Number of operations (default: 10)\n"
              << "  --mode <mode>       Test mode: allocate, put, get, all (default: all)\n"
              << "  --help              Show this help\n";
}

Config parse_args(int argc, char* argv[]) {
    Config config;
    config.node_id = 100;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--server" && i + 1 < argc) {
            config.grpc_address = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            config.grpc_port = std::stoul(argv[++i]);
        } else if (arg == "--node_id" && i + 1 < argc) {
            config.node_id = std::stoull(argv[++i]);
        } else if (arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        }
    }

    return config;
}

int main(int argc, char* argv[]) {
    Config config = parse_args(argc, argv);

    Logger::instance().set_level(Logger::Level::INFO);

    DMP_INFO("=== Distributed Memory Pool Client ===");
    DMP_INFO("Server: {}:{}", config.grpc_address, config.grpc_port);
    DMP_INFO("Node ID: {}", config.node_id);

    MemoryClient client;

    if (!client.connect(config.grpc_address, config.grpc_port)) {
        DMP_ERROR("Failed to connect to server");
        return 1;
    }

    {
        DMP_INFO("--- Monitor Test ---");
        auto stats_result = client.monitor();
        if (stats_result.success) {
            auto& stats = stats_result.value;
            DMP_INFO("Server Stats:");
            DMP_INFO("  Total Capacity: {} MB", stats.total_capacity / (1024 * 1024));
            DMP_INFO("  Used Capacity: {} MB", stats.used_capacity / (1024 * 1024));
            DMP_INFO("  Free Capacity: {} MB", stats.free_capacity / (1024 * 1024));
            DMP_INFO("  Usage: {:.2f}%", stats.usage_percent);
            DMP_INFO("  Fragmentation: {:.2f}%", stats.fragmentation_ratio * 100);
            DMP_INFO("  Blocks: {}/{} allocated",
                     stats.allocated_blocks, stats.total_blocks);
        }
    }

    {
        DMP_INFO("--- Allocate Test ---");
        std::vector<BlockId> block_ids;

        for (int i = 0; i < 10; ++i) {
            uint64_t size = (i + 1) * 4 * 1024;
            auto result = client.allocate(size, config.node_id);

            if (result.success) {
                DMP_INFO("Allocated block {}: size={}KB, offset={}",
                         result.value.block_id, size / 1024, result.value.offset);
                block_ids.push_back(result.value.block_id);
            } else {
                DMP_ERROR("Failed to allocate block: {}", result.error_message);
                break;
            }
        }

        DMP_INFO("--- Put/GET Test ---");

        if (!block_ids.empty()) {
            BlockId test_block = block_ids.front();

            std::string test_data = "Hello, Distributed Memory Pool!";
            auto put_result = client.put(test_block, 0, test_data.data(),
                                          test_data.size(), config.node_id);

            if (put_result.success) {
                DMP_INFO("Put data to block {}: {} bytes", test_block, test_data.size());

                auto get_result = client.get(test_block, 0, test_data.size(),
                                              config.node_id);

                if (get_result.success) {
                    std::string received(get_result.value.begin(), get_result.value.end());
                    DMP_INFO("Get data from block {}: '{}'", test_block, received);

                    if (received == test_data) {
                        DMP_INFO("Data verification: SUCCESS");
                    } else {
                        DMP_ERROR("Data verification: FAILED");
                    }
                } else {
                    DMP_ERROR("Failed to get data: {}", get_result.error_message);
                }
            } else {
                DMP_ERROR("Failed to put data: {}", put_result.error_message);
            }
        }

        DMP_INFO("--- Release Test ---");

        for (auto block_id : block_ids) {
            auto result = client.release(block_id, config.node_id);
            if (result.success) {
                DMP_INFO("Released block {}", block_id);
            } else {
                DMP_ERROR("Failed to release block {}: {}", block_id, result.error_message);
            }
        }
    }

    {
        DMP_INFO("--- Final Monitor ---");
        auto stats_result = client.monitor();
        if (stats_result.success) {
            DMP_INFO("Usage: {:.2f}%, Free: {} MB",
                     stats_result.value.usage_percent,
                     stats_result.value.free_capacity / (1024 * 1024));
        }
    }

    client.disconnect();

    DMP_INFO("=== Client Test Complete ===");

    return 0;
}
