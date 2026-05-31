#include "motion/temporal_fusion.h"
#include <random>

TemporalFusion::TemporalFusion(const TemporalConv3DConfig& config)
    : config_(config) {
    weights_ = {
        {}, config.in_channels, config.out_channels,
        config.temporal_kernel, config.spatial_kernel_h, config.spatial_kernel_w,
        1.0f / 127.0f, 0
    };
}

TemporalFusion::~TemporalFusion() = default;

size_t TemporalFusion::calculateWeightSize(const TemporalConv3DConfig& config) {
    return static_cast<size_t>(config.in_channels) * config.out_channels *
           config.temporal_kernel * config.spatial_kernel_h * config.spatial_kernel_w;
}

void TemporalFusion::initDefaultWeights() {
    size_t total = calculateWeightSize(config_);
    weights_.data.resize(total);

    std::mt19937 rng(67890);
    std::uniform_int_distribution<int16_t> dist(-127, 127);

    for (size_t i = 0; i < total; ++i) {
        weights_.data[i] = static_cast<int8_t>(dist(rng));
    }

    int center_t = config_.temporal_kernel / 2;
    int center_h = config_.spatial_kernel_h / 2;
    int center_w = config_.spatial_kernel_w / 2;
    int center_idx = ((center_t * config_.spatial_kernel_h + center_h) *
                       config_.spatial_kernel_w + center_w);

    for (int oc = 0; oc < config_.out_channels; ++oc) {
        for (int ic = 0; ic < config_.in_channels; ++ic) {
            if (ic == oc) {
                size_t idx = static_cast<size_t>(oc * config_.in_channels + ic) *
                             config_.temporal_kernel * config_.spatial_kernel_h *
                             config_.spatial_kernel_w + center_idx;
                if (idx < total) {
                    weights_.data[idx] = 127;
                }
            }
        }
    }

    weights_.scale = 1.0f / (127.0f * 3.0f);
    weights_.zero_point = 0;
    weights_loaded_ = true;
}

bool TemporalFusion::loadWeights(const std::vector<int8_t>& weights_data) {
    size_t expected = calculateWeightSize(config_);
    if (weights_data.size() != expected) {
        std::cerr << "[TemporalFusion] Weight size mismatch: expected "
                  << expected << ", got " << weights_data.size() << "\n";
        return false;
    }
    weights_.data = weights_data;
    weights_loaded_ = true;
    return true;
}

std::vector<uint8_t> TemporalFusion::fuse(
    const std::vector<uint8_t>& prev_frame,
    const std::vector<uint8_t>& curr_frame,
    const std::vector<uint8_t>& next_frame,
    int height, int width, int channels) {

    int T = 3;
    int H = height;
    int W = width;
    int C = channels;

    std::vector<int8_t> input(static_cast<size_t>(T) * H * W * C);
    size_t frame_size = static_cast<size_t>(H) * W * C;

    for (int i = 0; i < static_cast<int>(prev_frame.size()) && i < static_cast<int>(frame_size); ++i) {
        input[i] = static_cast<int8_t>(static_cast<int16_t>(prev_frame[i]) - 128);
    }
    for (int i = 0; i < static_cast<int>(curr_frame.size()) && i < static_cast<int>(frame_size); ++i) {
        input[frame_size + i] = static_cast<int8_t>(static_cast<int16_t>(curr_frame[i]) - 128);
    }
    for (int i = 0; i < static_cast<int>(next_frame.size()) && i < static_cast<int>(frame_size); ++i) {
        input[2 * frame_size + i] = static_cast<int8_t>(static_cast<int16_t>(next_frame[i]) - 128);
    }

    auto quant_output = conv3dInt8(input, T, H, W, C);

    std::vector<uint8_t> output(static_cast<size_t>(H) * W * C);
    for (int i = 0; i < static_cast<int>(quant_output.size()) && i < static_cast<int>(output.size()); ++i) {
        int16_t val = static_cast<int16_t>(quant_output[i]) + 128;
        output[i] = static_cast<uint8_t>(std::max(0, std::min(255, val)));
    }

    return output;
}

cv::Mat TemporalFusion::fuse(
    const std::vector<cv::Mat>& temporal_volume,
    int height, int width) {

    if (temporal_volume.size() != 3) {
        return temporal_volume.empty() ? cv::Mat() : temporal_volume[1].clone();
    }

    std::vector<uint8_t> prev_data, curr_data, next_data;
    int channels = temporal_volume[0].channels();

    auto matToVec = [](const cv::Mat& mat) -> std::vector<uint8_t> {
        std::vector<uint8_t> vec(mat.total() * mat.elemSize());
        std::memcpy(vec.data(), mat.data, vec.size());
        return vec;
    };

    prev_data = matToVec(temporal_volume[0]);
    curr_data = matToVec(temporal_volume[1]);
    next_data = matToVec(temporal_volume[2]);

    auto fused = fuse(prev_data, curr_data, next_data, height, width, channels);

    cv::Mat result(height, width, CV_8UC(channels), fused.data());
    return result.clone();
}

std::vector<int8_t> TemporalFusion::conv3dInt8(
    const std::vector<int8_t>& input,
    int T, int H, int W, int C) const {

    if (!weights_loaded_) return {};

    int tK = config_.temporal_kernel;
    int hK = config_.spatial_kernel_h;
    int wK = config_.spatial_kernel_w;
    int pad_t = tK / 2;
    int pad_h = hK / 2;
    int pad_w = wK / 2;
    int out_C = config_.out_channels;

    size_t output_size = static_cast<size_t>(T) * H * W * out_C;
    std::vector<int32_t> accum(output_size, 0);

    for (int oc = 0; oc < out_C; ++oc) {
        for (int t = 0; t < T; ++t) {
            for (int h = 0; h < H; ++h) {
                for (int w = 0; w < W; ++w) {
                    int32_t sum = 0;
                    for (int ic = 0; ic < C; ++ic) {
                        for (int kt = 0; kt < tK; ++kt) {
                            for (int kh = 0; kh < hK; ++kh) {
                                for (int kw = 0; kw < wK; ++kw) {
                                    int it = t + kt - pad_t;
                                    int ih = h + kh - pad_h;
                                    int iw = w + kw - pad_w;
                                    if (it >= 0 && it < T && ih >= 0 && ih < H && iw >= 0 && iw < W) {
                                        size_t in_idx = ((static_cast<size_t>(it) * C + ic) * H + ih) * W + iw;
                                        size_t w_idx = ((((static_cast<size_t>(oc) * C + ic) * tK + kt) * hK + kh) * wK + kw);
                                        int32_t in_val = static_cast<int32_t>(input[in_idx]);
                                        int32_t w_val = static_cast<int32_t>(weights_.data[w_idx]);
                                        sum += in_val * w_val;
                                    }
                                }
                            }
                        }
                    }
                    size_t out_idx = ((static_cast<size_t>(t) * out_C + oc) * H + h) * W + w;
                    accum[out_idx] = sum;
                }
            }
        }
    }

    std::vector<int8_t> output(output_size);
    for (size_t i = 0; i < output_size; ++i) {
        float deq = static_cast<float>(accum[i]) * weights_.scale;
        deq = std::max(-1.0f, std::min(1.0f, deq));
        output[i] = static_cast<int8_t>(deq * 127.0f);
    }

    return output;
}