#pragma once

#include "utils/common.h"
#include <opencv2/opencv.hpp>

struct PSNRResult {
    double psnr_y;
    double psnr_u;
    double psnr_v;
    double psnr_rgb;
    double mse;
};

class PSNREvaluator {
public:
    PSNREvaluator();

    PSNRResult compute(const uint8_t* original, const uint8_t* processed,
                       int width, int height, int channels);

    PSNRResult compute(const cv::Mat& original, const cv::Mat& processed);

    double computeMSE(const uint8_t* a, const uint8_t* b, size_t size) const;
    double mseToPSNR(double mse) const;

    void updateAverage(const PSNRResult& result);
    double getAveragePSNR() const { return average_psnr_; }
    uint64_t getNumSamples() const { return num_samples_; }

    void reset();

private:
    double average_psnr_ = 0.0;
    uint64_t num_samples_ = 0;
};