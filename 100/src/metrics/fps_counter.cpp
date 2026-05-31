#include "metrics/fps_counter.h"

FPSCounter::FPSCounter(int window_size)
    : window_size_(window_size) {
    last_time_ = std::chrono::steady_clock::now();
}

void FPSCounter::tick() {
    auto now = std::chrono::steady_clock::now();
    tick(now);
}

void FPSCounter::tick(const std::chrono::steady_clock::time_point& timestamp) {
    double elapsed_ms = std::chrono::duration<double, std::milli>(
        timestamp - last_time_).count();
    last_time_ = timestamp;

    if (elapsed_ms > 0.0) {
        double fps = 1000.0 / elapsed_ms;
        frame_times_.push_back(fps);
        if (frame_times_.size() > static_cast<size_t>(window_size_)) {
            frame_times_.pop_front();
        }
        all_fps_.push_back(fps);
    }

    total_frames_++;
    updateStats();
}

double FPSCounter::getCurrentFPS() const {
    return current_fps_;
}

double FPSCounter::getAverageFPS() const {
    return average_fps_;
}

double FPSCounter::getMinFPS() const {
    return min_fps_;
}

double FPSCounter::getMaxFPS() const {
    return max_fps_;
}

void FPSCounter::reset() {
    frame_times_.clear();
    all_fps_.clear();
    total_frames_ = 0;
    current_fps_ = 0.0;
    average_fps_ = 0.0;
    min_fps_ = 0.0;
    max_fps_ = 0.0;
    last_time_ = std::chrono::steady_clock::now();
}

void FPSCounter::updateStats() {
    if (frame_times_.empty()) return;

    current_fps_ = frame_times_.back();

    double sum = 0.0;
    for (double fps : frame_times_) {
        sum += fps;
    }
    average_fps_ = sum / frame_times_.size();

    if (!all_fps_.empty()) {
        min_fps_ = *std::min_element(all_fps_.begin(), all_fps_.end());
        max_fps_ = *std::max_element(all_fps_.begin(), all_fps_.end());
    }
}