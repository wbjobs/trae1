#pragma once

#include "utils/common.h"

struct EDSRConfig {
    int in_channels = INPUT_CHANNELS;
    int out_channels = OUTPUT_CHANNELS;
    int num_features = EDSR_NUM_FEATURES;
    int num_residual_blocks = EDSR_NUM_RESIDUAL_BLOCKS;
    int scale = EDSR_SCALE_FACTOR;
    int kernel_size = EDSR_KERNEL_SIZE;
    int padding = EDSR_PADDING;
};

struct ConvWeight {
    std::vector<int8_t> data;
    int in_channels;
    int out_channels;
    int kernel_h;
    int kernel_w;
    float scale;
    int32_t zero_point;
};

struct EDSRWeights {
    ConvWeight input_conv;
    std::vector<ConvWeight> residual_conv1;
    std::vector<ConvWeight> residual_conv2;
    ConvWeight middle_conv;
    ConvWeight upsample_conv;
    ConvWeight output_conv;
    bool initialized = false;
};

class EDSRModel {
public:
    EDSRModel(const EDSRConfig& config = EDSRConfig());
    ~EDSRModel();

    bool loadWeights(const std::string& path);
    bool hasWeights() const { return weights_.initialized; }

    std::vector<float> forward(const std::vector<float>& input,
                                int in_h, int in_w) const;

    void initDefaultWeights();

    const EDSRConfig& getConfig() const { return config_; }
    const EDSRWeights& getWeights() const { return weights_; }

private:
    std::vector<float> conv2d(const std::vector<float>& input,
                               const ConvWeight& weight,
                               int in_h, int in_w) const;

    std::vector<float> relu(const std::vector<float>& input) const;

    std::vector<float> residualBlock(const std::vector<float>& input,
                                      const ConvWeight& conv1,
                                      const ConvWeight& conv2,
                                      int h, int w) const;

    std::vector<float> upsample(const std::vector<float>& input,
                                 int in_h, int in_w) const;

    EDSRConfig config_;
    EDSRWeights weights_;
};