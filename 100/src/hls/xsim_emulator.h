#pragma once

#include "utils/common.h"

struct HLSModuleConfig {
    int input_h;
    int input_w;
    int output_h;
    int output_w;
    int num_features;
    int num_res_blocks;
    int kernel_size;
    int scale_factor;
    int fpga_freq_mhz;
    int ddr_burst_length;
    int pe_array_size;
    bool use_unrolling;
    bool use_pipelining;
    bool enable_mc;
    int temporal_frames;
    int temporal_kernel;
};

struct CycleBreakdown {
    uint64_t input_load;
    uint64_t temporal_conv;
    uint64_t conv_input;
    uint64_t residual_blocks;
    uint64_t conv_middle;
    uint64_t upsample;
    uint64_t conv_output;
    uint64_t output_store;
    uint64_t total;
};

struct MemoryTraffic {
    uint64_t ddr_reads;
    uint64_t ddr_writes;
    uint64_t bram_reads;
    uint64_t bram_writes;
    uint64_t total_bytes;
};

class XSimEmulator {
public:
    XSimEmulator();
    ~XSimEmulator();

    void configure(const HLSModuleConfig& config);
    void setWeightsLoaded(bool loaded) { weights_loaded_ = loaded; }
    void setMotionCompensation(bool enable) { config_.enable_mc = enable; }

    CycleBreakdown simulateFrameProcessing(int frame_h, int frame_w);
    MemoryTraffic estimateMemoryTraffic(int frame_h, int frame_w);

    double cyclesToMs(uint64_t cycles) const;
    uint64_t msToCycles(double ms) const;

    void printCycleReport(const CycleBreakdown& breakdown) const;
    void printMemoryReport(const MemoryTraffic& traffic) const;

    bool isConfigured() const { return configured_; }
    const HLSModuleConfig& getConfig() const { return config_; }

    static uint64_t estimateConvCycles(int in_h, int in_w, int in_c, int out_c,
                                       int kH, int kW, int pe_array_size);
    static uint64_t estimateConv3DCycles(int T, int H, int W, int in_c, int out_c,
                                          int kT, int kH, int kW, int pe_array_size);
    static uint64_t estimateMemoryCycles(uint64_t bytes, int ddr_width, int burst_length);

private:
    HLSModuleConfig config_;
    bool configured_ = false;
    bool weights_loaded_ = false;

    static constexpr int PE_ARRAY_DEFAULT = 16;
    static constexpr int DDR_BURST_DEFAULT = 256;
    static constexpr int BRAM_LATENCY_CYCLES = 2;
    static constexpr int DDR_LATENCY_CYCLES = 20;
};