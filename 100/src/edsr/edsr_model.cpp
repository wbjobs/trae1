#include "edsr/edsr_model.h"
#include <random>

EDSRModel::EDSRModel(const EDSRConfig& config)
    : config_(config) {
    weights_.input_conv = { {}, config.in_channels, config.num_features,
                            config.kernel_size, config.kernel_size, 1.0f, 0 };
    weights_.residual_conv1.resize(config.num_residual_blocks);
    weights_.residual_conv2.resize(config.num_residual_blocks);
    for (int i = 0; i < config.num_residual_blocks; ++i) {
        weights_.residual_conv1[i] = { {}, config.num_features, config.num_features,
                                        config.kernel_size, config.kernel_size, 1.0f, 0 };
        weights_.residual_conv2[i] = { {}, config.num_features, config.num_features,
                                        config.kernel_size, config.kernel_size, 1.0f, 0 };
    }
    weights_.middle_conv = { {}, config.num_features, config.num_features,
                              config.kernel_size, config.kernel_size, 1.0f, 0 };
    weights_.upsample_conv = { {}, config.num_features, config.num_features * config.scale * config.scale,
                                config.kernel_size, config.kernel_size, 1.0f, 0 };
    weights_.output_conv = { {}, config.num_features, config.out_channels,
                              config.kernel_size, config.kernel_size, 1.0f, 0 };
}

EDSRModel::~EDSRModel() = default;

void EDSRModel::initDefaultWeights() {
    auto initConv = [](ConvWeight& w) {
        size_t total = static_cast<size_t>(w.in_channels) * w.out_channels *
                       w.kernel_h * w.kernel_w;
        w.data.resize(total);
        float scale = std::sqrt(2.0f / (w.kernel_h * w.kernel_w * w.in_channels));
        std::mt19937 rng(42);
        std::normal_distribution<float> dist(0.0f, scale);
        for (size_t i = 0; i < total; ++i) {
            float val = dist(rng);
            val = std::max(-1.0f, std::min(1.0f, val));
            w.data[i] = static_cast<int8_t>(val * 127.0f);
        }
        w.scale = 1.0f / 127.0f;
        w.zero_point = 0;
    };

    initConv(weights_.input_conv);
    for (int i = 0; i < config_.num_residual_blocks; ++i) {
        initConv(weights_.residual_conv1[i]);
        initConv(weights_.residual_conv2[i]);
    }
    initConv(weights_.middle_conv);
    initConv(weights_.upsample_conv);
    initConv(weights_.output_conv);
    weights_.initialized = true;
}

std::vector<float> EDSRModel::forward(const std::vector<float>& input,
                                       int in_h, int in_w) const {
    if (!weights_.initialized) {
        return {};
    }

    auto features = conv2d(input, weights_.input_conv, in_h, in_w);
    auto x = features;

    for (int i = 0; i < config_.num_residual_blocks; ++i) {
        x = residualBlock(x, weights_.residual_conv1[i], weights_.residual_conv2[i],
                          in_h, in_w);
    }

    x = conv2d(x, weights_.middle_conv, in_h, in_w);

    int feat_h = in_h;
    int feat_w = in_w;
    x = upsample(x, feat_h, feat_w);

    int up_h = in_h * config_.scale;
    int up_w = in_w * config_.scale;
    auto output = conv2d(x, weights_.output_conv, up_h, up_w);

    return output;
}

std::vector<float> EDSRModel::conv2d(const std::vector<float>& input,
                                      const ConvWeight& weight,
                                      int in_h, int in_w) const {
    int out_h = in_h;
    int out_w = in_w;
    int out_channels = weight.out_channels;
    int in_channels = weight.in_channels;
    int kH = weight.kernel_h;
    int kW = weight.kernel_w;
    int pad = config_.padding;

    std::vector<float> output(static_cast<size_t>(out_channels) * out_h * out_w, 0.0f);

    for (int oc = 0; oc < out_channels; ++oc) {
        for (int oh = 0; oh < out_h; ++oh) {
            for (int ow = 0; ow < out_w; ++ow) {
                float sum = 0.0f;
                for (int ic = 0; ic < in_channels; ++ic) {
                    for (int kh = 0; kh < kH; ++kh) {
                        for (int kw = 0; kw < kW; ++kw) {
                            int ih = oh + kh - pad;
                            int iw = ow + kw - pad;
                            if (ih >= 0 && ih < in_h && iw >= 0 && iw < in_w) {
                                float in_val = input[(ic * in_h + ih) * in_w + iw];
                                int8_t w_val = weight.data[((oc * in_channels + ic) * kH + kh) * kW + kw];
                                sum += in_val * static_cast<float>(w_val) * weight.scale;
                            }
                        }
                    }
                }
                output[(oc * out_h + oh) * out_w + ow] = sum;
            }
        }
    }

    return output;
}

std::vector<float> EDSRModel::relu(const std::vector<float>& input) const {
    std::vector<float> output(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        output[i] = std::max(0.0f, input[i]);
    }
    return output;
}

std::vector<float> EDSRModel::residualBlock(const std::vector<float>& input,
                                             const ConvWeight& conv1,
                                             const ConvWeight& conv2,
                                             int h, int w) const {
    auto x = conv2d(input, conv1, h, w);
    x = relu(x);
    x = conv2d(x, conv2, h, w);

    for (size_t i = 0; i < input.size(); ++i) {
        x[i] += input[i];
    }
    return x;
}

std::vector<float> EDSRModel::upsample(const std::vector<float>& input,
                                        int in_h, int in_w) const {
    auto up = conv2d(input, weights_.upsample_conv, in_h, in_w);

    int scale = config_.scale;
    int channels = config_.num_features;
    int out_h = in_h * scale;
    int out_w = in_w * scale;

    std::vector<float> output(static_cast<size_t>(channels) * out_h * out_w, 0.0f);

    for (int c = 0; c < channels; ++c) {
        for (int ih = 0; ih < in_h; ++ih) {
            for (int iw = 0; iw < in_w; ++iw) {
                int src_idx = (c * in_h + ih) * in_w + iw;
                for (int sh = 0; sh < scale; ++sh) {
                    for (int sw = 0; sw < scale; ++sw) {
                        int oh = ih * scale + sh;
                        int ow = iw * scale + sw;
                        int dst_idx = (c * out_h + oh) * out_w + ow;
                        int src_sub_idx = ((c * scale + sh) * scale + sw) * in_h * in_w + src_idx;
                        if (src_sub_idx >= 0 && src_sub_idx < static_cast<int>(up.size())) {
                            output[dst_idx] = up[src_sub_idx];
                        }
                    }
                }
            }
        }
    }

    return output;
}