#pragma once

#include "common/types.h"
#include "common/config.h"
#include "common/utils.h"
#include "memory/memory_pool.h"
#include "memory/distributed_lock_manager.h"
#include "rdma/rdma_context.h"
#include "rdma/rdma_transport.h"
#include "memory_service.grpc.pb.h"
#include <grpc++/grpc++.h>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <thread>
#include <atomic>

namespace dmp {

class MemoryServiceImpl final : public dmp::MemoryService::Service {
public:
    MemoryServiceImpl();
    ~MemoryServiceImpl() override;

    bool initialize(const Config& config);

    void shutdown();

    grpc::Status Allocate(grpc::ServerContext* context,
                          const dmp::AllocateRequest* request,
                          dmp::AllocateResponse* response) override;

    grpc::Status Release(grpc::ServerContext* context,
                         const dmp::ReleaseRequest* request,
                         dmp::ReleaseResponse* response) override;

    grpc::Status Put(grpc::ServerContext* context,
                     const dmp::PutRequest* request,
                     dmp::PutResponse* response) override;

    grpc::Status Get(grpc::ServerContext* context,
                     const dmp::GetRequest* request,
                     dmp::GetResponse* response) override;

    grpc::Status Lock(grpc::ServerContext* context,
                      const dmp::LockRequest* request,
                      dmp::LockResponse* response) override;

    grpc::Status Unlock(grpc::ServerContext* context,
                        const dmp::UnlockRequest* request,
                        dmp::UnlockResponse* response) override;

    grpc::Status RenewLock(grpc::ServerContext* context,
                           const dmp::RenewLockRequest* request,
                           dmp::RenewLockResponse* response) override;

    grpc::Status LockMonitor(grpc::ServerContext* context,
                             const dmp::LockMonitorRequest* request,
                             dmp::LockMonitorResponse* response) override;

    grpc::Status Monitor(grpc::ServerContext* context,
                         const dmp::MonitorRequest* request,
                         dmp::MonitorResponse* response) override;

    grpc::Status Heartbeat(grpc::ServerContext* context,
                           const dmp::HeartbeatRequest* request,
                           dmp::HeartbeatResponse* response) override;

    grpc::Status ReportNodeFailure(grpc::ServerContext* context,
                                    const dmp::NodeFailureRequest* request,
                                    dmp::NodeFailureResponse* response) override;

    MemoryPool& memory_pool() { return *memory_pool_; }
    RdmaTransport& rdma_transport() { return *rdma_transport_; }
    RdmaContext& rdma_context() { return *rdma_context_; }
    DistributedLockManager& lock_manager() { return *lock_manager_; }

    const Config& config() const { return config_; }

private:
    bool initialize_memory();
    bool initialize_rdma();
    bool initialize_lock_manager();
    void start_background_tasks();
    void stop_background_tasks();
    void heartbeat_task();
    void lease_check_task();

    Config config_;
    std::unique_ptr<MemoryPool> memory_pool_;
    std::unique_ptr<DistributedLockManager> lock_manager_;
    std::unique_ptr<RdmaContext> rdma_context_;
    std::unique_ptr<RdmaTransport> rdma_transport_;
    void* memory_base_;
    ibv_mr* memory_mr_;
    std::unique_ptr<std::thread> heartbeat_thread_;
    std::unique_ptr<std::thread> lease_check_thread_;
    std::atomic<bool> running_{false};

    struct PeerInfo {
        NodeId node_id;
        std::string address;
        uint32_t grpc_port;
        std::chrono::steady_clock::time_point last_heartbeat;
        NodeState state;
    };

    mutable std::shared_mutex peers_mutex_;
    std::unordered_map<NodeId, PeerInfo> peers_;
};

}
