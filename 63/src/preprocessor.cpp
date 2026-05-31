#include "preprocessor.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <iostream>

namespace afp {

Preprocessor::Preprocessor(int sample_rate, double cutoff)
    : sample_rate_(sample_rate), cutoff_(cutoff) {
    double rc = 1.0 / (2.0 * M_PI * cutoff_);
    double dt = 1.0 / sample_rate_;
    alpha_ = rc / (rc + dt);
}

void Preprocessor::reset() {
    prev_input_ = 0.0;
    prev_output_ = 0.0;
    noise_estimation_done_ = false;
    noise_frame_count_ = 0;
    avg_energy_ = 0.0;
}

void Preprocessor::process(const Frame& input, Frame& output) {
    if (output.size() != input.size()) {
        output.resize(input.size());
    }

    for (size_t i = 0; i < input.size(); ++i) {
        output[i] = input[i];
    }

    highPassFilter(output);

    if (!noise_estimation_done_) {
        adaptiveGainControl(output);
    }

    normalize(output);
}

void Preprocessor::processForNoiseEstimation(const Frame& input, Frame& output) {
    if (output.size() != input.size()) {
        output.resize(input.size());
    }

    for (size_t i = 0; i < input.size(); ++i) {
        output[i] = input[i];
    }

    highPassFilter(output);

    double energy = 0.0;
    for (const auto& s : output) {
        energy += s * s;
    }
    energy /= output.size();

    if (noise_frame_count_ < NOISE_ESTIMATION_TOTAL_FRAMES) {
        avg_energy_ = (avg_energy_ * noise_frame_count_ + energy) /
                     (noise_frame_count_ + 1);
        noise_frame_count_++;

        if (noise_frame_count_ >= NOISE_ESTIMATION_TOTAL_FRAMES) {
            noise_estimation_done_ = true;
        }
    }

    normalize(output);
}

void Preprocessor::highPassFilter(Frame& frame) {
    for (size_t i = 0; i < frame.size(); ++i) {
        double current_input = frame[i];
        frame[i] = alpha_ * (prev_output_ + current_input - prev_input_);
        prev_input_ = current_input;
        prev_output_ = frame[i];
    }
}

void Preprocessor::adaptiveGainControl(Frame& frame) {
    if (avg_energy_ < 1e-10) return;

    double current_energy = 0.0;
    for (const auto& s : frame) {
        current_energy += s * s;
    }
    current_energy /= frame.size();

    if (current_energy > 1e-10) {
        double gain = std::sqrt(avg_energy_ / current_energy);
        gain = std::min(gain, 10.0);
        gain = std::max(gain, 0.1);

        for (auto& s : frame) {
            s *= gain;
        }
    }
}

void Preprocessor::normalize(Frame& frame) {
    double max_val = 0.0;
    for (const auto& s : frame) {
        double abs_val = std::abs(s);
        if (abs_val > max_val) max_val = abs_val;
    }

    if (max_val > 1e-10) {
        double scale = 1.0 / max_val;
        for (auto& s : frame) {
            s *= scale;
        }
    }
}

}
