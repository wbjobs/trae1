#pragma once

#include "utils/common.h"
#include "scene/scene_classifier.h"

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>

enum class FPGAType {
    XILINX_SERIES7,
    XILINX_ULTRASCALE,
    XILINX_ULTRASCALE_PLUS,
    SIMULATION
};

struct BitstreamInfo {
    SceneType scene_type;
    std::string file_path;
    size_t file_size = 0;
    std::vector<uint8_t> checksum;
    int64_t load_time_ms = 0;
    bool is_loaded = false;
};

struct PartialReconfigRegion {
    std::string name;
    int32_t clk_divider;
    int32_t data_width;
    bool is_active = false;
};

class FPGABitstreamLoader {
public:
    FPGABitstreamLoader();
    ~FPGABitstreamLoader();

    bool initialize(FPGAType fpga_type = FPGAType::SIMULATION);

    bool loadFullBitstream(const std::string& bitstream_path);
    bool loadPartialBitstream(SceneType scene_type, const std::string& bitstream_path);

    bool reconfigureForScene(SceneType scene_type);

    bool isReconfiguring() const { return reconfiguring_.load(); }
    int64_t getLastReconfigTime() const { return last_reconfig_time_ms_.load(); }
    int getEstimatedFramesDropped() const;

    const BitstreamInfo* getCurrentBitstream() const { return current_bitstream_; }
    const std::vector<BitstreamInfo>& getAvailableBitstreams() const { return available_bitstreams_; }

    bool registerBitstream(SceneType scene_type, const std::string& bitstream_path);

    bool validateBitstream(const std::string& bitstream_path);

    int64_t getEstimatedReconfigTime(SceneType scene_type);

    void setSimulationMode(bool enabled) { simulation_mode_ = enabled; }
    bool isSimulationMode() const { return simulation_mode_; }

    void setSimulationReconfigTime(int64_t time_ms) { simulation_reconfig_time_ms_ = time_ms; }

private:
    FPGAType fpga_type_ = FPGAType::SIMULATION;
    bool initialized_ = false;
    bool simulation_mode_ = true;

    std::atomic<bool> reconfiguring_{false};
    std::atomic<int64_t> last_reconfig_time_ms_{0};
    std::atomic<int64_t> simulation_reconfig_time_ms_{200};

    BitstreamInfo* current_bitstream_ = nullptr;
    std::vector<BitstreamInfo> available_bitstreams_;
    std::unordered_map<SceneType, BitstreamInfo> bitstream_map_;

    PartialReconfigRegion pr_region_;

    std::mutex reconfig_mutex_;

    bool loadBitstreamFromFile(const std::string& path, std::vector<uint8_t>& data);
    bool validateBitstreamHeader(const std::vector<uint8_t>& data);
    bool performPRConfiguration(const BitstreamInfo& bitstream);
    bool simulatePRConfiguration(const BitstreamInfo& bitstream);

    std::vector<uint8_t> computeChecksum(const std::vector<uint8_t>& data);
    int64_t estimateReconfigTime(size_t bitstream_size);
};

extern std::shared_ptr<FPGABitstreamLoader> g_fpga_loader;