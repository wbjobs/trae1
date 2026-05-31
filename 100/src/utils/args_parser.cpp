#include "utils/args_parser.h"

ServiceConfig ArgsParser::parse(int argc, char* argv[]) {
    ServiceConfig config;
    config.rtmp_urls = parseCSVArgs(argc, argv, "--rtmp-urls");
    config.model_path = parseStringArg(argc, argv, "--model-path", "models/edsr_int8.bin");
    config.models_dir = parseStringArg(argc, argv, "--models-dir", "models");
    config.classifier_model_path = parseStringArg(argc, argv, "--classifier-model", "");
    config.num_streams = parseIntArg(argc, argv, "--num-streams", 1);
    config.enable_psnr = parseBoolArg(argc, argv, "--enable-psnr");
    config.enable_ssim = parseBoolArg(argc, argv, "--enable-ssim");
    config.enable_fps_stats = parseBoolArg(argc, argv, "--fps-stats");
    config.target_latency_ms = parseIntArg(argc, argv, "--latency", TARGET_LATENCY_MS);
    config.enable_motion_compensation = parseBoolArg(argc, argv, "--motion-compensation");
    config.auto_fallback = !parseBoolArg(argc, argv, "--no-auto-fallback");
    config.motion_threshold = parseDoubleArg(argc, argv, "--motion-threshold", MOTION_THRESHOLD_DEFAULT);
    config.artifact_threshold = parseDoubleArg(argc, argv, "--artifact-threshold", ARTIFACT_SEVERITY_THRESHOLD);

    config.enable_scene_classification = parseBoolArg(argc, argv, "--scene-classification");
    config.scene_confidence_threshold = parseDoubleArg(argc, argv, "--scene-confidence",
                                                       SCENE_CLASSIFICATION_MIN_CONFIDENCE);
    config.scene_classification_interval = parseIntArg(argc, argv, "--scene-interval",
                                                        SCENE_CLASSIFICATION_INTERVAL);
    config.scene_min_frames_before_switch = parseIntArg(argc, argv, "--scene-min-frames",
                                                         SCENE_SWITCH_MIN_FRAMES);
    config.forced_scene = parseStringArg(argc, argv, "--force-scene", "");

    config.enable_dashboard = parseBoolArg(argc, argv, "--dashboard");
    config.dashboard_update_interval_ms = parseIntArg(argc, argv, "--dashboard-interval", 2000);

    config.custom_model_path = parseStringArg(argc, argv, "--custom-model", "");
    config.custom_model_name = parseStringArg(argc, argv, "--custom-model-name", "Custom");
    config.custom_model_description = parseStringArg(argc, argv, "--custom-model-desc",
                                                      "User-uploaded custom model");

    std::string modeStr = parseStringArg(argc, argv, "--mode", "hls");
    if (modeStr == "cpu") {
        config.mode = ProcessingMode::CPU_REFERENCE;
    } else if (modeStr == "hls") {
        config.mode = ProcessingMode::HLS_SIMULATION;
    } else {
        config.mode = ProcessingMode::HYBRID;
    }

    std::string switchModeStr = parseStringArg(argc, argv, "--switch-mode", "auto");
    if (switchModeStr == "manual") {
        config.model_switch_mode = SwitchMode::MANUAL;
    } else if (switchModeStr == "forced") {
        config.model_switch_mode = SwitchMode::FORCED;
    } else {
        config.model_switch_mode = SwitchMode::AUTO_CLASSIFIER;
    }

    config.num_streams = std::min(config.num_streams, MAX_STREAMS);
    if (config.rtmp_urls.empty()) {
        config.rtmp_urls.push_back("rtmp://localhost/live/stream");
    }

    if (!config.forced_scene.empty()) {
        config.model_switch_mode = SwitchMode::FORCED;
    }

    if (config.enable_dashboard) {
        config.enable_ssim = true;
        config.enable_psnr = true;
    }

    return config;
}

void ArgsParser::printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [OPTIONS]\n"
              << "Video Stream Super-Resolution Service (EDSR + Vitis HLS + Motion Compensation)\n"
              << "                Adaptive Scene Model Switching v1.2\n\n"
              << "Input/Output Options:\n"
              << "  --rtmp-urls <url1,url2,...>  Comma-separated RTMP input URLs\n"
              << "  --model-path <path>           Path to default INT8 quantized model weights\n"
              << "  --models-dir <dir>            Directory containing scene-specific models (default: models)\n"
              << "  --num-streams <N>             Number of concurrent streams (1-4, default: 1)\n"
              << "  --mode <cpu|hls|hybrid>      Processing mode (default: hls)\n\n"
              << "Quality Metrics Options:\n"
              << "  --enable-psnr                 Enable PSNR evaluation\n"
              << "  --enable-ssim                 Enable SSIM evaluation\n"
              << "  --fps-stats                   Enable FPS statistics output\n"
              << "  --latency <ms>                Target latency in ms (default: 50)\n\n"
              << "Motion Compensation Options:\n"
              << "  --motion-compensation         Enable temporal motion compensation (3D conv)\n"
              << "  --no-auto-fallback            Disable automatic fallback on artifact detection\n"
              << "  --motion-threshold <val>      Motion detection threshold (default: 15.0)\n"
              << "  --artifact-threshold <val>    Artifact severity threshold (default: 0.7)\n\n"
              << "Scene Classification & Model Switching Options:\n"
              << "  --scene-classification        Enable real-time scene classification\n"
              << "  --classifier-model <path>     Path to MobileNet classification model (optional)\n"
              << "  --switch-mode <auto|manual|forced>\n"
              << "                                Model switching mode (default: auto)\n"
              << "  --scene-confidence <val>      Min confidence for scene switch (0.0-1.0, default: 0.8)\n"
              << "  --scene-interval <N>          Classify every N frames (default: 10)\n"
              << "  --scene-min-frames <N>        Min frames before switch (default: 300)\n"
              << "  --force-scene <type>          Force specific scene (animation|sports|movie|surveillance)\n\n"
              << "Performance Dashboard Options:\n"
              << "  --dashboard                   Enable performance comparison dashboard\n"
              << "  --dashboard-interval <ms>     Dashboard update interval (default: 2000)\n\n"
              << "Custom Model Options:\n"
              << "  --custom-model <path>         Path to custom user model\n"
              << "  --custom-model-name <name>    Name for custom model (default: Custom)\n"
              << "  --custom-model-desc <text>    Description for custom model\n\n"
              << "  --help                        Show this help message\n\n"
              << "Scene Types: animation | sports | movie | surveillance | custom\n\n"
              << "Examples:\n"
              << "  " << programName << " --scene-classification --dashboard --motion-compensation\n"
              << "  " << programName << " --force-scene sports --motion-compensation --enable-psnr\n"
              << "  " << programName << " --scene-classification --scene-confidence 0.85 --scene-interval 5\n"
              << "  " << programName << " --custom-model models/my_model.bin --custom-model-name \"MyModel\"\n"
              << "  " << programName << " --rtmp-urls url1,url2 --num-streams 2 --dashboard --motion-compensation\n";
}

void ArgsParser::printConfig(const ServiceConfig& config) {
    std::cout << "=== Video SR Service Configuration v1.2:\n"
              << "  RTMP URLs: ";
    for (size_t i = 0; i < config.rtmp_urls.size(); ++i) {
        std::cout << config.rtmp_urls[i];
        if (i < config.rtmp_urls.size() - 1) std::cout << ", ";
    }
    std::cout << "\n"
              << "  Model Path: " << config.model_path << "\n"
              << "  Models Directory: " << config.models_dir << "\n"
              << "  Num Streams: " << config.num_streams << "\n"
              << "  Mode: " << (config.mode == ProcessingMode::CPU_REFERENCE ? "CPU"
                      : config.mode == ProcessingMode::HLS_SIMULATION ? "HLS" : "Hybrid") << "\n"
              << "  Motion Compensation: " << (config.enable_motion_compensation ? "Enabled" : "Disabled") << "\n"
              << "  Auto Fallback: " << (config.auto_fallback ? "Enabled" : "Disabled") << "\n"
              << "  Motion Threshold: " << config.motion_threshold << "\n"
              << "  Artifact Threshold: " << config.artifact_threshold << "\n"
              << "  PSNR: " << (config.enable_psnr ? "Enabled" : "Disabled") << "\n"
              << "  SSIM: " << (config.enable_ssim ? "Enabled" : "Disabled") << "\n"
              << "  FPS Stats: " << (config.enable_fps_stats ? "Enabled" : "Disabled") << "\n"
              << "  Target Latency: " << config.target_latency_ms << "ms\n"
              << "  Input: " << INPUT_WIDTH << "x" << INPUT_HEIGHT << "@" << TARGET_FPS << "fps\n"
              << "  Output: " << OUTPUT_WIDTH << "x" << OUTPUT_HEIGHT << "@" << TARGET_FPS << "fps\n";

    if (config.enable_scene_classification || config.model_switch_mode != SwitchMode::MANUAL) {
        std::cout << "\n=== Scene Classification & Model Switching:\n"
                  << "  Scene Classification: " << (config.enable_scene_classification ? "Enabled" : "Disabled") << "\n";

        std::string switch_mode_str = "Manual";
        if (config.model_switch_mode == SwitchMode::AUTO_CLASSIFIER) switch_mode_str = "Auto (Classifier)";
        if (config.model_switch_mode == SwitchMode::FORCED) switch_mode_str = "Forced";
        std::cout << "  Switch Mode: " << switch_mode_str << "\n";

        if (config.model_switch_mode == SwitchMode::FORCED && !config.forced_scene.empty()) {
            std::cout << "  Forced Scene: " << config.forced_scene << "\n";
        }

        if (config.enable_scene_classification) {
            std::cout << "  Confidence Threshold: " << config.scene_confidence_threshold << "\n"
                      << "  Classification Interval: every " << config.scene_classification_interval << " frames\n"
                      << "  Min Frames Before Switch: " << config.scene_min_frames_before_switch << " frames\n";
            if (!config.classifier_model_path.empty()) {
                std::cout << "  Classifier Model: " << config.classifier_model_path << "\n";
            }
        }
    }

    if (config.enable_dashboard) {
        std::cout << "\n=== Performance Dashboard:\n"
                  << "  Dashboard: Enabled\n"
                  << "  Update Interval: " << config.dashboard_update_interval_ms << "ms\n";
    }

    if (!config.custom_model_path.empty()) {
        std::cout << "\n=== Custom Model:\n"
                  << "  Path: " << config.custom_model_path << "\n"
                  << "  Name: " << config.custom_model_name << "\n"
                  << "  Description: " << config.custom_model_description << "\n";
    }
}

std::string ArgsParser::parseStringArg(int argc, char* argv[], const std::string& flag,
                                       const std::string& defaultVal) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == flag && i + 1 < argc) {
            return argv[i + 1];
        }
    }
    return defaultVal;
}

int ArgsParser::parseIntArg(int argc, char* argv[], const std::string& flag, int defaultVal) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == flag && i + 1 < argc) {
            return std::atoi(argv[i + 1]);
        }
    }
    return defaultVal;
}

double ArgsParser::parseDoubleArg(int argc, char* argv[], const std::string& flag, double defaultVal) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == flag && i + 1 < argc) {
            return std::atof(argv[i + 1]);
        }
    }
    return defaultVal;
}

bool ArgsParser::parseBoolArg(int argc, char* argv[], const std::string& flag) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == flag) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> ArgsParser::parseCSVArgs(int argc, char* argv[], const std::string& flag) {
    std::vector<std::string> result;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == flag && i + 1 < argc) {
            std::string csv = argv[i + 1];
            std::stringstream ss(csv);
            std::string item;
            while (std::getline(ss, item, ',')) {
                if (!item.empty()) {
                    result.push_back(item);
                }
            }
        }
    }
    return result;
}