#pragma once

#include "utils/common.h"
#include "edsr/edsr_model.h"

struct TemporalConv3DConfig {
    int temporal_kernel = 3;
    int spatial_kernel_h = 3;
    int spatial_kernel_w = 3;
    int in_channels = INPUT_CHANNELS;
    int out_channels = INPUT_CHANNELS;
    int num_frames = 3;
    bool use_quantized = true;
};

struct TemporalConv3DWeight {
    std::vector<int8_t> data;
    int in_channels;
    int out_channels;
    int temporal_kernel;
    int spatial_kernel_h;
    int spatial_kernel_w;
    float scale;
    int32_t zero_point;
};

class TemporalFusion {
public:
    TemporalFusion(const TemporalConv3DConfig& config = TemporalConv3DConfig());
    ~TemporalFusion();

    bool loadWeights(const std::vector<int8_t>& weights_data);
    bool hasWeights() const { return weights_loaded_; }

    std::vector<uint8_t> fuse(
        const std::vector<uint8_t>& prev_frame,
        const std::vector<uint8_t>& curr_frame,
        const std::vector<uint8_t>& next_frame,
        int height, int width, int channels);

    cv::Mat fuse(
        const std::vector<cv::Mat>& temporal_volume,
        int height, int width);

    void initDefaultWeights();

    const TemporalConv3DWeight& getWeights() const { return weights_; }
    const TemporalConv3DConfig& getConfig() const { return config_; }

    static size_t calculateWeightSize(const TemporalConv3DConfig& config);

private:
    std::vector<int8_t> conv3dInt8(
        const std::vector<int8_t>& input,
        int T, int H, int W, int C) const;

    TemporalConv3DConfig config_;
    TemporalConv3DWeight weights_;
    bool weights_loaded_ = false;
};