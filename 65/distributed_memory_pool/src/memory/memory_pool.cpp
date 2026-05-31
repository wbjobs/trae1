#include "memory/memory_pool.h"
#include <algorithm>
#include <cstring>

namespace dmp {

MemoryPool::MemoryPool()
    : base_addr_(nullptr)
    , total_size_(0)
    , rkey_(0)
    , lkey_(0)
{
}

MemoryPool::~MemoryPool() = default;

bool MemoryPool::initialize(uint64_t total_size, void* base_addr) {
    base_addr_ = base_addr;
    total_size_ = total_size;

    if (!slab_allocator_.initialize(total_size, 0)) {
        DMP_ERROR("Failed to initialize slab allocator");
        return false;
    }

    DMP_INFO("MemoryPool initialized: total_size={}MB, base_addr={}",
             total_size / (1024 * 1024), base_addr);

    return true;
}

BlockId MemoryPool::generate_block_id() {
    return next_block_id_.fetch_add(1, std::memory_order_relaxed);
}

ResultT<BlockInfo> MemoryPool::allocate(uint64_t size, NodeId owner_node_id) {
    if (size == 0 || size > MAX_TRANSFER_SIZE) {
        return ResultT<BlockInfo>::error("Invalid allocation size");
    }

    uint64_t total_size = size + BLOCK_DATA_HEADER_SIZE;
    uint64_t aligned_size = align_up(total_size, MIN_BLOCK_SIZE);

    auto alloc_result = slab_allocator_.allocate(aligned_size);
    if (!alloc_result.success) {
        return ResultT<BlockInfo>::error(alloc_result.error_message);
    }

    BlockId block_id = generate_block_id();

    void* block_ptr = static_cast<uint8_t*>(base_addr_) + alloc_result.value;
    BlockDataState* state_ptr = reinterpret_cast<BlockDataState*>(block_ptr);
    *state_ptr = BlockDataState::INITIAL;

    BlockInfo info{};
    info.block_id = block_id;
    info.offset = alloc_result.value;
    info.size = aligned_size;
    info.data_offset = alloc_result.value + BLOCK_DATA_HEADER_SIZE;
    info.state = BlockState::Allocated;
    info.owner_node_id = owner_node_id;
    info.lease_expiry = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(LEASE_DURATION_MS);
    info.rkey = rkey_;
    info.remote_addr = reinterpret_cast<uint64_t>(base_addr_) + alloc_result.value;

    {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        blocks_[block_id] = info;
        offset_to_block_[info.offset] = block_id;
    }

    DMP_DEBUG("Allocated block: id={}, offset={}, size={}, data_offset={}, owner={}",
              block_id, info.offset, info.size, info.data_offset, owner_node_id);

    return ResultT<BlockInfo>::ok(info);
}

Result MemoryPool::release(BlockId block_id, NodeId owner_node_id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);

    auto it = blocks_.find(block_id);
    if (it == blocks_.end()) {
        return Result::error("Block not found: " + std::to_string(block_id));
    }

    if (it->second.owner_node_id != owner_node_id) {
        return Result::error("Block owned by different node");
    }

    if (it->second.state != BlockState::Allocated &&
        it->second.state != BlockState::Fault) {
        return Result::error("Block is not in releasable state");
    }

    auto slab_result = slab_allocator_.release(it->second.offset);
    if (!slab_result.success) {
        return Result::error("Failed to release slab: " + slab_result.error_message);
    }

    offset_to_block_.erase(it->second.offset);
    blocks_.erase(it);

    DMP_DEBUG("Released block: id={}", block_id);

    return Result::ok();
}

ResultT<BlockInfo> MemoryPool::get_block_info(BlockId block_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = blocks_.find(block_id);
    if (it == blocks_.end()) {
        return ResultT<BlockInfo>::error("Block not found");
    }

    return ResultT<BlockInfo>::ok(it->second);
}

ResultT<BlockInfo> MemoryPool::find_block_by_offset(uint64_t offset) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = offset_to_block_.find(offset);
    if (it == offset_to_block_.end()) {
        return ResultT<BlockInfo>::error("Block not found for offset");
    }

    auto block_it = blocks_.find(it->second);
    if (block_it == blocks_.end()) {
        return ResultT<BlockInfo>::error("Block not found");
    }

    return ResultT<BlockInfo>::ok(block_it->second);
}

Stats MemoryPool::get_stats() const {
    return slab_allocator_.get_stats();
}

std::vector<BlockInfo> MemoryPool::get_all_blocks() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::vector<BlockInfo> result;
    result.reserve(blocks_.size());

    for (const auto& [id, info] : blocks_) {
        result.push_back(info);
    }

    return result;
}

std::vector<BlockInfo> MemoryPool::get_leaked_blocks() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    std::vector<BlockInfo> leaked;

    for (const auto& [id, info] : blocks_) {
        if (info.state == BlockState::Allocated && info.lease_expiry < now) {
            leaked.push_back(info);
        }
    }

    return leaked;
}

ResultT<uint64_t> MemoryPool::recover_node(NodeId failed_node_id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);

    uint64_t recovered_memory = 0;
    std::vector<BlockId> to_release;

    for (const auto& [id, info] : blocks_) {
        if (info.owner_node_id == failed_node_id) {
            to_release.push_back(id);
            recovered_memory += info.size;
        }
    }

    for (auto block_id : to_release) {
        auto it = blocks_.find(block_id);
        if (it != blocks_.end()) {
            slab_allocator_.release(it->second.offset);
            offset_to_block_.erase(it->second.offset);
            blocks_.erase(it);
        }
    }

    DMP_INFO("Recovered node {}: {} blocks, {}MB",
             failed_node_id, to_release.size(), recovered_memory / (1024 * 1024));

    return ResultT<uint64_t>::ok(recovered_memory);
}

bool MemoryPool::renew_lease(BlockId block_id, NodeId owner_node_id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);

    auto it = blocks_.find(block_id);
    if (it == blocks_.end()) {
        return false;
    }

    if (it->second.owner_node_id != owner_node_id) {
        return false;
    }

    it->second.lease_expiry = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(LEASE_DURATION_MS);
    return true;
}

void MemoryPool::check_leases() {
    std::lock_guard<std::shared_mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    std::vector<BlockId> expired;

    for (const auto& [id, info] : blocks_) {
        if (info.state == BlockState::Allocated && info.lease_expiry < now) {
            expired.push_back(id);
        }
    }

    for (auto block_id : expired) {
        auto it = blocks_.find(block_id);
        if (it != blocks_.end()) {
            it->second.state = BlockState::Fault;

            if (lease_callback_) {
                lease_callback_(block_id);
            }

            DMP_WARN("Block {} lease expired, marked as fault", block_id);
        }
    }

    if (!expired.empty()) {
        DMP_INFO("Lease check: {} blocks expired", expired.size());
    }
}

BlockDataState MemoryPool::get_block_data_state(BlockId block_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = blocks_.find(block_id);
    if (it == blocks_.end()) {
        return BlockDataState::INITIAL;
    }

    void* block_ptr = static_cast<uint8_t*>(base_addr_) + it->second.offset;
    BlockDataState* state_ptr = reinterpret_cast<BlockDataState*>(block_ptr);
    return *state_ptr;
}

void MemoryPool::set_block_data_state(BlockId block_id, BlockDataState state) {
    std::lock_guard<std::shared_mutex> lock(mutex_);

    auto it = blocks_.find(block_id);
    if (it == blocks_.end()) {
        return;
    }

    void* block_ptr = static_cast<uint8_t*>(base_addr_) + it->second.offset;
    BlockDataState* state_ptr = reinterpret_cast<BlockDataState*>(block_ptr);
    *state_ptr = state;
}

bool MemoryPool::check_block_writable(BlockId block_id) const {
    BlockDataState state = get_block_data_state(block_id);
    return state == BlockDataState::INITIAL ||
           state == BlockDataState::WRITTEN ||
           state == BlockDataState::WRITE_FAILED;
}

bool MemoryPool::check_block_readable(BlockId block_id) const {
    BlockDataState state = get_block_data_state(block_id);
    return state == BlockDataState::WRITTEN;
}

void* MemoryPool::get_block_data_ptr(BlockId block_id, uint64_t offset) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = blocks_.find(block_id);
    if (it == blocks_.end()) {
        return nullptr;
    }

    return static_cast<uint8_t*>(base_addr_) + it->second.data_offset + offset;
}

uint64_t MemoryPool::get_block_data_size(BlockId block_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = blocks_.find(block_id);
    if (it == blocks_.end()) {
        return 0;
    }

    return it->second.size - BLOCK_DATA_HEADER_SIZE;
}

}
