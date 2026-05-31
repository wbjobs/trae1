#pragma once

#include "utils/common.h"
#include "edsr/edsr_quantized.h"
#include "hls/xsim_emulator.h"

struct HLSPerformanceInfo {
    uint64_t total_cycles;
    uint64_t conv_cycles;
    uint64_t memory_cycles;
    uint64_t overhead_cycles;
    double estimated_latency_ms;
    double estimated_throughput_fps;
    int fpga_frequency_mhz;
    bool meets_requirements;
};

class HLSWrapper {
public:
    HLSWrapper(const EDSRConfig& config = EDSRConfig());
    ~HLSWrapper();

    bool loadWeights(const std::string& path);
    bool hasWeights() const { return quant_model_.hasWeights(); }

    std::vector<uint8_t> processFrame(const uint8_t* input,
                                        int in_h, int in_w,
                                        int out_h, int out_w);

    const HLSPerformanceInfo& getPerformanceInfo() const { return perf_info_; }
    void printPerformanceReport() const;

    void setFPGAConfiguration(int freq_mhz, int ddr_width_bytes);

    XSimEmulator& getEmulator() { return emulator_; }

private:
    void estimatePerformance(int in_h, int in_w);
    void runSoftwareEmulation(const uint8_t* input,
                               std::vector<uint8_t>& output,
                               int in_h, int in_w,
                               int out_h, int out_w);

    EDSRConfig config_;
    EDSRQuantized quant_model_;
    XSimEmulator emulator_;
    HLSPerformanceInfo perf_info_;
    int fpga_freq_mhz_ = 300;
    int ddr_width_bytes_ = 16;
};