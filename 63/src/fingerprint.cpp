#include "fingerprint.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>

namespace afp {

FingerprintExtractor::FingerprintExtractor(int frame_size, int num_bins)
    : frame_size_(frame_size), num_bins_(num_bins), half_size_(frame_size / 2 + 1) {
    fft_input_ = (double*)fftw_malloc(sizeof(double) * frame_size_);
    fft_output_ = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * half_size_);
    plan_ = fftw_plan_dft_r2c_1d(frame_size_, fft_input_, fft_output_, FFTW_MEASURE);

    window_.resize(frame_size_);
    for (int i = 0; i < frame_size_; ++i) {
        window_[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (frame_size_ - 1)));
    }

    noise_estimator_local_.init(num_bins_);
}

FingerprintExtractor::~FingerprintExtractor() {
    fftw_destroy_plan(plan_);
    fftw_free(fft_input_);
    fftw_free(fft_output_);
}

void FingerprintExtractor::applyHannWindow(Frame& frame) {
    for (size_t i = 0; i < frame.size() && i < window_.size(); ++i) {
        frame[i] *= window_[i];
    }
}

void FingerprintExtractor::computeSpectrum(const Frame& frame) {
    for (int i = 0; i < frame_size_; ++i) {
        fft_input_[i] = frame[i];
    }
    fftw_execute(plan_);
}

Magnitudes FingerprintExtractor::computeBandMagnitudes() {
    int band_size = half_size_ / num_bins_;
    if (band_size < 1) band_size = 1;

    Magnitudes magnitudes(num_bins_, 0.0);

    for (int bin = 0; bin < num_bins_; ++bin) {
        double mag_sum = 0.0;
        int start = bin * band_size;
        int end = std::min(start + band_size, half_size_);

        for (int i = start; i < end; ++i) {
            double re = fft_output_[i][0];
            double im = fft_output_[i][1];
            mag_sum += std::sqrt(re * re + im * im);
        }
        magnitudes[bin] = mag_sum / (end - start);
    }

    return magnitudes;
}

std::vector<Peak> FingerprintExtractor::extractPeaks(const Frame& frame, int frame_index,
                                                     bool apply_noise_subtraction) {
    Frame windowed = frame;
    applyHannWindow(windowed);
    computeSpectrum(windowed);

    Magnitudes magnitudes = computeBandMagnitudes();

    if (apply_noise_subtraction && hasNoiseEstimator()) {
        noise_estimator_->subtract(magnitudes);
    }

    std::vector<Peak> peaks;
    findPeaks(magnitudes, peaks, frame_index);
    return peaks;
}

std::vector<Peak> FingerprintExtractor::extractPeaksForNoiseEstimation(const Frame& frame, int frame_index) {
    Frame windowed = frame;
    applyHannWindow(windowed);
    computeSpectrum(windowed);

    Magnitudes magnitudes = computeBandMagnitudes();

    noise_estimator_local_.accumulate(magnitudes);

    std::vector<Peak> peaks;
    findPeaks(magnitudes, peaks, frame_index);
    return peaks;
}

void FingerprintExtractor::findPeaks(Magnitudes& magnitudes, std::vector<Peak>& peaks, int frame_index) {
    if (magnitudes.empty()) return;

    double max_mag = *std::max_element(magnitudes.begin(), magnitudes.end());
    if (max_mag < 1e-10) return;

    double threshold = max_mag * PEAK_THRESHOLD;

    for (int i = 1; i < static_cast<int>(magnitudes.size()) - 1; ++i) {
        if (magnitudes[i] > threshold &&
            magnitudes[i] >= magnitudes[i - 1] &&
            magnitudes[i] >= magnitudes[i + 1]) {
            peaks.push_back({i, magnitudes[i], frame_index});
        }
    }

    if (peaks.size() > 10) {
        std::partial_sort(peaks.begin(), peaks.begin() + 10, peaks.end(),
            [](const Peak& a, const Peak& b) { return a.magnitude > b.magnitude; });
        peaks.resize(10);
    }
}

std::vector<FingerprintHash> FingerprintExtractor::generateHashes(const std::vector<Peak>& peaks) {
    std::vector<FingerprintHash> hashes;
    if (peaks.size() < 2) return hashes;

    for (size_t i = 0; i < peaks.size(); ++i) {
        int fan_count = 0;
        for (size_t j = i + 1; j < peaks.size() && fan_count < FAN_VALUE; ++j) {
            int delta = peaks[j].frame_index - peaks[i].frame_index;

            if (delta >= TARGET_ZONE_START && delta <= TARGET_ZONE_END) {
                FingerprintHash fh;
                fh.hash = computeHash(peaks[i].bin, peaks[j].bin, delta);
                fh.time_delta = peaks[i].frame_index;
                hashes.push_back(fh);
                fan_count++;
            }
        }
    }

    return hashes;
}

uint32_t FingerprintExtractor::computeHash(int bin1, int bin2, int delta) {
    uint32_t hash = 0;
    hash = (static_cast<uint32_t>(bin1) & 0x3F) << 26;
    hash |= (static_cast<uint32_t>(bin2) & 0x3F) << 20;
    hash |= (static_cast<uint32_t>(delta) & 0x0F) << 16;
    hash |= (static_cast<uint32_t>(bin1) ^ static_cast<uint32_t>(bin2) ^ static_cast<uint32_t>(delta)) & 0xFFFF;
    return hash;
}

}
