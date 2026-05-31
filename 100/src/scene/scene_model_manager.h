#pragma once

#include "utils/common.h"
#include "edsr/edsr_model.h"
#include "edsr/edsr_quantized.h"
#include "scene/scene_classifier.h"

#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>

struct SceneModelInfo {
    SceneType type;
    std::string name;
    std::string model_path;
    std::string bitstream_path;
    std::string description;
    bool is_custom = false;
    bool is_loaded = false;
    int64_t load_time_ms = 0;
};

struct ModelSwitchStats {
    uint64_t total_switches = 0;
    uint64_t successful_switches = 0;
    uint64_t failed_switches = 0;
    uint64_t total_frames_dropped = 0;
    double average_switch_time_ms = 0.0;
    std::unordered_map<SceneType, uint64_t> scene_switch_counts;
};

class SceneModelManager {
public:
    SceneModelManager();
    ~SceneModelManager();

    bool initialize(const std::string& models_dir);

    bool loadSceneModel(SceneType type);
    bool switchToScene(SceneType type);

    SceneType getCurrentScene() const { return current_scene_; }
    std::shared_ptr<EDSRWeights> getCurrentModel() { return current_model_; }
    std::shared_ptr<EDSRConfig> getCurrentConfig() { return current_config_; }

    SceneModelInfo* getModelInfo(SceneType type);
    const std::unordered_map<SceneType, SceneModelInfo>& getAllModelInfo() const { return scene_models_; }

    bool registerCustomModel(const std::string& model_path,
                             const std::string& name,
                             const std::string& description);

    bool isSwitchingInProgress() const { return switch_in_progress_.load(); }
    int getFramesDroppedDuringSwitch() const { return frames_dropped_.load(); }

    const ModelSwitchStats& getSwitchStats() const { return switch_stats_; }
    void resetSwitchStats() { switch_stats_ = ModelSwitchStats(); }

    void setMinConfidenceThreshold(double threshold) { min_confidence_threshold_ = threshold; }
    double getMinConfidenceThreshold() const { return min_confidence_threshold_; }

    bool uploadCustomModel(const std::string& source_path,
                           const std::string& name,
                           const std::string& description,
                           std::string& error_msg);

    bool validateModelFile(const std::string& path);

private:
    std::unordered_map<SceneType, SceneModelInfo> scene_models_;
    std::unordered_map<SceneType, std::shared_ptr<EDSRWeights>> loaded_models_;
    std::unordered_map<SceneType, std::shared_ptr<EDSRConfig>> loaded_configs_;

    SceneType current_scene_ = SceneType::MOVIE;
    std::shared_ptr<EDSRWeights> current_model_;
    std::shared_ptr<EDSRConfig> current_config_;

    std::atomic<bool> switch_in_progress_{false};
    std::atomic<int> frames_dropped_{0};
    std::mutex switch_mutex_;

    ModelSwitchStats switch_stats_;
    double min_confidence_threshold_ = 0.8;

    const std::vector<SceneType> default_scenes_ = {
        SceneType::ANIMATION,
        SceneType::SPORTS,
        SceneType::MOVIE,
        SceneType::SURVEILLANCE
    };

    std::string getDefaultModelPath(const std::string& models_dir, SceneType type);
    std::string getDefaultBitstreamPath(const std::string& models_dir, SceneType type);
    std::string getSceneModelName(SceneType type);
    std::string getSceneModelDescription(SceneType type);

    bool loadWeightsForScene(SceneType type);
    bool performModelSwitch(SceneType new_scene);
};

extern std::shared_ptr<SceneModelManager> g_scene_model_manager;