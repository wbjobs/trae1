#pragma once

#include "common/types.h"
#include "common/utils.h"
#include "memory/slab_allocator.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <functional>

namespace dmp {

class MemoryPool {
public:
    using LeaseCallback = std::function<void(BlockId)>;

    MemoryPool();
    ~MemoryPool();

    bool initialize(uint64_t total_size, void* base_addr);

    ResultT<BlockInfo> allocate(uint64_t size, NodeId owner_node_id);

    Result release(BlockId block_id, NodeId owner_node_id);

    ResultT<BlockInfo> get_block_info(BlockId block_id) const;

    ResultT<BlockInfo> find_block_by_offset(uint64_t offset) const;

    BlockDataState get_block_data_state(BlockId block_id) const;

    void set_block_data_state(BlockId block_id, BlockDataState state);

    bool check_block_writable(BlockId block_id) const;

    bool check_block_readable(BlockId block_id) const;

    void* get_block_data_ptr(BlockId block_id, uint64_t offset = 0) const;

    uint64_t get_block_data_size(BlockId block_id) const;

    void* get_base_address() const { return base_addr_; }

    uint64_t get_base_offset() const { return 0; }

    uint32_t get_rkey() const { return rkey_; }
    uint32_t get_lkey() const { return lkey_; }

    void set_rkey(uint32_t rkey) { rkey_ = rkey; }
    void set_lkey(uint32_t lkey) { lkey_ = lkey; }

    Stats get_stats() const;

    std::vector<BlockInfo> get_all_blocks() const;

    std::vector<BlockInfo> get_leaked_blocks() const;

    ResultT<uint64_t> recover_node(NodeId failed_node_id);

    bool renew_lease(BlockId block_id, NodeId owner_node_id);

    void set_lease_callback(LeaseCallback callback) { lease_callback_ = std::move(callback); }

    void check_leases();

    SlabAllocator& slab_allocator() { return slab_allocator_; }
    const SlabAllocator& slab_allocator() const { return slab_allocator_; }

private:
    BlockId generate_block_id();

    mutable std::shared_mutex mutex_;
    SlabAllocator slab_allocator_;
    std::unordered_map<BlockId, BlockInfo> blocks_;
    std::unordered_map<uint64_t, BlockId> offset_to_block_;
    void* base_addr_;
    uint64_t total_size_;
    uint32_t rkey_;
    uint32_t lkey_;
    std::atomic<BlockId> next_block_id_{1};
    LeaseCallback lease_callback_;
};

}
