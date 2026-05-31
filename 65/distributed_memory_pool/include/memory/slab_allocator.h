#pragma once

#include "common/types.h"
#include "common/utils.h"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <memory>

namespace dmp {

struct Slab {
    uint64_t size_class;
    uint64_t start_offset;
    uint64_t free_count;
    uint64_t total_count;
    std::vector<bool> bitmap;
    std::vector<uint64_t> free_list;

    Slab(uint64_t size, uint64_t start, uint64_t count)
        : size_class(size)
        , start_offset(start)
        , free_count(count)
        , total_count(count)
        , bitmap(count, false)
    {
        free_list.reserve(count);
        for (uint64_t i = 0; i < count; ++i) {
            free_list.push_back(i);
        }
    }
};

class SlabAllocator {
public:
    SlabAllocator();
    ~SlabAllocator();

    bool initialize(uint64_t total_size, uint64_t base_offset = 0);

    ResultT<uint64_t> allocate(uint64_t size);

    Result release(uint64_t offset);

    size_t size_class_index(uint64_t size) const;

    uint64_t size_class(size_t index) const;

    Stats get_stats() const;

    double fragmentation_ratio() const;

    size_t total_classes() const { return SLAB_CLASS_COUNT; }

    uint64_t total_capacity() const { return total_size_; }

    uint64_t used_capacity() const;
    uint64_t free_capacity() const;

private:
    void create_slabs(uint64_t total_size);

    uint64_t find_best_size_class(uint64_t size) const;

    uint64_t allocate_from_slab(size_t class_index);

    void deallocate_to_slab(uint64_t offset);

    mutable std::shared_mutex mutex_;
    std::vector<std::unique_ptr<Slab>> slabs_;
    std::unordered_map<uint64_t, size_t> offset_to_slab_;
    uint64_t total_size_;
    uint64_t base_offset_;
    std::atomic<uint64_t> next_block_id_{1};
};

}
