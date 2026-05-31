#pragma once

#include "utils/common.h"
#include "rtmp/rtmp_receiver.h"
#include "hls/hls_wrapper.h"
#include "edsr/edsr_model.h"
#include "metrics/fps_counter.h"
#include "metrics/psnr_evaluator.h"
#include "metrics/performance_dashboard.h"
#include "motion/motion_compensation.h"
#include "motion/temporal_fusion.h"
#include "motion/artifact_detector.h"
#include "scene/scene_classifier.h"
#include "scene/scene_model_manager.h"
#include "fpga/fpga_bitstream_loader.h"

class StreamPipeline {
public:
    StreamPipeline(int stream_id, const std::string& rtmp_url,
                   const ServiceConfig& config);
    ~StreamPipeline();

    bool initialize(EDSRQuantized& shared_quant_model,
                     EDSRModel& shared_cpu_model,
                     TemporalFusion* shared_temporal_fusion,
                     SceneClassifier* shared_scene_classifier = nullptr,
                     SceneModelManager* shared_model_manager = nullptr,
                     FPGABitstreamLoader* shared_fpga_loader = nullptr,
                     PerformanceDashboard* shared_dashboard = nullptr);

    void start();
    void stop();
    bool isRunning() const { return running_.load(); }

    void setOutputCallback(FrameCallback callback);

    StreamStats getStats() const;
    int getStreamId() const { return stream_id_; }

    void setReferenceFrameProvider(
        std::function<cv::Mat(int)> provider) {
        reference_provider_ = std::move(provider);
    }

    bool isMotionCompensationActive() const { return motion_comp_active_.load(); }
    bool isFallbackActive() const { return fallback_active_.load(); }

    SceneType getCurrentScene() const { return current_scene_.load(); }
    std::string getCurrentSceneName() const;
    double getCurrentSceneConfidence() const { return current_scene_confidence_.load(); }

    bool switchToScene(SceneType scene);
    bool forceScene(SceneType scene);

    PerformanceDashboard* getDashboard() { return dashboard_; }

private:
    void runLoop();
    bool processOneFrame();

    std::vector<uint8_t> processWithHLS(const uint8_t* input,
                                         int in_h, int in_w,
                                         int out_h, int out_w);
    std::vector<uint8_t> processWithCPU(const uint8_t* input,
                                         int in_h, int in_w,
                                         int out_h, int out_w);

    std::vector<uint8_t> applyMotionCompensation(
        const cv::Mat& prev_frame,
        const cv::Mat& curr_frame,
        const cv::Mat& next_frame,
        int out_h, int out_w);

    void checkAndHandleArtifacts(
        const cv::Mat& original,
        const cv::Mat& processed,
        double motion_score);

    void classifySceneIfNeeded(const cv::Mat& frame);
    bool checkAndPerformModelSwitch();
    void updateSceneStats(const SceneClassificationResult& result);

    double computeSSIM(const cv::Mat& img1, const cv::Mat& img2);

    int stream_id_;
    std::string rtmp_url_;
    ServiceConfig service_config_;

    std::unique_ptr<RTMPReceiver> receiver_;
    HLSWrapper* hls_wrapper_ = nullptr;
    EDSRModel* cpu_model_ = nullptr;
    EDSRQuantized* quant_model_ = nullptr;
    TemporalFusion* temporal_fusion_ = nullptr;
    SceneClassifier* scene_classifier_ = nullptr;
    SceneModelManager* model_manager_ = nullptr;
    FPGABitstreamLoader* fpga_loader_ = nullptr;
    PerformanceDashboard* dashboard_ = nullptr;

    std::unique_ptr<MotionCompensation> motion_comp_;
    std::unique_ptr<ArtifactDetector> artifact_detector_;

    FPSCounter fps_counter_;
    PSNREvaluator psnr_evaluator_;

    FrameCallback output_callback_;
    std::function<cv::Mat(int)> reference_provider_;

    std::atomic<bool> running_{false};
    std::atomic<bool> motion_comp_active_{false};
    std::atomic<bool> fallback_active_{false};
    std::atomic<SceneType> current_scene_{SceneType::MOVIE};
    std::atomic<double> current_scene_confidence_{0.0};
    std::thread worker_thread_;

    uint64_t frame_counter_ = 0;
    uint64_t dropped_frames_ = 0;
    uint64_t fallback_count_ = 0;
    uint64_t model_switch_count_ = 0;
    uint64_t frames_dropped_during_switch_ = 0;
    uint64_t frames_since_last_switch_ = 0;
    uint64_t last_classification_frame_ = 0;

    std::deque<double> latency_history_;
    int latency_window_ = 30;

    cv::Mat prev_frame_;
    cv::Mat next_frame_;
    bool has_prev_frame_ = false;
    bool has_next_frame_ = false;

    double last_motion_score_ = 0.0;
    int last_artifact_type_ = 0;

    SceneType pending_scene_switch_ = SceneType::UNKNOWN;
    uint64_t pending_scene_frames_ = 0;
    bool switch_in_progress_ = false;

    double current_psnr_ = 0.0;
    double current_ssim_ = 0.0;
    double average_psnr_ = 0.0;
    double average_ssim_ = 0.0;

    mutable std::mutex stats_mutex_;
    mutable std::mutex switch_mutex_;
    double current_latency_ms_ = 0.0;
    double average_latency_ms_ = 0.0;
};