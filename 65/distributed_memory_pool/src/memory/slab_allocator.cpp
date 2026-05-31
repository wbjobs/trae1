#include "memory/slab_allocator.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dmp {

SlabAllocator::SlabAllocator()
    : total_size_(0)
    , base_offset_(0)
{
}

SlabAllocator::~SlabAllocator() = default;

bool SlabAllocator::initialize(uint64_t total_size, uint64_t base_offset) {
    std::lock_guard<std::shared_mutex> lock(mutex_);

    if (total_size < MIN_BLOCK_SIZE) {
        DMP_ERROR("Total size {} too small, minimum is {}", total_size, MIN_BLOCK_SIZE);
        return false;
    }

    total_size_ = total_size;
    base_offset_ = base_offset;

    create_slabs(total_size);

    DMP_INFO("SlabAllocator initialized: total_size={}MB, base_offset={}",
             total_size / (1024 * 1024), base_offset);

    return true;
}

void SlabAllocator::create_slabs(uint64_t total_size) {
    slabs_.clear();
    offset_to_slab_.clear();

    uint64_t remaining = total_size;
    uint64_t current_offset = 0;

    for (size_t i = 0; i < SLAB_CLASS_COUNT; ++i) {
        uint64_t size_class = SLAB_SIZES[i];

        uint64_t allocation;
        if (i < SLAB_CLASS_COUNT - 1) {
            allocation = total_size / SLAB_CLASS_COUNT;
            allocation = align_down(allocation, size_class);
        } else {
            allocation = align_down(remaining, size_class);
        }

        if (allocation < size_class) {
            allocation = size_class;
        }

        if (allocation > remaining && i > 0) {
            allocation = align_down(remaining, size_class);
        }

        if (allocation < size_class) {
            continue;
        }

        uint64_t count = allocation / size_class;

        auto slab = std::make_unique<Slab>(size_class, current_offset, count);
        slabs_.push_back(std::move(slab));

        for (uint64_t j = 0; j < count; ++j) {
            offset_to_slab_[current_offset + j * size_class] = i;
        }

        current_offset += allocation;
        remaining -= allocation;

        DMP_DEBUG("Slab class {}: size={}KB, count={}, offset={}",
                  i, size_class / 1024, count, current_offset - allocation);
    }

    DMP_INFO("Created {} slab classes, total allocated={}MB",
             slabs_.size(), current_offset / (1024 * 1024));
}

uint64_t SlabAllocator::find_best_size_class(uint64_t size) const {
    for (size_t i = 0; i < SLAB_CLASS_COUNT; ++i) {
        if (SLAB_SIZES[i] >= size) {
            return SLAB_SIZES[i];
        }
    }
    return SLAB_SIZES[SLAB_CLASS_COUNT - 1];
}

size_t SlabAllocator::size_class_index(uint64_t size) const {
    for (size_t i = 0; i < SLAB_CLASS_COUNT; ++i) {
        if (SLAB_SIZES[i] >= size) {
            return i;
        }
    }
    return SLAB_CLASS_COUNT - 1;
}

uint64_t SlabAllocator::size_class(size_t index) const {
    if (index >= SLAB_CLASS_COUNT) {
        return SLAB_SIZES[SLAB_CLASS_COUNT - 1];
    }
    return SLAB_SIZES[index];
}

ResultT<uint64_t> SlabAllocator::allocate(uint64_t size) {
    if (size == 0 || size > MAX_TRANSFER_SIZE) {
        return ResultT<uint64_t>::error(
            "Invalid allocation size: " + std::to_string(size));
    }

    uint64_t aligned_size = align_up(size, MIN_BLOCK_SIZE);

    {
        std::lock_guard<std::shared_mutex> lock(mutex_);

        for (size_t i = 0; i < slabs_.size(); ++i) {
            if (slabs_[i]->size_class < aligned_size) {
                continue;
            }

            if (slabs_[i]->free_count == 0) {
                continue;
            }

            uint64_t offset = allocate_from_slab(i);
            if (offset != UINT64_MAX) {
                DMP_DEBUG("Allocated block: offset={}, size={}, class={}",
                         base_offset_ + offset, slabs_[i]->size_class, i);
                return ResultT<uint64_t>::ok(base_offset_ + offset);
            }
        }
    }

    return ResultT<uint64_t>::error("No available memory block for size " +
                                     std::to_string(size));
}

uint64_t SlabAllocator::allocate_from_slab(size_t class_index) {
    if (class_index >= slabs_.size() || slabs_[class_index]->free_count == 0) {
        return UINT64_MAX;
    }

    auto& slab = slabs_[class_index];

    while (!slab->free_list.empty()) {
        uint64_t index = slab->free_list.back();
        slab->free_list.pop_back();

        if (!slab->bitmap[index]) {
            slab->bitmap[index] = true;
            slab->free_count--;
            return slab->start_offset + index * slab->size_class;
        }
    }

    return UINT64_MAX;
}

Result SlabAllocator::release(uint64_t offset) {
    uint64_t local_offset = offset - base_offset_;

    {
        std::lock_guard<std::shared_mutex> lock(mutex_);

        auto it = offset_to_slab_.find(local_offset);
        if (it == offset_to_slab_.end()) {
            return Result::error("Invalid offset: " + std::to_string(offset));
        }

        deallocate_to_slab(local_offset);
    }

    DMP_DEBUG("Released block: offset={}", offset);
    return Result::ok();
}

void SlabAllocator::deallocate_to_slab(uint64_t local_offset) {
    for (size_t i = 0; i < slabs_.size(); ++i) {
        auto& slab = slabs_[i];
        if (local_offset >= slab->start_offset &&
            local_offset < slab->start_offset + slab->size_class * slab->total_count) {

            uint64_t index = (local_offset - slab->start_offset) / slab->size_class;

            if (slab->bitmap[index]) {
                slab->bitmap[index] = false;
                slab->free_list.push_back(index);
                slab->free_count++;
            }
            return;
        }
    }
}

uint64_t SlabAllocator::used_capacity() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    uint64_t used = 0;
    for (const auto& slab : slabs_) {
        used += (slab->total_count - slab->free_count) * slab->size_class;
    }
    return used;
}

uint64_t SlabAllocator::free_capacity() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    uint64_t free = 0;
    for (const auto& slab : slabs_) {
        free += slab->free_count * slab->size_class;
    }
    return free;
}

Stats SlabAllocator::get_stats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    Stats stats{};
    stats.total_capacity = total_size_;
    stats.allocated_blocks = 0;
    stats.free_blocks = 0;
    stats.total_blocks = 0;

    for (const auto& slab : slabs_) {
        uint64_t allocated = slab->total_count - slab->free_count;
        stats.allocated_blocks += allocated;
        stats.free_blocks += slab->free_count;
        stats.total_blocks += slab->total_count;
        stats.used_capacity += allocated * slab->size_class;
        stats.free_capacity += slab->free_count * slab->size_class;
    }

    stats.usage_percent = total_size_ > 0
        ? (static_cast<double>(stats.used_capacity) / total_size_) * 100.0
        : 0.0;

    stats.fragmentation_ratio = fragmentation_ratio();

    return stats;
}

double SlabAllocator::fragmentation_ratio() const {
    uint64_t total_free = 0;
    uint64_t max_free_block = 0;

    for (const auto& slab : slabs_) {
        uint64_t class_free = slab->free_count * slab->size_class;
        total_free += class_free;
        if (class_free > max_free_block) {
            max_free_block = class_free;
        }
    }

    if (total_free == 0) {
        return 0.0;
    }

    uint64_t sum_free = 0;
    for (const auto& slab : slabs_) {
        sum_free += slab->free_count * slab->size_class;
    }

    return 1.0 - (static_cast<double>(max_free_block) / total_free);
}

}
