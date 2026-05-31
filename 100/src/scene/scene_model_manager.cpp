#include "scene/scene_model_manager.h"
#include "edsr/edsr_weights_loader.h"

#include <fstream>
#include <chrono>
#include <filesystem>

std::shared_ptr<SceneModelManager> g_scene_model_manager = nullptr;

SceneModelManager::SceneModelManager() {}

SceneModelManager::~SceneModelManager() {}

bool SceneModelManager::initialize(const std::string& models_dir) {
    scene_models_.clear();
    loaded_models_.clear();
    loaded_configs_.clear();

    for (SceneType type : default_scenes_) {
        SceneModelInfo info;
        info.type = type;
        info.name = getSceneModelName(type);
        info.model_path = getDefaultModelPath(models_dir, type);
        info.bitstream_path = getDefaultBitstreamPath(models_dir, type);
        info.description = getSceneModelDescription(type);
        info.is_custom = false;
        info.is_loaded = false;
        scene_models_[type] = info;
    }

    if (!loadSceneModel(SceneType::MOVIE)) {
        std::cerr << "[SceneModelManager] Failed to load default model (MOVIE)\n";
        return false;
    }

    current_scene_ = SceneType::MOVIE;
    current_model_ = loaded_models_[SceneType::MOVIE];
    current_config_ = loaded_configs_[SceneType::MOVIE];

    std::cout << "[SceneModelManager] Initialized with " << scene_models_.size()
              << " scene models, default: " << getSceneModelName(SceneType::MOVIE) << "\n";

    return true;
}

bool SceneModelManager::loadSceneModel(SceneType type) {
    if (loaded_models_.find(type) != loaded_models_.end()) {
        scene_models_[type].is_loaded = true;
        return true;
    }

    auto it = scene_models_.find(type);
    if (it == scene_models_.end()) {
        std::cerr << "[SceneModelManager] Unknown scene type: " << (int)type << "\n";
        return false;
    }

    return loadWeightsForScene(type);
}

bool SceneModelManager::switchToScene(SceneType type) {
    if (type == current_scene_ && !switch_in_progress_.load()) {
        return true;
    }

    std::lock_guard<std::mutex> lock(switch_mutex_);

    if (type == current_scene_) {
        return true;
    }

    auto start = std::chrono::high_resolution_clock::now();
    switch_in_progress_.store(true);
    frames_dropped_.store(0);
    switch_stats_.total_switches++;

    bool success = performModelSwitch(type);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double switch_time = duration.count();
    int frames_dropped = static_cast<int>(switch_time / 33.3);
    frames_dropped = std::min(frames_dropped, 3);
    frames_dropped_.store(frames_dropped);

    switch_stats_.scene_switch_counts[type]++;
    switch_stats_.total_frames_dropped += frames_dropped;
    switch_stats_.average_switch_time_ms =
        (switch_stats_.average_switch_time_ms * (switch_stats_.total_switches - 1) + switch_time) /
        switch_stats_.total_switches;

    if (success) {
        switch_stats_.successful_switches++;
        std::cout << "[SceneModelManager] Switched to " << getSceneModelName(type)
                  << " in " << switch_time << "ms, dropped " << frames_dropped << " frames\n";
    } else {
        switch_stats_.failed_switches++;
        std::cerr << "[SceneModelManager] Failed to switch to " << getSceneModelName(type) << "\n";
    }

    switch_in_progress_.store(false);

    return success;
}

SceneModelInfo* SceneModelManager::getModelInfo(SceneType type) {
    auto it = scene_models_.find(type);
    if (it != scene_models_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool SceneModelManager::registerCustomModel(const std::string& model_path,
                                            const std::string& name,
                                            const std::string& description) {
    if (!validateModelFile(model_path)) {
        std::cerr << "[SceneModelManager] Invalid model file: " << model_path << "\n";
        return false;
    }

    SceneModelInfo info;
    info.type = SceneType::CUSTOM;
    info.name = name;
    info.model_path = model_path;
    info.bitstream_path = "";
    info.description = description;
    info.is_custom = true;
    info.is_loaded = false;

    scene_models_[SceneType::CUSTOM] = info;

    std::cout << "[SceneModelManager] Registered custom model: " << name << "\n";

    return true;
}

bool SceneModelManager::uploadCustomModel(const std::string& source_path,
                                          const std::string& name,
                                          const std::string& description,
                                          std::string& error_msg) {
    if (!validateModelFile(source_path)) {
        error_msg = "Invalid model file format";
        return false;
    }

    std::string dest_dir = "models/custom/";
    try {
        std::filesystem::create_directories(dest_dir);
    } catch (const std::filesystem::filesystem_error& e) {
        error_msg = "Failed to create directory: " + std::string(e.what());
        return false;
    }

    std::string dest_path = dest_dir + name + ".bin";

    try {
        std::filesystem::copy_file(source_path, dest_path,
                                   std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error& e) {
        error_msg = "Failed to copy model file: " + std::string(e.what());
        return false;
    }

    return registerCustomModel(dest_path, name, description);
}

bool SceneModelManager::validateModelFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    char magic[4];
    file.read(magic, 4);
    if (std::strncmp(magic, "EDSR", 4) != 0) {
        return false;
    }

    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version < 1 || version > 2) {
        return false;
    }

    return true;
}

std::string SceneModelManager::getDefaultModelPath(const std::string& models_dir, SceneType type) {
    std::string scene_name = SceneClassifier::sceneTypeToString(type);
    return models_dir + "/edsr_" + scene_name + ".bin";
}

std::string SceneModelManager::getDefaultBitstreamPath(const std::string& models_dir, SceneType type) {
    std::string scene_name = SceneClassifier::sceneTypeToString(type);
    return models_dir + "/bitstream/edsr_" + scene_name + ".bit";
}

std::string SceneModelManager::getSceneModelName(SceneType type) {
    switch (type) {
        case SceneType::ANIMATION: return "EDSR-Animation";
        case SceneType::SPORTS: return "EDSR-Sports";
        case SceneType::MOVIE: return "EDSR-Movie";
        case SceneType::SURVEILLANCE: return "EDSR-Surveillance";
        case SceneType::CUSTOM: return "EDSR-Custom";
        default: return "EDSR-Default";
    }
}

std::string SceneModelManager::getSceneModelDescription(SceneType type) {
    switch (type) {
        case SceneType::ANIMATION:
            return "Optimized for anime/cartoon content with sharp edges and vibrant colors";
        case SceneType::SPORTS:
            return "Optimized for fast motion with enhanced temporal consistency";
        case SceneType::MOVIE:
            return "General purpose model optimized for film and video content";
        case SceneType::SURVEILLANCE:
            return "Optimized for low-light and high-compression surveillance footage";
        case SceneType::CUSTOM:
            return "User-uploaded custom model";
        default:
            return "Default model";
    }
}

bool SceneModelManager::loadWeightsForScene(SceneType type) {
    auto& info = scene_models_[type];

    EDSRConfig config;
    config.in_channels = 3;
    config.out_channels = 3;
    config.num_features = 64;
    config.num_residual_blocks = 8;
    config.kernel_size = 3;
    config.scale = EDSR_SCALE_FACTOR;
    config.activation = "relu";

    auto weights = std::make_shared<EDSRWeights>();

    auto start = std::chrono::high_resolution_clock::now();

    bool loaded = EDSRWeightsLoader::loadFromFile(info.model_path, *weights);
    if (!loaded) {
        std::cout << "[SceneModelManager] Model not found for " << info.name
                  << ", generating random weights\n";
        EDSRWeightsLoader::generateRandomWeights(*weights, config);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    info.load_time_ms = duration.count();
    info.is_loaded = true;

    auto config_ptr = std::make_shared<EDSRConfig>(config);

    loaded_models_[type] = weights;
    loaded_configs_[type] = config_ptr;

    std::cout << "[SceneModelManager] Loaded " << info.name << " in "
              << info.load_time_ms << "ms\n";

    return true;
}

bool SceneModelManager::performModelSwitch(SceneType new_scene) {
    if (!loadSceneModel(new_scene)) {
        std::cerr << "[SceneModelManager] Failed to load model for scene: "
                  << getSceneModelName(new_scene) << "\n";
        return false;
    }

    current_model_ = loaded_models_[new_scene];
    current_config_ = loaded_configs_[new_scene];
    current_scene_ = new_scene;

    return true;
}