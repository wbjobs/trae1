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

namespace dmp {

static std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    DMP_INFO("Received signal {}, shutting down...", signal);
    g_running.store(false, std::memory_order_release);
}

class RpcServer {
public:
    RpcServer() = default;
    ~RpcServer() { stop(); }

    bool start(const Config& config) {
        std::string server_address = config.grpc_address + ":" +
                                      std::to_string(config.grpc_port);

        grpc::ServerBuilder builder;
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

        service_ = std::make_unique<MemoryServiceImpl>();
        if (!service_->initialize(config)) {
            DMP_ERROR("Failed to initialize memory service");
            return false;
        }

        builder.RegisterService(service_.get());

        monitor_ = std::make_unique<Monitor>();
        if (!monitor_->initialize(&service_->memory_pool(), config.monitor_port)) {
            DMP_ERROR("Failed to initialize monitor");
            return false;
        }

        server_ = builder.BuildAndStart();
        if (!server_) {
            DMP_ERROR("Failed to start gRPC server");
            return false;
        }

        DMP_INFO("RPC server started on {}", server_address);

        return true;
    }

    void stop() {
        if (server_) {
            server_->Shutdown();
            server_.reset();
        }
        if (monitor_) {
            monitor_->shutdown();
            monitor_.reset();
        }
        if (service_) {
            service_->shutdown();
            service_.reset();
        }
    }

    void wait() {
        if (server_) {
            server_->Wait();
        }
    }

private:
    std::unique_ptr<MemoryServiceImpl> service_;
    std::unique_ptr<Monitor> monitor_;
    std::unique_ptr<grpc::Server> server_;
};

}
