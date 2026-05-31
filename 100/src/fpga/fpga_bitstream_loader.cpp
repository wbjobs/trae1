#include "fpga/fpga_bitstream_loader.h"

#include <fstream>
#include <filesystem>

std::shared_ptr<FPGABitstreamLoader> g_fpga_loader = nullptr;

FPGABitstreamLoader::FPGABitstreamLoader() {
    pr_region_.name = "edsr_pr_region";
    pr_region_.clk_divider = 2;
    pr_region_.data_width = 32;
}

FPGABitstreamLoader::~FPGABitstreamLoader() {}

bool FPGABitstreamLoader::initialize(FPGAType fpga_type) {
    fpga_type_ = fpga_type;

    if (fpga_type == FPGAType::SIMULATION) {
        simulation_mode_ = true;
        std::cout << "[FPGA] Initialized in SIMULATION mode\n";
    } else {
        simulation_mode_ = false;
        std::cout << "[FPGA] Initialized for " << (int)fpga_type << "\n";
    }

    initialized_ = true;
    return true;
}

bool FPGABitstreamLoader::loadFullBitstream(const std::string& bitstream_path) {
    std::lock_guard<std::mutex> lock(reconfig_mutex_);

    if (!validateBitstream(bitstream_path)) {
        std::cerr << "[FPGA] Invalid bitstream: " << bitstream_path << "\n";
        return false;
    }

    std::vector<uint8_t> bitstream_data;
    if (!loadBitstreamFromFile(bitstream_path, bitstream_data)) {
        std::cerr << "[FPGA] Failed to read bitstream\n";
        return false;
    }

    reconfiguring_.store(true);
    auto start = std::chrono::high_resolution_clock::now();

    bool success;
    if (simulation_mode_) {
        BitstreamInfo dummy_info;
        dummy_info.file_path = bitstream_path;
        dummy_info.file_size = bitstream_data.size();
        success = simulatePRConfiguration(dummy_info);
    } else {
        success = false;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    last_reconfig_time_ms_.store(duration.count());
    reconfiguring_.store(false);

    if (success) {
        std::cout << "[FPGA] Full bitstream loaded in " << duration.count() << "ms\n";
    }

    return success;
}

bool FPGABitstreamLoader::loadPartialBitstream(SceneType scene_type,
                                                const std::string& bitstream_path) {
    if (!validateBitstream(bitstream_path)) {
        std::cerr << "[FPGA] Invalid partial bitstream: " << bitstream_path << "\n";
        return false;
    }

    BitstreamInfo info;
    info.scene_type = scene_type;
    info.file_path = bitstream_path;

    std::vector<uint8_t> data;
    if (!loadBitstreamFromFile(bitstream_path, data)) {
        return false;
    }

    info.file_size = data.size();
    info.checksum = computeChecksum(data);

    bitstream_map_[scene_type] = info;
    available_bitstreams_.push_back(info);

    std::cout << "[FPGA] Registered partial bitstream for "
              << SceneClassifier::sceneTypeToString(scene_type)
              << " (" << (data.size() / 1024) << " KB)\n";

    return true;
}

bool FPGABitstreamLoader::reconfigureForScene(SceneType scene_type) {
    std::lock_guard<std::mutex> lock(reconfig_mutex_);

    auto it = bitstream_map_.find(scene_type);
    if (it == bitstream_map_.end()) {
        std::cerr << "[FPGA] No bitstream registered for scene: "
                  << SceneClassifier::sceneTypeToString(scene_type) << "\n";
        return false;
    }

    if (current_bitstream_ && current_bitstream_->scene_type == scene_type) {
        return true;
    }

    reconfiguring_.store(true);
    auto start = std::chrono::high_resolution_clock::now();

    bool success;
    if (simulation_mode_) {
        success = simulatePRConfiguration(it->second);
    } else {
        success = performPRConfiguration(it->second);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    last_reconfig_time_ms_.store(duration.count());
    reconfiguring_.store(false);

    if (success) {
        current_bitstream_ = &it->second;
        it->second.is_loaded = true;
        it->second.load_time_ms = duration.count();

        std::cout << "[FPGA] Partial reconfiguration complete for "
                  << SceneClassifier::sceneTypeToString(scene_type)
                  << " in " << duration.count() << "ms\n";
    }

    return success;
}

int FPGABitstreamLoader::getEstimatedFramesDropped() const {
    int64_t reconfig_time = last_reconfig_time_ms_.load() > 0
                               ? last_reconfig_time_ms_.load()
                               : simulation_reconfig_time_ms_.load();

    int frames = static_cast<int>(reconfig_time / 33.3);
    return std::min(frames, 3);
}

bool FPGABitstreamLoader::registerBitstream(SceneType scene_type,
                                            const std::string& bitstream_path) {
    return loadPartialBitstream(scene_type, bitstream_path);
}

bool FPGABitstreamLoader::validateBitstream(const std::string& bitstream_path) {
    if (!std::filesystem::exists(bitstream_path)) {
        return false;
    }

    std::vector<uint8_t> data;
    if (!loadBitstreamFromFile(bitstream_path, data)) {
        return false;
    }

    return validateBitstreamHeader(data);
}

int64_t FPGABitstreamLoader::getEstimatedReconfigTime(SceneType scene_type) {
    auto it = bitstream_map_.find(scene_type);
    if (it != bitstream_map_.end() && it->second.file_size > 0) {
        return estimateReconfigTime(it->second.file_size);
    }
    return simulation_reconfig_time_ms_.load();
}

bool FPGABitstreamLoader::loadBitstreamFromFile(const std::string& path,
                                                std::vector<uint8_t>& data) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    data.resize(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        return false;
    }

    return true;
}

bool FPGABitstreamLoader::validateBitstreamHeader(const std::vector<uint8_t>& data) {
    if (data.size() < 64) {
        return false;
    }

    if (simulation_mode_) {
        return true;
    }

    if (data[0] == 0x00 && data[1] == 0x09 && data[2] == 0x0f && data[3] == 0xf0) {
        return true;
    }

    if (data.size() >= 8 &&
        data[0] == 'x' && data[1] == 'o' && data[2] == 'r' && data[3] == 'b' &&
        data[4] == 'i' && data[5] == 't' && data[6] == 's' && data[7] == 't' &&
        data[8] == 'r' && data[9] == 'e' && data[10] == 'a' && data[11] == 'm') {
        return true;
    }

    return true;
}

bool FPGABitstreamLoader::performPRConfiguration(const BitstreamInfo& bitstream) {
    if (simulation_mode_) {
        return simulatePRConfiguration(bitstream);
    }

    std::cerr << "[FPGA] Real FPGA PR not implemented in this version\n";
    return false;
}

bool FPGABitstreamLoader::simulatePRConfiguration(const BitstreamInfo& bitstream) {
    int64_t estimated_time = estimateReconfigTime(bitstream.file_size);
    if (estimated_time < simulation_reconfig_time_ms_) {
        estimated_time = simulation_reconfig_time_ms_;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(estimated_time));

    return true;
}

std::vector<uint8_t> FPGABitstreamLoader::computeChecksum(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> checksum(16, 0);
    for (size_t i = 0; i < data.size(); ++i) {
        checksum[i % 16] ^= data[i];
    }
    return checksum;
}

int64_t FPGABitstreamLoader::estimateReconfigTime(size_t bitstream_size) {
    int64_t base_time = 50;
    int64_t per_mb_time = 2;
    size_t mb = bitstream_size / (1024 * 1024);
    return base_time + static_cast<int64_t>(mb) * per_mb_time;
}