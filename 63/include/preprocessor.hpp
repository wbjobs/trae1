#pragma once

#include "common.hpp"

namespace afp {

class Preprocessor {
public:
    Preprocessor(int sample_rate = SAMPLE_RATE, double cutoff = HIGH_PASS_CUTOFF);

    void process(const Frame& input, Frame& output);

    void processForNoiseEstimation(const Frame& input, Frame& output);

    void reset();

    bool isNoiseEstimationComplete() const { return noise_estimation_done_; }

    int getNoiseEstimationFrames() const { return noise_frame_count_; }

    static constexpr int NOISE_ESTIMATION_TOTAL_FRAMES =
        static_cast<int>(NOISE_ESTIMATION_DURATION * SAMPLE_RATE / FRAME_SIZE);

private:
    void highPassFilter(Frame& frame);
    void normalize(Frame& frame);
    void adaptiveGainControl(Frame& frame);

    int sample_rate_;
    double cutoff_;
    double prev_input_ = 0.0;
    double prev_output_ = 0.0;
    double alpha_;

    bool noise_estimation_done_ = false;
    int noise_frame_count_ = 0;
    double avg_energy_ = 0.0;
};

}
