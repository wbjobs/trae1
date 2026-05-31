#pragma once

#include "utils/common.h"
#include "edsr/edsr_model.h"
#include "edsr/edsr_quantized.h"
#include "motion/temporal_fusion.h"

class EDSRWeightsLoader {
public:
    static bool loadFromFile(const std::string& path, EDSRWeights& weights);
    static bool saveToFile(const std::string& path, const EDSRWeights& weights);

    static bool loadTemporalWeights(const std::string& path,
                                      TemporalConv3DWeight& temporal_weights,
                                      const TemporalConv3DConfig& config);

    static bool loadFromONNX(const std::string& onnx_path, EDSRWeights& weights);

    static bool exportWeights(const std::vector<float>& raw_weights,
                            const EDSRConfig& config);

    static bool loadRawWeights(const std::string& path,
                                const EDSRConfig& config,
                                EDSRWeights& weights);

    static void generateRandomWeights(EDSRWeights& weights,
                                     const EDSRConfig& config);

    static void generateTemporalWeights(TemporalConv3DWeight& weights,
                                         const TemporalConv3DConfig& config);
};