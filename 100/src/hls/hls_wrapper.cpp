#include "hls/hls_wrapper.h"

HLSWrapper::HLSWrapper(const EDSRConfig& config)
    : config_(config), quant_model_(config) {
    HLSModuleConfig hls_config = {
        INPUT_HEIGHT, INPUT_WIDTH, OUTPUT_HEIGHT, OUTPUT_WIDTH,
        config_.num_features, config_.num_residual_blocks,
        config_.kernel_size, config_.scale,
        fpga_freq_mhz_, 256, 16, true, true
    };
    emulator_.configure(hls_config);
}

HLSWrapper::~HLSWrapper() = default;

bool HLSWrapper::loadWeights(const std::string& path) {
    bool success = quant_model_.hasWeights();
    if (!success) {
        success = EDSRWeightsLoader::loadFromFile(path,
            const_cast<EDSRWeights&>(quant_model_.getWeights()));
    }
    if (success) {
        emulator_.setWeightsLoaded(true);
        std::cout << "[HLSWrapper] Weights loaded for HLS simulation\n";
    }
    return success;
}

std::vector<uint8_t> HLSWrapper::processFrame(const uint8_t* input,
                                                int in_h, int in_w,
                                                int out_h, int out_w) {
    estimatePerformance(in_h, in_w);

    auto start = std::chrono::steady_clock::now();

    std::vector<uint8_t> output(static_cast<size_t>(out_h) * out_w * config_.out_channels);

    runSoftwareEmulation(input, output, in_h, in_w, out_h, out_w);

    auto end = std::chrono::steady_clock::now();
    double actual_ms = std::chrono::duration<double, std::milli>(end - start).count();

    perf_info_.estimated_latency_ms = actual_ms;

    return output;
}

void HLSWrapper::estimatePerformance(int in_h, int in_w) {
    auto breakdown = emulator_.simulateFrameProcessing(in_h, in_w);

    perf_info_.total_cycles = breakdown.total;
    perf_info_.conv_cycles = breakdown.conv_input + breakdown.residual_blocks +
                              breakdown.conv_middle + breakdown.upsample +
                              breakdown.conv_output;
    perf_info_.memory_cycles = breakdown.input_load + breakdown.output_store;
    perf_info_.overhead_cycles = 0;
    perf_info_.estimated_latency_ms = emulator_.cyclesToMs(breakdown.total);
    perf_info_.estimated_throughput_fps = 1000.0 / perf_info_.estimated_latency_ms;
    perf_info_.fpga_frequency_mhz = fpga_freq_mhz_;
    perf_info_.meets_requirements = perf_info_.estimated_latency_ms < TARGET_LATENCY_MS;
}

void HLSWrapper::runSoftwareEmulation(const uint8_t* input,
                                       std::vector<uint8_t>& output,
                                       int in_h, int in_w,
                                       int out_h, int out_w) {
    auto quant_result = quant_model_.processFrame(input, in_h, in_w, out_h, out_w);

    int copy_size = std::min(output.size(), quant_result.size());
    std::memcpy(output.data(), quant_result.data(), copy_size);

    auto breakdown = emulator_.simulateFrameProcessing(in_h, in_w);
    emulator_.printCycleReport(breakdown);
}

void HLSWrapper::printPerformanceReport() const {
    std::cout << "\n=== HLS Performance Report ===\n";
    std::cout << "  FPGA Frequency:      " << fpga_freq_mhz_ << " MHz\n";
    std::cout << "  Total Cycles:        " << perf_info_.total_cycles << "\n";
    std::cout << "  Conv Cycles:         " << perf_info_.conv_cycles << "\n";
    std::cout << "  Memory Cycles:       " << perf_info_.memory_cycles << "\n";
    std::cout << "  Est. Latency:        " << std::fixed << std::setprecision(2)
              << perf_info_.estimated_latency_ms << " ms\n";
    std::cout << "  Est. Throughput:     " << std::fixed << std::setprecision(1)
              << perf_info_.estimated_throughput_fps << " fps\n";
    std::cout << "  Meets <" << TARGET_LATENCY_MS << "ms:  "
              << (perf_info_.meets_requirements ? "YES" : "NO") << "\n";
}

void HLSWrapper::setFPGAConfiguration(int freq_mhz, int ddr_width_bytes) {
    fpga_freq_mhz_ = freq_mhz;
    ddr_width_bytes_ = ddr_width_bytes;
    HLSModuleConfig hls_config = emulator_.getConfig();
    hls_config.fpga_freq_mhz = freq_mhz;
    hls_config.ddr_burst_length = ddr_width_bytes;
    emulator_.configure(hls_config);
}