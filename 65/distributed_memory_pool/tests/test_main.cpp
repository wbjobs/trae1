#include "memory/slab_allocator.h"
#include "memory/memory_pool.h"
#include "memory/distributed_lock_manager.h"
#include "common/utils.h"
#include <gtest/gtest.h>
#include <cstring>
#include <thread>
#include <vector>

using namespace dmp;

class SlabAllocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator_.initialize(64 * 1024 * 1024, 0);
    }

    SlabAllocator allocator_;
};

TEST_F(SlabAllocatorTest, Initialize) {
    EXPECT_EQ(allocator_.total_capacity(), 64 * 1024 * 1024);
    EXPECT_GT(allocator_.free_capacity(), 0);
}

TEST_F(SlabAllocatorTest, AllocateBasic) {
    auto result = allocator_.allocate(4096);
    EXPECT_TRUE(result.success);
    EXPECT_NE(result.value, UINT64_MAX);
}

TEST_F(SlabAllocatorTest, AllocateMultiple) {
    std::vector<uint64_t> offsets;
    for (int i = 0; i < 10; ++i) {
        auto result = allocator_.allocate(4096);
        ASSERT_TRUE(result.success);
        offsets.push_back(result.value);
    }

    EXPECT_EQ(offsets.size(), 10);
}

TEST_F(SlabAllocatorTest, Release) {
    auto result = allocator_.allocate(4096);
    ASSERT_TRUE(result.success);

    auto release_result = allocator_.release(result.value);
    EXPECT_TRUE(release_result.success);
}

TEST_F(SlabAllocatorTest, AllocateReleaseReuse) {
    auto result1 = allocator_.allocate(4096);
    ASSERT_TRUE(result1.success);

    allocator_.release(result1.value);

    auto result2 = allocator_.allocate(4096);
    EXPECT_TRUE(result2.success);
}

TEST_F(SlabAllocatorTest, FragmentationCalculation) {
    Stats stats = allocator_.get_stats();
    EXPECT_GE(stats.fragmentation_ratio, 0.0);
    EXPECT_LE(stats.fragmentation_ratio, 1.0);
}

TEST_F(SlabAllocatorTest, SizeClassIndex) {
    EXPECT_EQ(allocator_.size_class_index(4096), 0);
    EXPECT_EQ(allocator_.size_class_index(8192), 1);
}

class MemoryPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_.initialize(64 * 1024 * 1024, memory_);
    }

    void TearDown() override {
    }

    uint8_t memory_[64 * 1024 * 1024];
    MemoryPool pool_;
};

TEST_F(MemoryPoolTest, Allocate) {
    auto result = pool_.allocate(4096, 100);
    EXPECT_TRUE(result.success);
    EXPECT_GT(result.value.block_id, 0);
}

TEST_F(MemoryPoolTest, Release) {
    auto alloc_result = pool_.allocate(4096, 100);
    ASSERT_TRUE(alloc_result.success);

    auto release_result = pool_.release(alloc_result.value.block_id, 100);
    EXPECT_TRUE(release_result.success);
}

TEST_F(MemoryPoolTest, GetBlockInfo) {
    auto alloc_result = pool_.allocate(8192, 100);
    ASSERT_TRUE(alloc_result.success);

    auto info_result = pool_.get_block_info(alloc_result.value.block_id);
    EXPECT_TRUE(info_result.success);
    EXPECT_EQ(info_result.value.size, 8192);
}

TEST_F(MemoryPoolTest, Stats) {
    auto result = pool_.allocate(4096, 100);
    ASSERT_TRUE(result.success);

    Stats stats = pool_.get_stats();
    EXPECT_GT(stats.used_capacity, 0);
    EXPECT_GT(stats.allocated_blocks, 0);
}

TEST_F(MemoryPoolTest, RecoverNode) {
    std::vector<BlockId> block_ids;
    for (int i = 0; i < 5; ++i) {
        auto result = pool_.allocate(4096, 200);
        ASSERT_TRUE(result.success);
        block_ids.push_back(result.value.block_id);
    }

    auto recover_result = pool_.recover_node(200);
    EXPECT_TRUE(recover_result.success);
    EXPECT_GT(recover_result.value, 0);
}

TEST_F(MemoryPoolTest, DataStateInitial) {
    auto result = pool_.allocate(4096, 100);
    ASSERT_TRUE(result.success);

    BlockDataState state = pool_.get_block_data_state(result.value.block_id);
    EXPECT_EQ(state, BlockDataState::INITIAL);
}

TEST_F(MemoryPoolTest, DataStateTransition) {
    auto result = pool_.allocate(4096, 100);
    ASSERT_TRUE(result.success);

    BlockId block_id = result.value.block_id;

    EXPECT_TRUE(pool_.check_block_writable(block_id));
    EXPECT_FALSE(pool_.check_block_readable(block_id));

    pool_.set_block_data_state(block_id, BlockDataState::WRITING);
    EXPECT_FALSE(pool_.check_block_writable(block_id));
    EXPECT_FALSE(pool_.check_block_readable(block_id));

    pool_.set_block_data_state(block_id, BlockDataState::WRITTEN);
    EXPECT_TRUE(pool_.check_block_writable(block_id));
    EXPECT_TRUE(pool_.check_block_readable(block_id));

    pool_.set_block_data_state(block_id, BlockDataState::WRITE_FAILED);
    EXPECT_TRUE(pool_.check_block_writable(block_id));
    EXPECT_FALSE(pool_.check_block_readable(block_id));
}

TEST_F(MemoryPoolTest, DataOffset) {
    auto result = pool_.allocate(4096, 100);
    ASSERT_TRUE(result.success);

    EXPECT_GT(result.value.data_offset, result.value.offset);
    EXPECT_EQ(result.value.data_offset, result.value.offset + BLOCK_DATA_HEADER_SIZE);
}

TEST_F(MemoryPoolTest, BlockDataSize) {
    auto result = pool_.allocate(4096, 100);
    ASSERT_TRUE(result.success);

    uint64_t data_size = pool_.get_block_data_size(result.value.block_id);
    EXPECT_GT(data_size, 0);
    EXPECT_LT(data_size, result.value.size);
}

TEST_F(MemoryPoolTest, BlockDataPtr) {
    auto result = pool_.allocate(4096, 100);
    ASSERT_TRUE(result.success);

    void* ptr = pool_.get_block_data_ptr(result.value.block_id);
    EXPECT_NE(ptr, nullptr);

    void* ptr_offset = pool_.get_block_data_ptr(result.value.block_id, 100);
    EXPECT_NE(ptr_offset, nullptr);
    EXPECT_EQ(static_cast<uint8_t*>(ptr_offset) - static_cast<uint8_t*>(ptr), 100);
}

class DistributedLockManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_.initialize(64 * 1024 * 1024, memory_);
        lock_manager_.initialize(&pool_);
    }

    void TearDown() override {
        lock_manager_.shutdown();
    }

    uint8_t memory_[64 * 1024 * 1024];
    MemoryPool pool_;
    DistributedLockManager lock_manager_;
};

TEST_F(DistributedLockManagerTest, Initialize) {
    auto stats = lock_manager_.get_stats();
    EXPECT_EQ(stats.active_locks, 0);
}

TEST_F(DistributedLockManagerTest, LockAndUnlock) {
    auto alloc_result = pool_.allocate(4096, 100);
    ASSERT_TRUE(alloc_result.success);

    BlockId block_id = alloc_result.value.block_id;

    auto lock_result = lock_manager_.try_lock(block_id, 100);
    EXPECT_TRUE(lock_result.success);
    EXPECT_EQ(lock_result.value.owner_node_id, 100);

    EXPECT_TRUE(lock_manager_.is_locked(block_id));
    EXPECT_TRUE(lock_manager_.is_locked_by(block_id, 100));

    auto unlock_result = lock_manager_.unlock(block_id, 100);
    EXPECT_TRUE(unlock_result.success);

    EXPECT_FALSE(lock_manager_.is_locked(block_id));
}

TEST_F(DistributedLockManagerTest, LockContention) {
    auto alloc_result = pool_.allocate(4096, 100);
    ASSERT_TRUE(alloc_result.success);

    BlockId block_id = alloc_result.value.block_id;

    auto lock_result1 = lock_manager_.try_lock(block_id, 100);
    EXPECT_TRUE(lock_result1.success);

    auto lock_result2 = lock_manager_.try_lock(block_id, 200);
    EXPECT_FALSE(lock_result2.success);

    lock_manager_.unlock(block_id, 100);

    auto lock_result3 = lock_manager_.try_lock(block_id, 200);
    EXPECT_TRUE(lock_result3.success);
}

TEST_F(DistributedLockManagerTest, ReentrantLock) {
    auto alloc_result = pool_.allocate(4096, 100);
    ASSERT_TRUE(alloc_result.success);

    BlockId block_id = alloc_result.value.block_id;

    auto lock_result1 = lock_manager_.try_lock(block_id, 100);
    EXPECT_TRUE(lock_result1.success);

    auto lock_result2 = lock_manager_.try_lock(block_id, 100);
    EXPECT_TRUE(lock_result2.success);
    EXPECT_EQ(lock_result2.value.acquire_count, 2);

    auto unlock_result1 = lock_manager_.unlock(block_id, 100);
    EXPECT_TRUE(unlock_result1.success);

    EXPECT_TRUE(lock_manager_.is_locked(block_id));

    auto unlock_result2 = lock_manager_.unlock(block_id, 100);
    EXPECT_TRUE(unlock_result2.success);

    EXPECT_FALSE(lock_manager_.is_locked(block_id));
}

TEST_F(DistributedLockManagerTest, ForceUnlock) {
    auto alloc_result = pool_.allocate(4096, 100);
    ASSERT_TRUE(alloc_result.success);

    BlockId block_id = alloc_result.value.block_id;

    auto lock_result = lock_manager_.try_lock(block_id, 100);
    EXPECT_TRUE(lock_result.success);

    lock_manager_.force_unlock(block_id);

    EXPECT_FALSE(lock_manager_.is_locked(block_id));
}

TEST_F(DistributedLockManagerTest, RenewLock) {
    auto alloc_result = pool_.allocate(4096, 100);
    ASSERT_TRUE(alloc_result.success);

    BlockId block_id = alloc_result.value.block_id;

    auto lock_result = lock_manager_.try_lock(block_id, 100);
    EXPECT_TRUE(lock_result.success);

    auto renew_result = lock_manager_.renew_lock(block_id, 100);
    EXPECT_TRUE(renew_result.success);
}

TEST_F(DistributedLockManagerTest, LockStats) {
    auto alloc_result = pool_.allocate(4096, 100);
    ASSERT_TRUE(alloc_result.success);

    BlockId block_id = alloc_result.value.block_id;

    lock_manager_.lock(block_id, 100, 100);
    lock_manager_.unlock(block_id, 100);

    auto stats = lock_manager_.get_stats();
    EXPECT_GE(stats.lock_acquisitions, 1);
    EXPECT_GE(stats.lock_releases, 1);
}

TEST_F(DistributedLockManagerTest, ActiveLocksList) {
    auto alloc_result1 = pool_.allocate(4096, 100);
    auto alloc_result2 = pool_.allocate(8192, 100);

    ASSERT_TRUE(alloc_result1.success);
    ASSERT_TRUE(alloc_result2.success);

    lock_manager_.try_lock(alloc_result1.value.block_id, 100);
    lock_manager_.try_lock(alloc_result2.value.block_id, 100);

    auto active_locks = lock_manager_.get_active_locks();
    EXPECT_EQ(active_locks.size(), 2);

    lock_manager_.unlock(alloc_result1.value.block_id, 100);
    lock_manager_.unlock(alloc_result2.value.block_id, 100);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
