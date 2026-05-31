#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
#include <string>
#include <memory>
#include <functional>
#include <chrono>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <thread>
#include <optional>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstring>
#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>

constexpr int INPUT_WIDTH = 1280;
constexpr int INPUT_HEIGHT = 720;
constexpr int OUTPUT_WIDTH = 1920;
constexpr int OUTPUT_HEIGHT = 1080;
constexpr int INPUT_CHANNELS = 3;
constexpr int OUTPUT_CHANNELS = 3;
constexpr int MAX_STREAMS = 4;
constexpr int TARGET_FPS = 30;
constexpr int TARGET_LATENCY_MS = 50;

constexpr int EDSR_NUM_FEATURES = 64;
constexpr int EDSR_NUM_RESIDUAL_BLOCKS = 8;
constexpr int EDSR_SCALE_FACTOR = 2;
constexpr int EDSR_KERNEL_SIZE = 3;
constexpr int EDSR_PADDING = 1;

constexpr int QUANTIZATION_BITS = 8;
constexpr int QUANT_MIN = -128;
constexpr int QUANT_MAX = 127;

constexpr int TEMPORAL_WINDOW_SIZE = 3;
constexpr int TEMPORAL_KERNEL = 3;
constexpr double MOTION_THRESHOLD_DEFAULT = 15.0;
constexpr double ARTIFACT_SEVERITY_THRESHOLD = 0.7;

constexpr double SCENE_CLASSIFICATION_MIN_CONFIDENCE = 0.8;
constexpr int SCENE_STABLE_FRAMES_THRESHOLD = 15;
constexpr int SCENE_CLASSIFICATION_INTERVAL = 10;
constexpr int SCENE_SWITCH_MIN_FRAMES = 300;
constexpr int MAX_FRAMES_DROPPED_DURING_SWITCH = 3;

enum class SwitchMode {
    MANUAL,
    AUTO_CLASSIFIER,
    FORCED
};

struct FrameData {
    int stream_id;
    int frame_id;
    std::vector<uint8_t> data;
    int width;
    int height;
    int channels;
    std::chrono::steady_clock::time_point timestamp;
};

struct ProcessedFrame {
    int stream_id;
    int frame_id;
    std::vector<uint8_t> data;
    int width;
    int height;
    int channels;
    std::chrono::steady_clock::time_point input_timestamp;
    std::chrono::steady_clock::time_point output_timestamp;
    double psnr;
};

using FrameCallback = std::function<void(const ProcessedFrame&)>;

enum class ProcessingMode {
    CPU_REFERENCE,
    HLS_SIMULATION,
    HYBRID
};

struct ServiceConfig {
    std::vector<std::string> rtmp_urls;
    std::string model_path;
    std::string models_dir;
    std::string classifier_model_path;
    ProcessingMode mode;
    int num_streams;
    bool enable_psnr;
    bool enable_ssim;
    bool enable_fps_stats;
    int target_latency_ms;
    bool enable_motion_compensation;
    bool auto_fallback;
    double motion_threshold;
    double artifact_threshold;
    bool enable_scene_classification;
    SwitchMode model_switch_mode;
    double scene_confidence_threshold;
    int scene_classification_interval;
    int scene_min_frames_before_switch;
    std::string forced_scene;
    bool enable_dashboard;
    int dashboard_update_interval_ms;
    std::string custom_model_path;
    std::string custom_model_name;
    std::string custom_model_description;
};

struct StreamStats {
    int stream_id;
    double current_fps;
    double average_fps;
    double min_fps;
    double max_fps;
    double current_latency_ms;
    double average_latency_ms;
    double average_psnr;
    double average_ssim;
    uint64_t total_frames;
    uint64_t dropped_frames;
    double motion_score;
    int artifact_type;
    uint64_t fallback_count;
    bool motion_compensation_active;
    int current_scene;
    std::string current_scene_name;
    double scene_confidence;
    uint64_t model_switch_count;
    uint64_t frames_dropped_during_switch;
    double current_ssim;
    double ssim_average;
};

struct GlobalStats {
    std::vector<StreamStats> per_stream;
    double total_bandwidth_mbps;
    double system_cpu_usage;
    size_t total_memory_mb;
};

class SceneClassifier;
extern SceneClassifier* g_scene_classifier;

class SceneModelManager;
extern std::shared_ptr<SceneModelManager> g_scene_model_manager;

class FPGABitstreamLoader;
extern std::shared_ptr<FPGABitstreamLoader> g_fpga_loader;