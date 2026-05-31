#include "edsr/edsr_quantized.h"
#include <random>

EDSRQuantized::EDSRQuantized(const EDSRConfig& config)
    : config_(config) {
    weights_.input_conv = { {}, config.in_channels, config.num_features,
                            config.kernel_size, config.kernel_size, 1.0f / 127.0f, 0 };
    weights_.residual_conv1.resize(config.num_residual_blocks);
    weights_.residual_conv2.resize(config.num_residual_blocks);
    for (int i = 0; i < config.num_residual_blocks; ++i) {
        weights_.residual_conv1[i] = { {}, config.num_features, config.num_features,
                                        config.kernel_size, config.kernel_size, 1.0f / 127.0f, 0 };
        weights_.residual_conv2[i] = { {}, config.num_features, config.num_features,
                                        config.kernel_size, config.kernel_size, 1.0f / 127.0f, 0 };
    }
    weights_.middle_conv = { {}, config.num_features, config.num_features,
                              config.kernel_size, config.kernel_size, 1.0f / 127.0f, 0 };
    weights_.upsample_conv = { {}, config.num_features, config.num_features * config.scale * config.scale,
                                config.kernel_size, config.kernel_size, 1.0f / 127.0f, 0 };
    weights_.output_conv = { {}, config.num_features, config.out_channels,
                              config.kernel_size, config.kernel_size, 1.0f / 127.0f, 0 };
}

EDSRQuantized::~EDSRQuantized() = default;

void EDSRQuantized::initFromModel(const EDSRModel& model) {
    const auto& srcWeights = model.getWeights();
    weights_.input_conv = srcWeights.input_conv;
    weights_.residual_conv1 = srcWeights.residual_conv1;
    weights_.residual_conv2 = srcWeights.residual_conv2;
    weights_.middle_conv = srcWeights.middle_conv;
    weights_.upsample_conv = srcWeights.upsample_conv;
    weights_.output_conv = srcWeights.output_conv;
    weights_.initialized = srcWeights.initialized;
}

std::vector<int8_t> EDSRQuantized::forward(const std::vector<int8_t>& input,
                                            int in_h, int in_w) const {
    if (!weights_.initialized) {
        return {};
    }

    float in_scale = ACTIVATION_SCALE;
    int32_t in_zp = ACTIVATION_ZP;

    auto features = conv2dInt8(input, weights_.input_conv, in_h, in_w, in_scale, in_zp);
    auto x = features;

    for (int i = 0; i < config_.num_residual_blocks; ++i) {
        x = residualBlockInt8(x, weights_.residual_conv1[i], weights_.residual_conv2[i],
                              in_h, in_w, in_scale, in_zp);
    }

    x = conv2dInt8(x, weights_.middle_conv, in_h, in_w, in_scale, in_zp);

    x = upsampleInt8(x, in_h, in_w, in_scale, in_zp);

    int up_h = in_h * config_.scale;
    int up_w = in_w * config_.scale;
    auto output = conv2dInt8(x, weights_.output_conv, up_h, up_w, in_scale, in_zp);

    return output;
}

std::vector<uint8_t> EDSRQuantized::processFrame(const uint8_t* frame_data,
                                                   int in_h, int in_w,
                                                   int out_h, int out_w) const {
    size_t input_size = static_cast<size_t>(in_h) * in_w * config_.in_channels;
    std::vector<int8_t> quant_input(input_size);
    for (size_t i = 0; i < input_size; ++i) {
        quant_input[i] = static_cast<int8_t>(static_cast<int16_t>(frame_data[i]) - 128);
    }

    auto quant_output = forward(quant_input, in_h, in_w);

    size_t output_size = static_cast<size_t>(out_h) * out_w * config_.out_channels;
    std::vector<uint8_t> output(output_size);
    for (size_t i = 0; i < output_size && i < quant_output.size(); ++i) {
        int16_t val = static_cast<int16_t>(quant_output[i]) + 128;
        output[i] = static_cast<uint8_t>(std::max(0, std::min(255, val)));
    }

    return output;
}

std::vector<int8_t> EDSRQuantized::conv2dInt8(const std::vector<int8_t>& input,
                                               const ConvWeight& weight,
                                               int in_h, int in_w,
                                               float input_scale,
                                               int32_t input_zp) const {
    int out_h = in_h;
    int out_w = in_w;
    int out_channels = weight.out_channels;
    int in_channels = weight.in_channels;
    int kH = weight.kernel_h;
    int kW = weight.kernel_w;
    int pad = config_.padding;

    std::vector<int32_t> accum(static_cast<size_t>(out_channels) * out_h * out_w, 0);

    for (int oc = 0; oc < out_channels; ++oc) {
        for (int oh = 0; oh < out_h; ++oh) {
            for (int ow = 0; ow < out_w; ++ow) {
                int32_t sum = 0;
                for (int ic = 0; ic < in_channels; ++ic) {
                    for (int kh = 0; kh < kH; ++kh) {
                        for (int kw = 0; kw < kW; ++kw) {
                            int ih = oh + kh - pad;
                            int iw = ow + kw - pad;
                            if (ih >= 0 && ih < in_h && iw >= 0 && iw < in_w) {
                                int32_t in_val = static_cast<int32_t>(
                                    input[(ic * in_h + ih) * in_w + iw]) - input_zp;
                                int32_t w_val = static_cast<int32_t>(
                                    weight.data[((oc * in_channels + ic) * kH + kh) * kW + kw]);
                                sum += in_val * w_val;
                            }
                        }
                    }
                }
                accum[(oc * out_h + oh) * out_w + ow] = sum;
            }
        }
    }

    float output_scale = input_scale * weight.scale;
    std::vector<int8_t> output(accum.size());
    for (size_t i = 0; i < accum.size(); ++i) {
        float deq = static_cast<float>(accum[i]) * output_scale;
        deq = std::max(-1.0f, std::min(1.0f, deq));
        output[i] = static_cast<int8_t>(deq * 127.0f);
    }

    return output;
}

std::vector<int8_t> EDSRQuantized::reluInt8(const std::vector<int8_t>& input,
                                             float scale, int32_t zp) const {
    std::vector<int8_t> output(input.size());
    int8_t threshold = static_cast<int8_t>(zp);
    for (size_t i = 0; i < input.size(); ++i) {
        output[i] = std::max(input[i], threshold);
    }
    return output;
}

std::vector<int8_t> EDSRQuantized::residualBlockInt8(const std::vector<int8_t>& input,
                                                       const ConvWeight& conv1,
                                                       const ConvWeight& conv2,
                                                       int h, int w,
                                                       float in_scale, int32_t in_zp) const {
    auto x = conv2dInt8(input, conv1, h, w, in_scale, in_zp);
    x = reluInt8(x, in_scale, in_zp);
    x = conv2dInt8(x, conv2, h, w, in_scale, in_zp);

    for (size_t i = 0; i < input.size() && i < x.size(); ++i) {
        int16_t sum = static_cast<int16_t>(input[i]) + static_cast<int16_t>(x[i]);
        sum = std::max<int16_t>(-128, std::min<int16_t>(127, sum));
        x[i] = static_cast<int8_t>(sum);
    }
    return x;
}

std::vector<int8_t> EDSRQuantized::upsampleInt8(const std::vector<int8_t>& input,
                                                  int in_h, int in_w,
                                                  float in_scale, int32_t in_zp) const {
    auto up = conv2dInt8(input, weights_.upsample_conv, in_h, in_w, in_scale, in_zp);

    int scale = config_.scale;
    int channels = config_.num_features;
    int out_h = in_h * scale;
    int out_w = in_w * scale;

    std::vector<int8_t> output(static_cast<size_t>(channels) * out_h * out_w, 0);

    for (int c = 0; c < channels; ++c) {
        for (int ih = 0; ih < in_h; ++ih) {
            for (int iw = 0; iw < in_w; ++iw) {
                for (int sh = 0; sh < scale; ++sh) {
                    for (int sw = 0; sw < scale; ++sw) {
                        int oh = ih * scale + sh;
                        int ow = iw * scale + sw;
                        int sub_c = c * scale * scale + sh * scale + sw;
                        int src_idx = (sub_c * in_h + ih) * in_w + iw;
                        int dst_idx = (c * out_h + oh) * out_w + ow;
                        if (src_idx >= 0 && src_idx < static_cast<int>(up.size()) &&
                            dst_idx >= 0 && dst_idx < static_cast<int>(output.size())) {
                            output[dst_idx] = up[src_idx];
                        }
                    }
                }
            }
        }
    }

    return output;
}

std::vector<int8_t> EDSRQuantized::quantize(const std::vector<float>& input,
                                              float scale, int32_t zp) const {
    std::vector<int8_t> output(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        float val = std::max(-1.0f, std::min(1.0f, input[i]));
        int32_t q = static_cast<int32_t>(std::round(val / scale)) + zp;
        q = std::max(QUANT_MIN, std::min(QUANT_MAX, q));
        output[i] = static_cast<int8_t>(q);
    }
    return output;
}

std::vector<float> EDSRQuantized::dequantize(const std::vector<int8_t>& input,
                                               float scale, int32_t zp) const {
    std::vector<float> output(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        output[i] = (static_cast<float>(input[i]) - static_cast<float>(zp)) * scale;
    }
    return output;
}