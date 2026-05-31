#include "edsr/edsr_weights_loader.h"
#include <random>
#include <cstring>

#pragma pack(push, 1)
struct WeightsHeader {
    char magic[4] = {'E', 'D', 'S', 'R'};
    uint32_t version = 1;
    uint32_t in_channels;
    uint32_t out_channels;
    uint32_t num_features;
    uint32_t num_res_blocks;
    uint32_t scale;
    uint32_t kernel_size;
    uint32_t padding;
    uint32_t reserved[8] = {0};
};

struct ConvHeader {
    int32_t in_channels;
    int32_t out_channels;
    int32_t kernel_h;
    int32_t kernel_w;
    float scale;
    int32_t zero_point;
    uint32_t data_size;
};
#pragma pack(pop)

bool EDSRWeightsLoader::loadFromFile(const std::string& path, EDSRWeights& weights) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[WeightsLoader] Failed to open: " << path << "\n";
        return false;
    }

    WeightsHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (std::strncmp(header.magic, "EDSR", 4) != 0) {
        std::cerr << "[WeightsLoader] Invalid magic number\n";
        return false;
    }

    auto readConv = [&](ConvWeight& w) -> bool {
        ConvHeader ch;
        file.read(reinterpret_cast<char*>(&ch), sizeof(ch));
        w.in_channels = ch.in_channels;
        w.out_channels = ch.out_channels;
        w.kernel_h = ch.kernel_h;
        w.kernel_w = ch.kernel_w;
        w.scale = ch.scale;
        w.zero_point = ch.zero_point;
        w.data.resize(ch.data_size);
        if (ch.data_size > 0) {
            file.read(reinterpret_cast<char*>(w.data.data()), ch.data_size);
        }
        return file.good();
    };

    if (!readConv(weights.input_conv)) return false;

    weights.residual_conv1.resize(header.num_res_blocks);
    weights.residual_conv2.resize(header.num_res_blocks);
    for (uint32_t i = 0; i < header.num_res_blocks; ++i) {
        if (!readConv(weights.residual_conv1[i])) return false;
        if (!readConv(weights.residual_conv2[i])) return false;
    }

    if (!readConv(weights.middle_conv)) return false;
    if (!readConv(weights.upsample_conv)) return false;
    if (!readConv(weights.output_conv)) return false;

    weights.initialized = true;
    std::cout << "[WeightsLoader] Loaded weights from: " << path << "\n";
    return true;
}

bool EDSRWeightsLoader::saveToFile(const std::string& path, const EDSRWeights& weights) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[WeightsLoader] Failed to create: " << path << "\n";
        return false;
    }

    WeightsHeader header;
    header.in_channels = weights.input_conv.in_channels;
    header.out_channels = weights.output_conv.out_channels;
    header.num_features = weights.input_conv.out_channels;
    header.num_res_blocks = static_cast<uint32_t>(weights.residual_conv1.size());
    header.scale = weights.upsample_conv.out_channels / weights.upsample_conv.in_channels;
    header.kernel_size = weights.input_conv.kernel_h;
    header.padding = 1;

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    auto writeConv = [&](const ConvWeight& w) {
        ConvHeader ch;
        ch.in_channels = w.in_channels;
        ch.out_channels = w.out_channels;
        ch.kernel_h = w.kernel_h;
        ch.kernel_w = w.kernel_w;
        ch.scale = w.scale;
        ch.zero_point = w.zero_point;
        ch.data_size = static_cast<uint32_t>(w.data.size());
        file.write(reinterpret_cast<const char*>(&ch), sizeof(ch));
        if (ch.data_size > 0) {
            file.write(reinterpret_cast<const char*>(w.data.data()), ch.data_size);
        }
    };

    writeConv(weights.input_conv);
    for (const auto& w : weights.residual_conv1) writeConv(w);
    for (const auto& w : weights.residual_conv2) writeConv(w);
    writeConv(weights.middle_conv);
    writeConv(weights.upsample_conv);
    writeConv(weights.output_conv);

    std::cout << "[WeightsLoader] Saved weights to: " << path << "\n";
    return true;
}

bool EDSRWeightsLoader::loadFromONNX(const std::string& onnx_path, EDSRWeights& weights) {
    std::cerr << "[WeightsLoader] ONNX loading requires ONNX Runtime library.\n"
              << "Please export weights to binary format first.\n"
              << "Use: python export_onnx_weights.py <model.onnx> <output.bin>\n";
    return false;
}

bool EDSRWeightsLoader::loadRawWeights(const std::string& path,
                                         const EDSRConfig& config,
                                         EDSRWeights& weights) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[WeightsLoader] Failed to open raw weights: " << path << "\n";
        return false;
    }

    auto readTensor = [&](int in_c, int out_c, int kH, int kW) -> ConvWeight {
        ConvWeight w;
        w.in_channels = in_c;
        w.out_channels = out_c;
        w.kernel_h = kH;
        w.kernel_w = kW;
        w.scale = 1.0f / 127.0f;
        w.zero_point = 0;
        size_t total = static_cast<size_t>(in_c) * out_c * kH * kW;
        w.data.resize(total);
        file.read(reinterpret_cast<char*>(w.data.data()), total);
        return w;
    };

    weights.input_conv = readTensor(config.in_channels, config.num_features,
                                     config.kernel_size, config.kernel_size);
    weights.residual_conv1.resize(config.num_residual_blocks);
    weights.residual_conv2.resize(config.num_residual_blocks);
    for (int i = 0; i < config.num_residual_blocks; ++i) {
        weights.residual_conv1[i] = readTensor(config.num_features, config.num_features,
                                                config.kernel_size, config.kernel_size);
        weights.residual_conv2[i] = readTensor(config.num_features, config.num_features,
                                                config.kernel_size, config.kernel_size);
    }
    weights.middle_conv = readTensor(config.num_features, config.num_features,
                                      config.kernel_size, config.kernel_size);
    weights.upsample_conv = readTensor(config.num_features,
                                        config.num_features * config.scale * config.scale,
                                        config.kernel_size, config.kernel_size);
    weights.output_conv = readTensor(config.num_features, config.out_channels,
                                      config.kernel_size, config.kernel_size);

    weights.initialized = file.good();
    return weights.initialized;
}

void EDSRWeightsLoader::generateRandomWeights(EDSRWeights& weights,
                                               const EDSRConfig& config) {
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int16_t> dist(-127, 127);

    auto genConv = [&](int in_c, int out_c, int kH, int kW) -> ConvWeight {
        ConvWeight w;
        w.in_channels = in_c;
        w.out_channels = out_c;
        w.kernel_h = kH;
        w.kernel_w = kW;
        w.scale = 1.0f / 127.0f;
        w.zero_point = 0;
        size_t total = static_cast<size_t>(in_c) * out_c * kH * kW;
        w.data.resize(total);
        for (size_t i = 0; i < total; ++i) {
            w.data[i] = static_cast<int8_t>(dist(rng));
        }
        return w;
    };

    weights.input_conv = genConv(config.in_channels, config.num_features,
                                  config.kernel_size, config.kernel_size);
    weights.residual_conv1.resize(config.num_residual_blocks);
    weights.residual_conv2.resize(config.num_residual_blocks);
    for (int i = 0; i < config.num_residual_blocks; ++i) {
        weights.residual_conv1[i] = genConv(config.num_features, config.num_features,
                                             config.kernel_size, config.kernel_size);
        weights.residual_conv2[i] = genConv(config.num_features, config.num_features,
                                             config.kernel_size, config.kernel_size);
    }
    weights.middle_conv = genConv(config.num_features, config.num_features,
                                    config.kernel_size, config.kernel_size);
    weights.upsample_conv = genConv(config.num_features,
                                     config.num_features * config.scale * config.scale,
                                     config.kernel_size, config.kernel_size);
    weights.output_conv = genConv(config.num_features, config.out_channels,
                                    config.kernel_size, config.kernel_size);
    weights.initialized = true;
}

bool EDSRWeightsLoader::loadTemporalWeights(const std::string& path,
                                              TemporalConv3DWeight& temporal_weights,
                                              const TemporalConv3DConfig& config) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[WeightsLoader] Failed to open temporal weights: " << path << "\n";
        return false;
    }

    char magic[4];
    file.read(magic, 4);
    if (std::strncmp(magic, "EDSR", 4) != 0) {
        std::cerr << "[WeightsLoader] Invalid magic number for temporal weights\n";
        return false;
    }

    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));

    file.seekg(0, std::ios::end);
    std::streampos file_size = file.tellg();
    file.seekg(4 + sizeof(uint32_t), std::ios::beg);

    for (int i = 0; i < 7; ++i) {
        ConvHeader ch;
        file.read(reinterpret_cast<char*>(&ch), sizeof(ch));
        file.seekg(ch.data_size, std::ios::cur);
    }

    ConvHeader temporal_header;
    if (file.read(reinterpret_cast<char*>(&temporal_header), sizeof(temporal_header))) {
        temporal_weights.in_channels = temporal_header.in_channels;
        temporal_weights.out_channels = temporal_header.out_channels;
        temporal_weights.temporal_kernel = temporal_header.kernel_h;
        temporal_weights.spatial_kernel_h = temporal_header.kernel_w;
        temporal_weights.spatial_kernel_w = 3;
        temporal_weights.scale = temporal_header.scale;
        temporal_weights.zero_point = temporal_header.zero_point;
        temporal_weights.data.resize(temporal_header.data_size);
        if (temporal_header.data_size > 0) {
            file.read(reinterpret_cast<char*>(temporal_weights.data.data()),
                     temporal_header.data_size);
        }
        std::cout << "[WeightsLoader] Loaded temporal weights: "
                  << temporal_weights.in_channels << "->" << temporal_weights.out_channels
                  << " kernel: " << temporal_weights.temporal_kernel << "x"
                  << temporal_weights.spatial_kernel_h << "x"
                  << temporal_weights.spatial_kernel_w << "\n";
        return true;
    }

    std::cout << "[WeightsLoader] Temporal weights not found in file, will generate defaults\n";
    return false;
}

void EDSRWeightsLoader::generateTemporalWeights(TemporalConv3DWeight& weights,
                                                 const TemporalConv3DConfig& config) {
    std::mt19937 rng(67890);
    std::uniform_int_distribution<int16_t> dist(-127, 127);

    weights.in_channels = config.in_channels;
    weights.out_channels = config.out_channels;
    weights.temporal_kernel = config.temporal_kernel;
    weights.spatial_kernel_h = config.spatial_kernel_h;
    weights.spatial_kernel_w = config.spatial_kernel_w;
    weights.scale = 1.0f / (127.0f * 3.0f);
    weights.zero_point = 0;

    size_t total = static_cast<size_t>(config.in_channels) * config.out_channels *
                   config.temporal_kernel * config.spatial_kernel_h * config.spatial_kernel_w;
    weights.data.resize(total);

    for (size_t i = 0; i < total; ++i) {
        weights.data[i] = static_cast<int8_t>(dist(rng));
    }

    int center_t = config.temporal_kernel / 2;
    int center_h = config.spatial_kernel_h / 2;
    int center_w = config.spatial_kernel_w / 2;
    int center_idx = ((center_t * config.spatial_kernel_h + center_h) *
                       config.spatial_kernel_w + center_w);

    for (int oc = 0; oc < config.out_channels; ++oc) {
        for (int ic = 0; ic < config.in_channels; ++ic) {
            if (ic == oc) {
                size_t idx = static_cast<size_t>(oc * config.in_channels + ic) *
                             config.temporal_kernel * config.spatial_kernel_h *
                             config.spatial_kernel_w + center_idx;
                if (idx < total) {
                    weights.data[idx] = 127;
                }
            }
        }
    }

    std::cout << "[WeightsLoader] Generated temporal weights: "
              << config.in_channels << "->" << config.out_channels
              << " kernel: " << config.temporal_kernel << "x"
              << config.spatial_kernel_h << "x"
              << config.spatial_kernel_w
              << " (" << total << " params)\n";
}