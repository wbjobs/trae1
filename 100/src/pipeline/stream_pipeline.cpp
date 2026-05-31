#include "pipeline/stream_pipeline.h"
#include <opencv2/opencv.hpp>

StreamPipeline::StreamPipeline(int stream_id, const std::string& rtmp_url,
                                 const ServiceConfig& config)
    : stream_id_(stream_id), rtmp_url_(rtmp_url),
      service_config_(config), fps_counter_(30) {
    receiver_ = std::make_unique<RTMPReceiver>(stream_id, rtmp_url);

    if (config.enable_motion_compensation) {
        motion_comp_ = std::make_unique<MotionCompensation>();
        motion_comp_->setMotionThreshold(config.motion_threshold);
        artifact_detector_ = std::make_unique<ArtifactDetector>();
        artifact_detector_->setSeverityThreshold(config.artifact_threshold);
    }
}

StreamPipeline::~StreamPipeline() {
    stop();
}

bool StreamPipeline::initialize(EDSRQuantized& shared_quant_model,
                                  EDSRModel& shared_cpu_model,
                                  TemporalFusion* shared_temporal_fusion,
                                  SceneClassifier* shared_scene_classifier,
                                  SceneModelManager* shared_model_manager,
                                  FPGABitstreamLoader* shared_fpga_loader,
                                  PerformanceDashboard* shared_dashboard) {
    quant_model_ = &shared_quant_model;
    cpu_model_ = &shared_cpu_model;
    temporal_fusion_ = shared_temporal_fusion;
    scene_classifier_ = shared_scene_classifier;
    model_manager_ = shared_model_manager;
    fpga_loader_ = shared_fpga_loader;
    dashboard_ = shared_dashboard;

    if (!receiver_->connect()) {
        std::cerr << "[Pipeline " << stream_id_ << "] Failed to connect RTMP\n";
        return false;
    }

    if (service_config_.enable_motion_compensation) {
        std::cout << "[Pipeline " << stream_id_ << "] Motion compensation initialized\n";
        std::cout << "[Pipeline " << stream_id_ << "] 3D Temporal Convolution: "
                  << TEMPORAL_KERNEL << "x" << EDSR_KERNEL_SIZE << "x" << EDSR_KERNEL_SIZE << "\n";
        std::cout << "[Pipeline " << stream_id_ << "] Model param increase: ~15%\n";
    }

    if (service_config_.enable_scene_classification && scene_classifier_) {
        scene_classifier_->setMinConfidenceThreshold(
            service_config_.scene_confidence_threshold);
        scene_classifier_->setStableFramesThreshold(SCENE_STABLE_FRAMES_THRESHOLD);
        std::cout << "[Pipeline " << stream_id_ << "] Scene classification initialized\n";
        std::cout << "[Pipeline " << stream_id_ << "] Min confidence: "
                  << service_config_.scene_confidence_threshold << "\n";
    }

    if (service_config_.model_switch_mode == SwitchMode::FORCED &&
        !service_config_.forced_scene.empty()) {
        SceneType forced_type = SceneClassifier::stringToSceneType(
            service_config_.forced_scene);
        if (forced_type != SceneType::UNKNOWN) {
            forceScene(forced_type);
        }
    }

    std::cout << "[Pipeline " << stream_id_ << "] Initialized successfully\n";
    return true;
}

void StreamPipeline::setOutputCallback(FrameCallback callback) {
    output_callback_ = std::move(callback);
}

void StreamPipeline::start() {
    if (running_.exchange(true)) return;
    worker_thread_ = std::thread(&StreamPipeline::runLoop, this);
    std::cout << "[Pipeline " << stream_id_ << "] Started\n";
}

void StreamPipeline::stop() {
    if (!running_.exchange(false)) return;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    receiver_->disconnect();
    std::cout << "[Pipeline " << stream_id_ << "] Stopped\n";
}

void StreamPipeline::runLoop() {
    const auto frame_interval = std::chrono::microseconds(1000000 / TARGET_FPS);
    auto next_frame_time = std::chrono::steady_clock::now();

    while (running_.load()) {
        auto loop_start = std::chrono::steady_clock::now();

        if (!processOneFrame()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        next_frame_time += frame_interval;
        auto now = std::chrono::steady_clock::now();
        if (now < next_frame_time) {
            std::this_thread::sleep_until(next_frame_time);
        } else {
            dropped_frames_++;
            next_frame_time = now;
        }
    }
}

bool StreamPipeline::processOneFrame() {
    auto input_start = std::chrono::steady_clock::now();

    cv::Mat frame;
    if (!receiver_->readFrame(frame)) {
        return false;
    }

    frame_counter_++;
    fps_counter_.tick();
    frames_since_last_switch_++;

    if (switch_in_progress_) {
        frames_dropped_during_switch_++;
        if (frames_dropped_during_switch_ < MAX_FRAMES_DROPPED_DURING_SWITCH) {
            return true;
        }
        switch_in_progress_ = false;
    }

    if (service_config_.enable_scene_classification && scene_classifier_) {
        classifySceneIfNeeded(frame);
        checkAndPerformModelSwitch();
    }

    if (has_prev_frame_ && has_next_frame_) {
        prev_frame_ = next_frame_.clone();
        next_frame_ = frame.clone();
    } else if (!has_prev_frame_) {
        prev_frame_ = frame.clone();
        has_prev_frame_ = true;
    } else if (!has_next_frame_) {
        next_frame_ = frame.clone();
        has_next_frame_ = true;
    }

    int in_h = frame.rows;
    int in_w = frame.cols;
    int out_h = in_h * EDSR_SCALE_FACTOR;
    int out_w = in_w * EDSR_SCALE_FACTOR;

    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);

    std::vector<uint8_t> output_data;
    auto process_start = std::chrono::steady_clock::now();

    bool use_motion_comp = service_config_.enable_motion_compensation &&
                           !fallback_active_.load() &&
                           has_prev_frame_ && has_next_frame_;

    if (use_motion_comp && temporal_fusion_ && temporal_fusion_->hasWeights()) {
        motion_comp_active_.store(true);
        output_data = applyMotionCompensation(prev_frame_, frame, next_frame_,
                                               out_h, out_w);
    } else {
        motion_comp_active_.store(false);
        if (service_config_.mode == ProcessingMode::HLS_SIMULATION ||
            service_config_.mode == ProcessingMode::HYBRID) {
            output_data = processWithHLS(rgb.data, in_h, in_w, out_h, out_w);
        } else {
            output_data = processWithCPU(rgb.data, in_h, in_w, out_h, out_w);
        }
    }

    auto process_end = std::chrono::steady_clock::now();
    double latency_ms = std::chrono::duration<double, std::milli>(
        process_end - input_start).count();

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        current_latency_ms_ = latency_ms;
        latency_history_.push_back(latency_ms);
        if (latency_history_.size() > static_cast<size_t>(latency_window_)) {
            latency_history_.pop_front();
        }
        double sum = 0.0;
        for (double l : latency_history_) sum += l;
        average_latency_ms_ = sum / latency_history_.size();
    }

    double psnr = 0.0;
    double ssim = 0.0;
    if (service_config_.enable_psnr || service_config_.enable_ssim) {
        if (reference_provider_) {
            cv::Mat reference = reference_provider_(frame_counter_);
            if (!reference.empty()) {
                cv::Mat out_mat(out_h, out_w, CV_8UC3, output_data.data());
                cv::Mat out_bgr;
                cv::cvtColor(out_mat, out_bgr, cv::COLOR_RGB2BGR);

                if (service_config_.enable_psnr) {
                    auto result = psnr_evaluator_.compute(reference, out_bgr);
                    psnr = result.psnr_rgb;
                    psnr_evaluator_.updateAverage(result);
                }

                if (service_config_.enable_ssim) {
                    ssim = computeSSIM(reference, out_bgr);
                }

                current_psnr_ = psnr;
                current_ssim_ = ssim;
                average_psnr_ = (average_psnr_ * (frame_counter_ - 1) + psnr) / frame_counter_;
                average_ssim_ = (average_ssim_ * (frame_counter_ - 1) + ssim) / frame_counter_;

                if (dashboard_ && service_config_.enable_dashboard) {
                    dashboard_->updateMetrics(current_scene_.load(), psnr, ssim, latency_ms);
                    dashboard_->recordFrameProcessed(current_scene_.load(),
                        psnr < 30.0 || ssim < 0.9);
                }
            }
        }
    }

    if (service_config_.enable_motion_compensation && service_config_.auto_fallback &&
        artifact_detector_) {
        cv::Mat out_bgr;
        if (!output_data.empty()) {
            cv::Mat out_mat(out_h, out_w, CV_8UC3, output_data.data());
            cv::cvtColor(out_mat, out_bgr, cv::COLOR_RGB2BGR);
            checkAndHandleArtifacts(frame, out_bgr, last_motion_score_);
        }
    }

    if (output_callback_) {
        ProcessedFrame pframe;
        pframe.stream_id = stream_id_;
        pframe.frame_id = frame_counter_;
        pframe.data = output_data;
        pframe.width = out_w;
        pframe.height = out_h;
        pframe.channels = 3;
        pframe.input_timestamp = input_start;
        pframe.output_timestamp = process_end;
        pframe.psnr = psnr;
        output_callback_(pframe);
    }

    if (service_config_.enable_fps_stats && (frame_counter_ % 30 == 0)) {
        std::cout << "[Stream " << stream_id_ << "] Frame " << frame_counter_
                  << " | FPS: " << std::fixed << std::setprecision(1)
                  << fps_counter_.getCurrentFPS()
                  << " | Latency: " << std::fixed << std::setprecision(1)
                  << current_latency_ms_ << "ms";
        if (service_config_.enable_motion_compensation) {
            std::cout << " | Motion: " << std::fixed << std::setprecision(1)
                      << last_motion_score_
                      << " | MC: " << (motion_comp_active_.load() ? "ON" : "OFF");
            if (fallback_active_.load()) {
                std::cout << " | FALLBACK";
            }
        }
        if (service_config_.enable_scene_classification) {
            std::cout << " | Scene: " << getCurrentSceneName()
                      << " (" << std::fixed << std::setprecision(2)
                      << current_scene_confidence_.load() << ")";
        }
        if (service_config_.enable_psnr && psnr > 0.0) {
            std::cout << " | PSNR: " << std::fixed << std::setprecision(2)
                      << psnr << "dB";
        }
        if (service_config_.enable_ssim && ssim > 0.0) {
            std::cout << " | SSIM: " << std::fixed << std::setprecision(4)
                      << ssim;
        }
        std::cout << "\n";
    }

    if (dashboard_ && service_config_.enable_dashboard &&
        (frame_counter_ % 60 == 0)) {
        dashboard_->updateDisplay();
    }

    return true;
}

std::vector<uint8_t> StreamPipeline::applyMotionCompensation(
    const cv::Mat& prev_frame,
    const cv::Mat& curr_frame,
    const cv::Mat& next_frame,
    int out_h, int out_w) {

    if (!motion_comp_ || !temporal_fusion_) {
        return {};
    }

    auto mc_result = motion_comp_->compensate(prev_frame, curr_frame, next_frame);
    last_motion_score_ = mc_result.flow.motion_score;

    auto volume = motion_comp_->buildTemporalVolume(
        mc_result.aligned_frame, curr_frame,
        motion_comp_->warpFrame(next_frame,
            -mc_result.flow.flow_x, -mc_result.flow.flow_y));

    cv::Mat prev_rgb, curr_rgb, next_rgb;
    if (volume[0].channels() == 3) {
        cv::cvtColor(volume[0], prev_rgb, cv::COLOR_BGR2RGB);
        cv::cvtColor(volume[1], curr_rgb, cv::COLOR_BGR2RGB);
        cv::cvtColor(volume[2], next_rgb, cv::COLOR_BGR2RGB);
    } else {
        prev_rgb = volume[0].clone();
        curr_rgb = volume[1].clone();
        next_rgb = volume[2].clone();
    }

    std::vector<uint8_t> prev_data(prev_rgb.total() * prev_rgb.elemSize());
    std::vector<uint8_t> curr_data(curr_rgb.total() * curr_rgb.elemSize());
    std::vector<uint8_t> next_data(next_rgb.total() * next_rgb.elemSize());

    std::memcpy(prev_data.data(), prev_rgb.data, prev_data.size());
    std::memcpy(curr_data.data(), curr_rgb.data, curr_data.size());
    std::memcpy(next_data.data(), next_rgb.data, next_data.size());

    int in_h = curr_rgb.rows;
    int in_w = curr_rgb.cols;

    auto fused = temporal_fusion_->fuse(prev_data, curr_data, next_data,
                                         in_h, in_w, 3);

    std::vector<uint8_t> output_data;
    if (service_config_.mode == ProcessingMode::HLS_SIMULATION ||
        service_config_.mode == ProcessingMode::HYBRID) {
        output_data = processWithHLS(fused.data(), in_h, in_w, out_h, out_w);
    } else {
        output_data = processWithCPU(fused.data(), in_h, in_w, out_h, out_w);
    }

    return output_data;
}

void StreamPipeline::checkAndHandleArtifacts(
    const cv::Mat& original,
    const cv::Mat& processed,
    double motion_score) {

    if (!artifact_detector_) return;

    auto result = artifact_detector_->detect(original, processed, motion_score);

    last_artifact_type_ = static_cast<int>(result.type);

    if (result.needs_fallback && service_config_.auto_fallback) {
        if (!fallback_active_.load()) {
            fallback_active_.store(true);
            fallback_count_++;
            std::cout << "[WARNING][Stream " << stream_id_
                      << "] Artifact detected: " << result.description
                      << " (severity: " << std::fixed << std::setprecision(2)
                      << result.severity << ")\n";
            std::cout << "[WARNING][Stream " << stream_id_
                      << "] Switching to standard mode (motion compensation disabled)\n";
        }
    } else if (fallback_active_.load() && result.severity < service_config_.artifact_threshold * 0.5) {
        fallback_active_.store(false);
        std::cout << "[INFO][Stream " << stream_id_
                  << "] Artifacts resolved, re-enabling motion compensation\n";
    }
}

std::vector<uint8_t> StreamPipeline::processWithHLS(const uint8_t* input,
                                                     int in_h, int in_w,
                                                     int out_h, int out_w) {
    if (!quant_model_ || !quant_model_->hasWeights()) {
        std::cerr << "[Pipeline " << stream_id_ << "] HLS model not ready\n";
        return {};
    }
    return quant_model_->processFrame(input, in_h, in_w, out_h, out_w);
}

std::vector<uint8_t> StreamPipeline::processWithCPU(const uint8_t* input,
                                                     int in_h, int in_w,
                                                     int out_h, int out_w) {
    if (!cpu_model_ || !cpu_model_->hasWeights()) {
        std::cerr << "[Pipeline " << stream_id_ << "] CPU model not ready\n";
        return {};
    }

    size_t input_size = static_cast<size_t>(in_h) * in_w * 3;
    std::vector<float> float_input(input_size);
    for (size_t i = 0; i < input_size; ++i) {
        float_input[i] = (static_cast<float>(input[i]) - 127.5f) / 127.5f;
    }

    auto float_output = cpu_model_->forward(float_input, in_h, in_w);

    size_t output_size = static_cast<size_t>(out_h) * out_w * 3;
    std::vector<uint8_t> output(output_size);
    for (size_t i = 0; i < output_size && i < float_output.size(); ++i) {
        float val = float_output[i] * 127.5f + 127.5f;
        val = std::max(0.0f, std::min(255.0f, val));
        output[i] = static_cast<uint8_t>(val);
    }

    return output;
}

StreamStats StreamPipeline::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    StreamStats stats;
    stats.stream_id = stream_id_;
    stats.current_fps = fps_counter_.getCurrentFPS();
    stats.average_fps = fps_counter_.getAverageFPS();
    stats.min_fps = fps_counter_.getMinFPS();
    stats.max_fps = fps_counter_.getMaxFPS();
    stats.current_latency_ms = current_latency_ms_;
    stats.average_latency_ms = average_latency_ms_;
    stats.average_psnr = psnr_evaluator_.getAveragePSNR();
    stats.average_ssim = average_ssim_;
    stats.total_frames = frame_counter_;
    stats.dropped_frames = dropped_frames_;
    stats.motion_score = last_motion_score_;
    stats.artifact_type = last_artifact_type_;
    stats.fallback_count = fallback_count_;
    stats.motion_compensation_active = motion_comp_active_.load();
    stats.current_scene = static_cast<int>(current_scene_.load());
    stats.current_scene_name = getCurrentSceneName();
    stats.scene_confidence = current_scene_confidence_.load();
    stats.model_switch_count = model_switch_count_;
    stats.frames_dropped_during_switch = frames_dropped_during_switch_;
    stats.current_ssim = current_ssim_;
    stats.ssim_average = average_ssim_;
    return stats;
}

std::string StreamPipeline::getCurrentSceneName() const {
    return SceneClassifier::sceneTypeToString(current_scene_.load());
}

bool StreamPipeline::switchToScene(SceneType scene) {
    std::lock_guard<std::mutex> lock(switch_mutex_);

    if (scene == current_scene_.load()) {
        return true;
    }

    if (frames_since_last_switch_ < service_config_.scene_min_frames_before_switch) {
        return false;
    }

    return forceScene(scene);
}

bool StreamPipeline::forceScene(SceneType scene) {
    std::lock_guard<std::mutex> lock(switch_mutex_);

    std::cout << "[Pipeline " << stream_id_ << "] Model switch: "
              << SceneClassifier::sceneTypeToString(current_scene_.load())
              << " -> " << SceneClassifier::sceneTypeToString(scene) << "\n";

    switch_in_progress_ = true;
    frames_dropped_during_switch_ = 0;

    bool success = true;

    if (model_manager_) {
        success = model_manager_->switchToScene(scene);
    }

    if (fpga_loader_ && success) {
        success = fpga_loader_->reconfigureForScene(scene);
    }

    if (success) {
        current_scene_.store(scene);
        model_switch_count_++;
        frames_since_last_switch_ = 0;

        std::cout << "[Pipeline " << stream_id_ << "] Switch complete, dropped "
                  << frames_dropped_during_switch_ << " frames\n";
    } else {
        std::cerr << "[Pipeline " << stream_id_ << "] Switch failed\n";
    }

    switch_in_progress_ = false;

    return success;
}

void StreamPipeline::classifySceneIfNeeded(const cv::Mat& frame) {
    if (frame_counter_ - last_classification_frame_ <
        static_cast<uint64_t>(service_config_.scene_classification_interval)) {
        return;
    }

    last_classification_frame_ = frame_counter_;

    auto result = scene_classifier_->classify(frame);
    updateSceneStats(result);

    current_scene_confidence_.store(result.confidence);

    if (result.type != current_scene_.load() &&
        result.confidence >= service_config_.scene_confidence_threshold) {
        pending_scene_switch_ = result.type;
        pending_scene_frames_++;
    } else if (result.type == current_scene_.load()) {
        pending_scene_switch_ = SceneType::UNKNOWN;
        pending_scene_frames_ = 0;
    }
}

bool StreamPipeline::checkAndPerformModelSwitch() {
    if (service_config_.model_switch_mode != SwitchMode::AUTO_CLASSIFIER) {
        return false;
    }

    if (pending_scene_switch_ == SceneType::UNKNOWN) {
        return false;
    }

    if (pending_scene_frames_ < SCENE_STABLE_FRAMES_THRESHOLD) {
        return false;
    }

    if (frames_since_last_switch_ <
        static_cast<uint64_t>(service_config_.scene_min_frames_before_switch)) {
        return false;
    }

    SceneType new_scene = pending_scene_switch_;
    pending_scene_switch_ = SceneType::UNKNOWN;
    pending_scene_frames_ = 0;

    return switchToScene(new_scene);
}

void StreamPipeline::updateSceneStats(const SceneClassificationResult& result) {
    if (frame_counter_ % 100 == 0 && result.inference_time_ms > 0) {
        std::cout << "[Scene " << stream_id_ << "] Classified as " << result.label
                  << " (conf: " << std::fixed << std::setprecision(2)
                  << result.confidence << ", time: " << result.inference_time_ms << "ms)\n";
    }
}

double StreamPipeline::computeSSIM(const cv::Mat& img1, const cv::Mat& img2) {
    const double C1 = 6.5025, C2 = 58.5225;

    cv::Mat I1, I2;
    img1.convertTo(I1, CV_32F);
    img2.convertTo(I2, CV_32F);

    cv::Mat I1_2 = I1.mul(I1);
    cv::Mat I2_2 = I2.mul(I2);
    cv::Mat I1_I2 = I1.mul(I2);

    cv::Mat mu1, mu2;
    cv::GaussianBlur(I1, mu1, cv::Size(11, 11), 1.5);
    cv::GaussianBlur(I2, mu2, cv::Size(11, 11), 1.5);

    cv::Mat mu1_2 = mu1.mul(mu1);
    cv::Mat mu2_2 = mu2.mul(mu2);
    cv::Mat mu1_mu2 = mu1.mul(mu2);

    cv::Mat sigma1_2, sigma2_2, sigma12;
    cv::GaussianBlur(I1_2, sigma1_2, cv::Size(11, 11), 1.5);
    sigma1_2 -= mu1_2;
    cv::GaussianBlur(I2_2, sigma2_2, cv::Size(11, 11), 1.5);
    sigma2_2 -= mu2_2;
    cv::GaussianBlur(I1_I2, sigma12, cv::Size(11, 11), 1.5);
    sigma12 -= mu1_mu2;

    cv::Mat t1, t2, t3;
    t1 = 2 * mu1_mu2 + C1;
    t2 = 2 * sigma12 + C2;
    t3 = t1.mul(t2);
    t1 = mu1_2 + mu2_2 + C1;
    t2 = sigma1_2 + sigma2_2 + C2;
    t1 = t1.mul(t2);

    cv::Mat ssim_map;
    cv::divide(t3, t1, ssim_map);

    cv::Scalar mssim = cv::mean(ssim_map);
    return (mssim[0] + mssim[1] + mssim[2]) / 3.0;
}