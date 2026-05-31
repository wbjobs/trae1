#pragma once

#include "utils/common.h"

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>

enum class SceneType {
    UNKNOWN = 0,
    ANIMATION = 1,
    SPORTS = 2,
    MOVIE = 3,
    SURVEILLANCE = 4,
    CUSTOM = 5
};

struct SceneClassificationResult {
    SceneType type;
    double confidence;
    std::string label;
    int64_t inference_time_ms;
    std::unordered_map<SceneType, double> class_probabilities;
};

class SceneClassifier {
public:
    SceneClassifier();
    ~SceneClassifier();

    bool initialize(const std::string& model_path = "");

    SceneClassificationResult classify(const cv::Mat& frame);

    SceneType getCurrentScene() const { return current_scene_; }
    double getCurrentConfidence() const { return current_confidence_; }

    bool isSceneStable() const;
    int getSceneDurationFrames() const { return stable_frames_; }

    static std::string sceneTypeToString(SceneType type);
    static SceneType stringToSceneType(const std::string& str);

    void setMinConfidenceThreshold(double threshold) { min_confidence_threshold_ = threshold; }
    void setStableFramesThreshold(int frames) { stable_frames_threshold_ = frames; }

private:
    cv::dnn::Net net_;
    bool initialized_ = false;

    SceneType current_scene_ = SceneType::UNKNOWN;
    SceneType pending_scene_ = SceneType::UNKNOWN;
    double current_confidence_ = 0.0;
    int stable_frames_ = 0;
    int pending_frames_ = 0;

    double min_confidence_threshold_ = 0.8;
    int stable_frames_threshold_ = 15;

    const std::vector<std::string> class_labels_ = {
        "unknown",
        "animation",
        "sports",
        "movie",
        "surveillance"
    };

    void preprocessFrame(const cv::Mat& frame, cv::Mat& blob);
    SceneClassificationResult postprocessResults(const cv::Mat& output, int64_t inference_time);

    SceneType classifyByHeuristics(const cv::Mat& frame,
                                   std::unordered_map<SceneType, double>& probabilities);

    double computeColorSaturation(const cv::Mat& frame);
    double computeMotionLevel(const cv::Mat& frame);
    double computeEdgeDensity(const cv::Mat& frame);
    double computeSkinToneRatio(const cv::Mat& frame);
};