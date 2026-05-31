#pragma once

#include "utils/common.h"
#include <deque>

class FPSCounter {
public:
    FPSCounter(int window_size = 30);

    void tick();
    void tick(const std::chrono::steady_clock::time_point& timestamp);

    double getCurrentFPS() const;
    double getAverageFPS() const;
    double getMinFPS() const;
    double getMaxFPS() const;
    uint64_t getTotalFrames() const { return total_frames_; }

    void reset();

private:
    void updateStats();

    std::deque<double> frame_times_;
    std::chrono::steady_clock::time_point last_time_;
    int window_size_;
    uint64_t total_frames_ = 0;
    double current_fps_ = 0.0;
    double average_fps_ = 0.0;
    double min_fps_ = 0.0;
    double max_fps_ = 0.0;
    std::vector<double> all_fps_;
};