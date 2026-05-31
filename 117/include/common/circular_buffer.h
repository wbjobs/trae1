#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace moshpp {

class CircularBuffer {
public:
    explicit CircularBuffer(size_t capacity = 1024 * 1024);
    ~CircularBuffer();

    size_t write(const uint8_t* data, size_t size);
    size_t read(uint8_t* data, size_t size, size_t offset = 0);
    
    size_t size() const;
    size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == capacity_; }
    
    void clear();
    
    size_t get_total_written() const { return total_written_; }
    
    std::vector<uint8_t> get_data_since(size_t offset) const;
    std::vector<uint8_t> get_all_data() const;
    
    std::string get_string_since(size_t offset) const;
    std::string get_all_string() const;

private:
    std::vector<uint8_t> buffer_;
    size_t capacity_;
    size_t start_;
    size_t size_;
    size_t total_written_;
    mutable std::mutex mutex_;
};

class OutputBuffer {
public:
    explicit OutputBuffer(size_t max_size = 1024 * 1024);
    
    void append(const std::vector<uint8_t>& data);
    void append(const std::string& data);
    void append(const uint8_t* data, size_t size);
    
    size_t get_total_bytes() const { return total_bytes_; }
    size_t get_current_size() const { return circular_buffer_.size(); }
    
    std::vector<uint8_t> get_delta(size_t last_offset) const;
    std::vector<uint8_t> get_all() const;
    
    std::string get_delta_string(size_t last_offset) const;
    std::string get_all_string() const;
    
    void clear();
    size_t capacity() const { return circular_buffer_.capacity(); }

private:
    CircularBuffer circular_buffer_;
    size_t total_bytes_;
    mutable std::mutex mutex_;
};

}
