#pragma once

#include "utils/common.h"
#include "pipeline/stream_pipeline.h"
#include "hls/hls_wrapper.h"
#include "edsr/edsr_model.h"
#include "edsr/edsr_quantized.h"
#include "motion/temporal_fusion.h"
#include "scene/scene_classifier.h"
#include "scene/scene_model_manager.h"
#include "fpga/fpga_bitstream_loader.h"
#include "metrics/performance_dashboard.h"

class PipelineManager {
public:
    explicit PipelineManager(const ServiceConfig& config);
    ~PipelineManager();

    bool initialize();
    void startAll();
    void stopAll();

    void setGlobalOutputCallback(FrameCallback callback);

    GlobalStats getGlobalStats() const;
    std::vector<StreamStats> getStreamStats() const;

    void printStatsReport() const;

    int getNumStreams() const { return static_cast<int>(pipelines_.size()); }
    bool isRunning() const { return running_.load(); }

    SceneClassifier* getSceneClassifier() { return scene_classifier_.get(); }
    SceneModelManager* getModelManager() { return model_manager_.get(); }
    FPGABitstreamLoader* getFPGALoader() { return fpga_loader_.get(); }
    PerformanceDashboard* getDashboard() { return dashboard_.get(); }

    bool uploadCustomModel(const std::string& source_path,
                           const std::string& name,
                           const std::string& description,
                           std::string& error_msg);

    bool exportDashboardReport(const std::string& filepath);

private:
    bool loadModels();
    void setupStreams();
    void statsPrintingLoop();

    ServiceConfig config_;

    EDSRConfig edsr_config_;
    EDSRModel cpu_model_;
    EDSRQuantized quant_model_;
    HLSWrapper hls_wrapper_;
    TemporalFusion temporal_fusion_;

    std::unique_ptr<SceneClassifier> scene_classifier_;
    std::unique_ptr<SceneModelManager> model_manager_;
    std::unique_ptr<FPGABitstreamLoader> fpga_loader_;
    std::unique_ptr<PerformanceDashboard> dashboard_;

    std::vector<std::unique_ptr<StreamPipeline>> pipelines_;
    std::atomic<bool> running_{false};
    std::thread stats_thread_;

    FrameCallback global_output_callback_;

    mutable std::mutex stats_mutex_;
};