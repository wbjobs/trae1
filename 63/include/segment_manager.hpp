#pragma once

#include "common.hpp"
#include <vector>
#include <deque>
#include <mutex>
#include <functional>
#include <chrono>

namespace afp {

class SegmentManager {
public:
    using SegmentCallback = std::function<void(const std::vector<Sample>&, double start_time, double end_time)>;

    SegmentManager(double segment_duration = STREAM_SEGMENT_DURATION,
                   double overlap_duration = STREAM_OVERLAP_DURATION);
    ~SegmentManager();

    void pushSamples(const std::vector<Sample>& samples, int64_t timestamp);

    void setSegmentCallback(SegmentCallback callback) { callback_ = std::move(callback); }

    void setSegmentDuration(double duration) { segment_duration_ = duration; }
    void setOverlapDuration(double duration) { overlap_duration_ = duration; }

    void reset();

    size_t getBufferedSamples() const { return buffer_.size(); }
    double getBufferedDuration() const { return (double)buffer_.size() / SAMPLE_RATE; }
    int getSegmentCount() const { return segment_count_; }

private:
    void processBuffer();

    std::deque<Sample> buffer_;
    SegmentCallback callback_;

    double segment_duration_;
    double overlap_duration_;
    double current_start_time_ = 0.0;
    int segment_count_ = 0;

    int64_t first_timestamp_ = -1;

    mutable std::mutex mutex_;
};

}
