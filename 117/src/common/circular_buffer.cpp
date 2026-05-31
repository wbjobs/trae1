#include "common/circular_buffer.h"
#include <algorithm>

namespace moshpp {

CircularBuffer::CircularBuffer(size_t capacity)
    : capacity_(capacity)
    , start_(0)
    , size_(0)
    , total_written_(0)
{
    buffer_.resize(capacity_);
}

CircularBuffer::~CircularBuffer() {
}

size_t CircularBuffer::write(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (size == 0) return 0;
    if (size > capacity_) {
        size = capacity_;
        data = data + size - capacity_;
    }

    size_t end = (start_ + size_) % capacity_;
    size_t first_part = std::min(size, capacity_ - end);
    size_t second_part = size - first_part;

    std::memcpy(buffer_.data() + end, data, first_part);
    if (second_part > 0) {
        std::memcpy(buffer_.data(), data + first_part, second_part);
    }

    if (size_ + size > capacity_) {
        start_ = (start_ + size_ + size - capacity_) % capacity_;
        size_ = capacity_;
    } else {
        size_ += size;
    }

    total_written_ += size;
    return size;
}

size_t CircularBuffer::read(uint8_t* data, size_t size, size_t offset) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (offset >= total_written_) {
        return 0;
    }

    size_t available = total_written_ - offset;
    size_t to_read = std::min(size, available);
    
    if (to_read == 0) {
        return 0;
    }

    size_t buffer_start;
    if (total_written_ <= capacity_) {
        buffer_start = offset;
    } else {
        buffer_start = (start_ + (offset - (total_written_ - size_))) % capacity_;
    }

    size_t first_part = std::min(to_read, capacity_ - buffer_start);
    size_t second_part = to_read - first_part;

    std::memcpy(data, buffer_.data() + buffer_start, first_part);
    if (second_part > 0) {
        std::memcpy(data + first_part, buffer_.data(), second_part);
    }

    return to_read;
}

size_t CircularBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

void CircularBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    start_ = 0;
    size_ = 0;
    total_written_ = 0;
}

std::vector<uint8_t> CircularBuffer::get_data_since(size_t offset) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (offset >= total_written_) {
        return {};
    }

    size_t available = total_written_ - offset;
    std::vector<uint8_t> result(available);
    
    size_t buffer_start;
    if (total_written_ <= capacity_) {
        buffer_start = offset;
    } else {
        size_t earliest_available = total_written_ - size_;
        if (offset < earliest_available) {
            offset = earliest_available;
            available = total_written_ - offset;
            result.resize(available);
        }
        buffer_start = (start_ + (offset - earliest_available)) % capacity_;
    }

    size_t first_part = std::min(available, capacity_ - buffer_start);
    size_t second_part = available - first_part;

    std::memcpy(result.data(), buffer_.data() + buffer_start, first_part);
    if (second_part > 0) {
        std::memcpy(result.data() + first_part, buffer_.data(), second_part);
    }

    return result;
}

std::vector<uint8_t> CircularBuffer::get_all_data() const {
    return get_data_since(total_written_ > capacity_ ? total_written_ - capacity_ : 0);
}

std::string CircularBuffer::get_string_since(size_t offset) const {
    std::vector<uint8_t> data = get_data_since(offset);
    return std::string(data.begin(), data.end());
}

std::string CircularBuffer::get_all_string() const {
    std::vector<uint8_t> data = get_all_data();
    return std::string(data.begin(), data.end());
}

OutputBuffer::OutputBuffer(size_t max_size)
    : circular_buffer_(max_size)
    , total_bytes_(0)
{
}

void OutputBuffer::append(const std::vector<uint8_t>& data) {
    append(data.data(), data.size());
}

void OutputBuffer::append(const std::string& data) {
    append(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

void OutputBuffer::append(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    circular_buffer_.write(data, size);
    total_bytes_ = circular_buffer_.get_total_written();
}

std::vector<uint8_t> OutputBuffer::get_delta(size_t last_offset) const {
    return circular_buffer_.get_data_since(last_offset);
}

std::vector<uint8_t> OutputBuffer::get_all() const {
    return circular_buffer_.get_all_data();
}

std::string OutputBuffer::get_delta_string(size_t last_offset) const {
    return circular_buffer_.get_string_since(last_offset);
}

std::string OutputBuffer::get_all_string() const {
    return circular_buffer_.get_all_string();
}

void OutputBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    circular_buffer_.clear();
    total_bytes_ = 0;
}

}
