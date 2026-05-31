#pragma once

#include "common.hpp"
#include <fftw3.h>
#include <memory>
#include <unordered_map>

namespace afp {

class FingerprintExtractor {
public:
    FingerprintExtractor(int frame_size = FRAME_SIZE, int num_bins = FINGERPRINT_BINS);
    ~FingerprintExtractor();

    FingerprintExtractor(const FingerprintExtractor&) = delete;
    FingerprintExtractor& operator=(const FingerprintExtractor&) = delete;

    std::vector<Peak> extractPeaks(const Frame& frame, int frame_index,
                                   bool apply_noise_subtraction = true);

    std::vector<Peak> extractPeaksForNoiseEstimation(const Frame& frame, int frame_index);

    std::vector<FingerprintHash> generateHashes(const std::vector<Peak>& peaks);

    static uint32_t computeHash(int bin1, int bin2, int delta);

    void setNoiseEstimator(const NoiseEstimator* estimator) {
        noise_estimator_ = estimator;
    }

    bool hasNoiseEstimator() const { return noise_estimator_ != nullptr && noise_estimator_->ready; }

    NoiseEstimator& getNoiseEstimator() { return noise_estimator_local_; }
    const NoiseEstimator& getNoiseEstimator() const { return noise_estimator_local_; }

    int getNumBins() const { return num_bins_; }

private:
    void computeSpectrum(const Frame& frame);
    void findPeaks(Magnitudes& magnitudes, std::vector<Peak>& peaks, int frame_index);
    void applyHannWindow(Frame& frame);
    Magnitudes computeBandMagnitudes();

    int frame_size_;
    int num_bins_;
    int half_size_;

    fftw_plan plan_;
    double* fft_input_;
    fftw_complex* fft_output_;

    Frame window_;

    const NoiseEstimator* noise_estimator_ = nullptr;
    NoiseEstimator noise_estimator_local_;
};

}
