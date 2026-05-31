#pragma once

#include "utils/common.h"
#include "scene/scene_classifier.h"
#include "metrics/psnr_evaluator.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <chrono>

struct ScenePerfMetrics {
    SceneType type;
    std::string name;

    double psnr_current = 0.0;
    double psnr_average = 0.0;
    double psnr_best = 0.0;
    double psnr_worst = 0.0;

    double ssim_current = 0.0;
    double ssim_average = 0.0;
    double ssim_best = 0.0;
    double ssim_worst = 0.0;

    uint64_t total_frames = 0;
    uint64_t frames_below_threshold = 0;

    double inference_time_ms = 0.0;
    double average_latency_ms = 0.0;

    std::vector<std::pair<double, double>> recent_samples;
    static constexpr int MAX_RECENT_SAMPLES = 100;
};

struct DashboardConfig {
    bool show_realtime = true;
    bool show_historical = true;
    bool show_comparison = true;
    int update_interval_ms = 1000;
    double psnr_warning_threshold = 30.0;
    double ssim_warning_threshold = 0.9;
};

class PerformanceDashboard {
public:
    PerformanceDashboard();
    ~PerformanceDashboard();

    bool initialize(const DashboardConfig& config = DashboardConfig());

    void updateMetrics(SceneType scene, double psnr, double ssim, double latency_ms = 0.0);

    void recordFrameProcessed(SceneType scene, bool below_threshold = false);

    ScenePerfMetrics* getMetrics(SceneType scene);
    const std::unordered_map<SceneType, ScenePerfMetrics>& getAllMetrics() const { return scene_metrics_; }

    std::string generateTextReport();
    std::string generateComparisonTable();
    std::string generateJSONReport();

    void setReferenceScene(SceneType scene) { reference_scene_ = scene; }
    SceneType getReferenceScene() const { return reference_scene_; }

    std::string getBestScene() const;
    std::string getWorstScene() const;

    double getOverallAveragePSNR() const;
    double getOverallAverageSSIM() const;

    void resetAllMetrics();
    void resetSceneMetrics(SceneType scene);

    void setAutoUpdate(bool enabled) { auto_update_ = enabled; }
    bool isAutoUpdateEnabled() const { return auto_update_; }

    void updateDisplay();
    void printSummary();

    void exportMetricsToFile(const std::string& filepath);

private:
    DashboardConfig config_;
    std::unordered_map<SceneType, ScenePerfMetrics> scene_metrics_;
    SceneType reference_scene_ = SceneType::MOVIE;
    bool auto_update_ = true;

    std::mutex metrics_mutex_;

    std::chrono::system_clock::time_point last_update_time_;
    uint64_t total_frames_processed_ = 0;

    void initializeSceneMetrics(SceneType type);
    void updateRunningAverage(ScenePerfMetrics& metrics, double psnr, double ssim, double latency);

    std::string formatMetricsRow(const ScenePerfMetrics& metrics, bool is_reference = false);
    std::string getSSIMQualityLabel(double ssim) const;
    std::string getPSNRQualityLabel(double psnr) const;

    double computeSSIM(const cv::Mat& img1, const cv::Mat& img2);
};