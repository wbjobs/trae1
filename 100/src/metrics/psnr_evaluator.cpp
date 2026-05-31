#include "metrics/psnr_evaluator.h"
#include <limits>

PSNREvaluator::PSNREvaluator() = default;

PSNRResult PSNREvaluator::compute(const uint8_t* original, const uint8_t* processed,
                                    int width, int height, int channels) {
    PSNRResult result = {};
    size_t total_size = static_cast<size_t>(width) * height * channels;

    result.mse = computeMSE(original, processed, total_size);
    result.psnr_rgb = mseToPSNR(result.mse);

    if (channels >= 3) {
        std::vector<uint8_t> orig_y(total_size / 3);
        std::vector<uint8_t> proc_y(total_size / 3);
        std::vector<uint8_t> orig_u(total_size / 3);
        std::vector<uint8_t> proc_u(total_size / 3);
        std::vector<uint8_t> orig_v(total_size / 3);
        std::vector<uint8_t> proc_v(total_size / 3);

        size_t y_idx = 0;
        for (size_t i = 0; i < total_size; i += 3) {
            int r = original[i];
            int g = original[i + 1];
            int b = original[i + 2];
            orig_y[y_idx] = static_cast<uint8_t>((77 * r + 150 * g + 29 * b) >> 8);
            orig_u[y_idx] = static_cast<uint8_t>((-44 * r - 87 * g + 131 * b + 256 * 128) >> 8);
            orig_v[y_idx] = static_cast<uint8_t>((131 * r - 110 * g - 21 * b + 256 * 128) >> 8);

            r = processed[i];
            g = processed[i + 1];
            b = processed[i + 2];
            proc_y[y_idx] = static_cast<uint8_t>((77 * r + 150 * g + 29 * b) >> 8);
            proc_u[y_idx] = static_cast<uint8_t>((-44 * r - 87 * g + 131 * b + 256 * 128) >> 8);
            proc_v[y_idx] = static_cast<uint8_t>((131 * r - 110 * g - 21 * b + 256 * 128) >> 8);
            y_idx++;
        }

        size_t plane_size = total_size / 3;
        result.psnr_y = mseToPSNR(computeMSE(orig_y.data(), proc_y.data(), plane_size));
        result.psnr_u = mseToPSNR(computeMSE(orig_u.data(), proc_u.data(), plane_size));
        result.psnr_v = mseToPSNR(computeMSE(orig_v.data(), proc_v.data(), plane_size));
    }

    return result;
}

PSNRResult PSNREvaluator::compute(const cv::Mat& original, const cv::Mat& processed) {
    CV_Assert(original.size() == processed.size());
    CV_Assert(original.type() == processed.type());

    cv::Mat orig_rgb, proc_rgb;
    if (original.channels() == 1) {
        cv::cvtColor(original, orig_rgb, cv::COLOR_GRAY2RGB);
        cv::cvtColor(processed, proc_rgb, cv::COLOR_GRAY2RGB);
    } else if (original.channels() == 4) {
        cv::cvtColor(original, orig_rgb, cv::COLOR_BGRA2RGB);
        cv::cvtColor(processed, proc_rgb, cv::COLOR_BGRA2RGB);
    } else {
        orig_rgb = original.clone();
        proc_rgb = processed.clone();
    }

    return compute(orig_rgb.data, proc_rgb.data,
                   orig_rgb.cols, orig_rgb.rows, orig_rgb.channels());
}

double PSNREvaluator::computeMSE(const uint8_t* a, const uint8_t* b, size_t size) const {
    if (size == 0) return 0.0;

    double sum = 0.0;
    for (size_t i = 0; i < size; ++i) {
        double diff = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum += diff * diff;
    }
    return sum / size;
}

double PSNREvaluator::mseToPSNR(double mse) const {
    if (mse <= 0.0) return std::numeric_limits<double>::infinity();
    return 10.0 * log10((255.0 * 255.0) / mse);
}

void PSNREvaluator::updateAverage(const PSNRResult& result) {
    if (std::isfinite(result.psnr_rgb)) {
        average_psnr_ = (average_psnr_ * num_samples_ + result.psnr_rgb) / (num_samples_ + 1);
        num_samples_++;
    }
}

void PSNREvaluator::reset() {
    average_psnr_ = 0.0;
    num_samples_ = 0;
}