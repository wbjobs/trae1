#include "hls/xsim_emulator.h"
#include <iomanip>

XSimEmulator::XSimEmulator() {
    config_ = { INPUT_HEIGHT, INPUT_WIDTH, OUTPUT_HEIGHT, OUTPUT_WIDTH,
                EDSR_NUM_FEATURES, EDSR_NUM_RESIDUAL_BLOCKS, EDSR_KERNEL_SIZE,
                EDSR_SCALE_FACTOR, 300, DDR_BURST_DEFAULT, PE_ARRAY_DEFAULT,
                true, true, false, TEMPORAL_WINDOW_SIZE, TEMPORAL_KERNEL };
    configured_ = true;
}

XSimEmulator::~XSimEmulator() = default;

void XSimEmulator::configure(const HLSModuleConfig& config) {
    config_ = config;
    configured_ = true;
}

uint64_t XSimEmulator::estimateConvCycles(int in_h, int in_w, int in_c, int out_c,
                                            int kH, int kW, int pe_array_size) {
    uint64_t total_macs = static_cast<uint64_t>(in_h) * in_w * out_c * in_c * kH * kW;
    int effective_pes = std::max(1, pe_array_size);
    if (effective_pes > 0 && total_macs > 0) {
        uint64_t base_cycles = (total_macs + effective_pes - 1) / effective_pes;
        uint64_t pipeline_overhead = static_cast<uint64_t>(in_h) * in_w;
        return base_cycles + pipeline_overhead + kH * kW;
    }
    return 0;
}

uint64_t XSimEmulator::estimateConv3DCycles(int T, int H, int W, int in_c, int out_c,
                                              int kT, int kH, int kW, int pe_array_size) {
    uint64_t total_macs = static_cast<uint64_t>(T) * H * W * out_c * in_c * kT * kH * kW;
    int effective_pes = std::max(1, pe_array_size);
    if (effective_pes > 0 && total_macs > 0) {
        uint64_t base_cycles = (total_macs + effective_pes - 1) / effective_pes;
        uint64_t pipeline_overhead = static_cast<uint64_t>(T) * H * W;
        return base_cycles + pipeline_overhead + kT * kH * kW;
    }
    return 0;
}

uint64_t XSimEmulator::estimateMemoryCycles(uint64_t bytes, int ddr_width, int burst_length) {
    if (ddr_width <= 0) ddr_width = 16;
    if (burst_length <= 0) burst_length = DDR_BURST_DEFAULT;
    uint64_t bursts = (bytes + burst_length - 1) / burst_length;
    uint64_t overhead = bursts * (DDR_LATENCY_CYCLES + BRAM_LATENCY_CYCLES);
    uint64_t transfer = (bytes + ddr_width - 1) / ddr_width;
    return overhead + transfer;
}

CycleBreakdown XSimEmulator::simulateFrameProcessing(int frame_h, int frame_w) {
    CycleBreakdown breakdown = {};

    int in_c = 3;
    int feat = config_.num_features;
    int kH = config_.kernel_size;
    int kW = config_.kernel_size;
    int scale = config_.scale_factor;
    int num_blocks = config_.num_res_blocks;

    if (config_.enable_mc) {
        breakdown.input_load = estimateMemoryCycles(
            static_cast<uint64_t>(config_.temporal_frames) * frame_h * frame_w * in_c,
            16, config_.ddr_burst_length);

        breakdown.temporal_conv = estimateConv3DCycles(
            config_.temporal_frames, frame_h, frame_w, in_c, in_c,
            config_.temporal_kernel, kH, kW, config_.pe_array_size);
    } else {
        breakdown.input_load = estimateMemoryCycles(
            static_cast<uint64_t>(frame_h) * frame_w * in_c,
            16, config_.ddr_burst_length);
        breakdown.temporal_conv = 0;
    }

    breakdown.conv_input = estimateConvCycles(frame_h, frame_w, in_c, feat, kH, kW,
                                               config_.pe_array_size);

    breakdown.residual_blocks = 0;
    for (int i = 0; i < num_blocks; ++i) {
        uint64_t block_cycles = estimateConvCycles(frame_h, frame_w, feat, feat, kH, kW,
                                                    config_.pe_array_size);
        block_cycles += estimateConvCycles(frame_h, frame_w, feat, feat, kH, kW,
                                            config_.pe_array_size);
        breakdown.residual_blocks += block_cycles;
    }

    breakdown.conv_middle = estimateConvCycles(frame_h, frame_w, feat, feat, kH, kW,
                                                config_.pe_array_size);

    int up_h = frame_h * scale;
    int up_w = frame_w * scale;
    breakdown.upsample = estimateConvCycles(frame_h, frame_w, feat, feat * scale * scale,
                                             kH, kW, config_.pe_array_size);

    breakdown.conv_output = estimateConvCycles(up_h, up_w, feat, in_c, kH, kW,
                                                config_.pe_array_size);

    breakdown.output_store = estimateMemoryCycles(
        static_cast<uint64_t>(up_h) * up_w * in_c,
        16, config_.ddr_burst_length);

    breakdown.total = breakdown.input_load + breakdown.temporal_conv +
                      breakdown.conv_input + breakdown.residual_blocks +
                      breakdown.conv_middle + breakdown.upsample +
                      breakdown.conv_output + breakdown.output_store;

    return breakdown;
}

MemoryTraffic XSimEmulator::estimateMemoryTraffic(int frame_h, int frame_w) {
    MemoryTraffic traffic = {};
    int in_c = 3;
    int feat = config_.num_features;
    int scale = config_.scale_factor;
    int up_h = frame_h * scale;
    int up_w = frame_w * scale;

    if (config_.enable_mc) {
        traffic.ddr_reads = static_cast<uint64_t>(config_.temporal_frames) * frame_h * frame_w * in_c;
    } else {
        traffic.ddr_reads = static_cast<uint64_t>(frame_h) * frame_w * in_c;
    }
    traffic.ddr_writes = static_cast<uint64_t>(up_h) * up_w * in_c;

    traffic.bram_reads = traffic.ddr_reads * (config_.num_res_blocks + 2);
    traffic.bram_writes = traffic.ddr_reads * (config_.num_res_blocks + 1);

    traffic.total_bytes = traffic.ddr_reads + traffic.ddr_writes +
                          traffic.bram_reads + traffic.bram_writes;

    return traffic;
}

double XSimEmulator::cyclesToMs(uint64_t cycles) const {
    if (config_.fpga_freq_mhz <= 0) return 0.0;
    return static_cast<double>(cycles) / static_cast<double>(config_.fpga_freq_mhz) / 1000.0;
}

uint64_t XSimEmulator::msToCycles(double ms) const {
    return static_cast<uint64_t>(ms * config_.fpga_freq_mhz * 1000.0);
}

void XSimEmulator::printCycleReport(const CycleBreakdown& breakdown) const {
    std::cout << "\n=== XSim Cycle Report (FPGA @" << config_.fpga_freq_mhz << "MHz)";
    if (config_.enable_mc) {
        std::cout << " + Motion Compensation";
    }
    std::cout << "\n";
    std::cout << std::string(60, '-') << "\n";
    std::cout << "  Input Load:        " << std::setw(12) << breakdown.input_load << " cycles ("
              << std::fixed << std::setprecision(2) << cyclesToMs(breakdown.input_load) << " ms)\n";
    if (config_.enable_mc) {
        std::cout << "  Temporal Conv:     " << std::setw(12) << breakdown.temporal_conv << " cycles ("
                  << std::fixed << std::setprecision(2) << cyclesToMs(breakdown.temporal_conv) << " ms)\n";
    }
    std::cout << "  Conv Input:        " << std::setw(12) << breakdown.conv_input << " cycles ("
              << std::fixed << std::setprecision(2) << cyclesToMs(breakdown.conv_input) << " ms)\n";
    std::cout << "  Residual Blocks:   " << std::setw(12) << breakdown.residual_blocks << " cycles ("
              << std::fixed << std::setprecision(2) << cyclesToMs(breakdown.residual_blocks) << " ms)\n";
    std::cout << "  Conv Middle:       " << std::setw(12) << breakdown.conv_middle << " cycles ("
              << std::fixed << std::setprecision(2) << cyclesToMs(breakdown.conv_middle) << " ms)\n";
    std::cout << "  Upsample:          " << std::setw(12) << breakdown.upsample << " cycles ("
              << std::fixed << std::setprecision(2) << cyclesToMs(breakdown.upsample) << " ms)\n";
    std::cout << "  Conv Output:       " << std::setw(12) << breakdown.conv_output << " cycles ("
              << std::fixed << std::setprecision(2) << cyclesToMs(breakdown.conv_output) << " ms)\n";
    std::cout << "  Output Store:      " << std::setw(12) << breakdown.output_store << " cycles ("
              << std::fixed << std::setprecision(2) << cyclesToMs(breakdown.output_store) << " ms)\n";
    std::cout << std::string(60, '-') << "\n";
    std::cout << "  TOTAL:             " << std::setw(12) << breakdown.total << " cycles ("
              << std::fixed << std::setprecision(2) << cyclesToMs(breakdown.total) << " ms)\n";
    std::cout << "  Est. Throughput:   " << std::fixed << std::setprecision(1)
              << (1000.0 / cyclesToMs(breakdown.total)) << " fps\n";

    double perf_degradation = 0.0;
    if (config_.enable_mc && breakdown.temporal_conv > 0) {
        uint64_t base_total = breakdown.total - breakdown.temporal_conv;
        if (base_total > 0) {
            perf_degradation = static_cast<double>(breakdown.temporal_conv) /
                               static_cast<double>(base_total) * 100.0;
        }
        std::cout << "  MC Overhead:       " << std::fixed << std::setprecision(1)
                  << perf_degradation << "%\n";
        std::cout << "  Meets <" << TARGET_LATENCY_MS << "ms:  "
                  << (cyclesToMs(breakdown.total) < TARGET_LATENCY_MS ? "YES" : "NO") << "\n";
    }
}

void XSimEmulator::printMemoryReport(const MemoryTraffic& traffic) const {
    std::cout << "\n=== XSim Memory Traffic Report";
    if (config_.enable_mc) {
        std::cout << " (MC Enabled)";
    }
    std::cout << "\n";
    std::cout << std::string(60, '-') << "\n";
    std::cout << "  DDR Reads:   " << traffic.ddr_reads << " bytes\n";
    std::cout << "  DDR Writes:  " << traffic.ddr_writes << " bytes\n";
    std::cout << "  BRAM Reads:  " << traffic.bram_reads << " bytes\n";
    std::cout << "  BRAM Writes: " << traffic.bram_writes << " bytes\n";
    std::cout << "  Total:       " << traffic.total_bytes << " bytes ("
              << std::fixed << std::setprecision(2)
              << static_cast<double>(traffic.total_bytes) / (1024.0 * 1024.0) << " MB)\n";
}