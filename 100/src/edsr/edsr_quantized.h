#pragma once

#include "utils/common.h"
#include "edsr/edsr_model.h"

class EDSRQuantized {
public:
    EDSRQuantized(const EDSRConfig& config = EDSRConfig());
    ~EDSRQuantized();

    bool loadWeights(const std::string& path);
    bool hasWeights() const { return weights_.initialized; }

    std::vector<int8_t> forward(const std::vector<int8_t>& input,
                                 int in_h, int in_w) const;

    std::vector<uint8_t> processFrame(const uint8_t* frame_data,
                                       int in_h, int in_w,
                                       int out_h, int out_w) const;

    void initFromModel(const EDSRModel& model);

    const EDSRConfig& getConfig() const { return config_; }
    const EDSRWeights& getWeights() const { return weights_; }

private:
    std::vector<int8_t> conv2dInt8(const std::vector<int8_t>& input,
                                    const ConvWeight& weight,
                                    int in_h, int in_w,
                                    float input_scale,
                                    int32_t input_zp) const;

    std::vector<int8_t> reluInt8(const std::vector<int8_t>& input,
                                  float scale, int32_t zp) const;

    std::vector<int8_t> residualBlockInt8(const std::vector<int8_t>& input,
                                           const ConvWeight& conv1,
                                           const ConvWeight& conv2,
                                           int h, int w,
                                           float in_scale, int32_t in_zp) const;

    std::vector<int8_t> upsampleInt8(const std::vector<int8_t>& input,
                                      int in_h, int in_w,
                                      float in_scale, int32_t in_zp) const;

    std::vector<int8_t> quantize(const std::vector<float>& input,
                                  float scale, int32_t zp) const;

    std::vector<float> dequantize(const std::vector<int8_t>& input,
                                   float scale, int32_t zp) const;

    static constexpr float ACTIVATION_SCALE = 1.0f / 127.0f;
    static constexpr int32_t ACTIVATION_ZP = 0;

    EDSRConfig config_;
    EDSRWeights weights_;
};