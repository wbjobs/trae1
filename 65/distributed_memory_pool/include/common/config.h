#pragma once

#include "types.h"
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <map>
#include <cstring>
#include <iostream>

namespace dmp {

struct Config {
    NodeId node_id = INVALID_NODE_ID;
    std::string grpc_address = "0.0.0.0";
    uint32_t grpc_port = DEFAULT_GRPC_PORT;
    std::string rdma_device = "mlx5_0";
    uint16_t rdma_port = DEFAULT_RDMA_PORT;
    uint16_t rdma_gid_index = 0;
    uint64_t total_memory = TOTAL_CAPACITY;
    std::string memory_backing_file;
    bool use_hugepages = false;
    std::vector<std::pair<std::string, uint32_t>> peer_nodes;
    uint64_t heartbeat_interval_ms = HEARTBEAT_INTERVAL_MS;
    uint64_t heartbeat_timeout_ms = HEARTBEAT_TIMEOUT_MS;
    uint64_t lease_duration_ms = LEASE_DURATION_MS;
    bool enable_monitoring = true;
    uint32_t monitor_port = 0;

    static Config load_from_file(const std::string& path) {
        Config config;
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open config file: " + path);
        }

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;

            auto eq_pos = line.find('=');
            if (eq_pos == std::string::npos) continue;

            std::string key = line.substr(0, eq_pos);
            std::string value = line.substr(eq_pos + 1);

            trim(key);
            trim(value);

            try {
                if (key == "node_id") {
                    config.node_id = std::stoull(value);
                } else if (key == "grpc_address") {
                    config.grpc_address = value;
                } else if (key == "grpc_port") {
                    config.grpc_port = std::stoul(value);
                } else if (key == "rdma_device") {
                    config.rdma_device = value;
                } else if (key == "rdma_port") {
                    config.rdma_port = static_cast<uint16_t>(std::stoul(value));
                } else if (key == "rdma_gid_index") {
                    config.rdma_gid_index = static_cast<uint16_t>(std::stoul(value));
                } else if (key == "total_memory") {
                    config.total_memory = parse_size(value);
                } else if (key == "memory_backing_file") {
                    config.memory_backing_file = value;
                } else if (key == "use_hugepages") {
                    config.use_hugepages = (value == "true" || value == "1");
                } else if (key == "peer") {
                    auto colon_pos = value.rfind(':');
                    if (colon_pos != std::string::npos) {
                        std::string addr = value.substr(0, colon_pos);
                        uint32_t port = std::stoul(value.substr(colon_pos + 1));
                        config.peer_nodes.emplace_back(addr, port);
                    }
                } else if (key == "heartbeat_interval_ms") {
                    config.heartbeat_interval_ms = std::stoull(value);
                } else if (key == "heartbeat_timeout_ms") {
                    config.heartbeat_timeout_ms = std::stoull(value);
                } else if (key == "lease_duration_ms") {
                    config.lease_duration_ms = std::stoull(value);
                } else if (key == "enable_monitoring") {
                    config.enable_monitoring = (value == "true" || value == "1");
                } else if (key == "monitor_port") {
                    config.monitor_port = std::stoul(value);
                }
            } catch (const std::exception& e) {
                std::cerr << "Warning: failed to parse config value for '" << key
                          << "': " << e.what() << std::endl;
            }
        }

        return config;
    }

    static void trim(std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        size_t end = s.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) {
            s.clear();
        } else {
            s = s.substr(start, end - start + 1);
        }
    }

    static uint64_t parse_size(const std::string& value) {
        uint64_t multiplier = 1;
        std::string num = value;

        if (value.size() >= 2) {
            char suffix = value.back();
            if (suffix == 'G' || suffix == 'g') {
                multiplier = 1024ULL * 1024 * 1024;
                num = value.substr(0, value.size() - 1);
            } else if (suffix == 'M' || suffix == 'm') {
                multiplier = 1024ULL * 1024;
                num = value.substr(0, value.size() - 1);
            } else if (suffix == 'K' || suffix == 'k') {
                multiplier = 1024ULL;
                num = value.substr(0, value.size() - 1);
            }
        }

        return std::stoull(num) * multiplier;
    }
};

}
