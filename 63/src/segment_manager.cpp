#include "segment_manager.hpp"
#include <algorithm>
#include <iostream>

namespace afp {

SegmentManager::SegmentManager(double segment_duration, double overlap_duration)
    : segment_duration_(segment_duration), overlap_duration_(overlap_duration) {
}

SegmentManager::~SegmentManager() = default;

void SegmentManager::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
    current_start_time_ = 0.0;
    segment_count_ = 0;
    first_timestamp_ = -1;
}

void SegmentManager::pushSamples(const std::vector<Sample>& samples, int64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (first_timestamp_ < 0) {
        first_timestamp_ = timestamp;
        current_start_time_ = 0.0;
    }

    buffer_.insert(buffer_.end(), samples.begin(), samples.end());

    processBuffer();
}

void SegmentManager::processBuffer() {
    int samples_per_segment = static_cast<int>(segment_duration_ * SAMPLE_RATE);
    int samples_per_overlap = static_cast<int>(overlap_duration_ * SAMPLE_RATE);
    int samples_per_hop = samples_per_segment - samples_per_overlap;

    if (samples_per_hop <= 0) {
        samples_per_hop = samples_per_segment;
    }

    while (static_cast<int>(buffer_.size()) >= samples_per_segment) {
        std::vector<Sample> segment(buffer_.begin(), buffer_.begin() + samples_per_segment);

        double end_time = current_start_time_ + segment_duration_;

        if (callback_) {
            callback_(segment, current_start_time_, end_time);
        }

        segment_count_++;
        current_start_time_ += (double)samples_per_hop / SAMPLE_RATE;

        int erase_count = std::min(samples_per_hop, static_cast<int>(buffer_.size()));
        buffer_.erase(buffer_.begin(), buffer_.begin() + erase_count);
    }
}

}
