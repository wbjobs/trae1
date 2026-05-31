#include "pipeline/pipeline_manager.h"

PipelineManager::PipelineManager(const ServiceConfig& config)
    : config_(config),
      cpu_model_(edsr_config_),
      quant_model_(edsr_config_),
      hls_wrapper_(edsr_config_),
      temporal_fusion_() {
    if (config_.enable_scene_classification) {
        scene_classifier_ = std::make_unique<SceneClassifier>();
    }

    model_manager_ = std::make_unique<SceneModelManager>();
    g_scene_model_manager = model_manager_;

    fpga_loader_ = std::make_unique<FPGABitstreamLoader>();
    g_fpga_loader = fpga_loader_;

    if (config_.enable_dashboard) {
        dashboard_ = std::make_unique<PerformanceDashboard>();
    }
}

PipelineManager::~PipelineManager() {
    stopAll();
    g_scene_model_manager = nullptr;
    g_fpga_loader = nullptr;
}

bool PipelineManager::initialize() {
    std::cout << "\n=== Initializing Pipeline Manager ===\n";

    if (config_.enable_dashboard && dashboard_) {
        DashboardConfig dash_config;
        dash_config.update_interval_ms = config_.dashboard_update_interval_ms;
        dashboard_->initialize(dash_config);
        std::cout << "[Manager] Performance dashboard initialized\n";
    }

    if (!loadModels()) {
        std::cerr << "[Manager] Failed to load models\n";
        return false;
    }

    if (config_.enable_scene_classification && scene_classifier_) {
        if (!scene_classifier_->initialize(config_.classifier_model_path)) {
            std::cerr << "[Manager] Failed to initialize scene classifier\n";
            return false;
        }
        g_scene_classifier = scene_classifier_.get();
        std::cout << "[Manager] Scene classifier initialized (MobileNet, target <10ms)\n";
    }

    if (model_manager_) {
        if (!model_manager_->initialize(config_.models_dir)) {
            std::cerr << "[Manager] Failed to initialize scene model manager\n";
            return false;
        }
        std::cout << "[Manager] Scene model manager initialized with "
                  << model_manager_->getAllModelInfo().size() << " models\n";
    }

    if (fpga_loader_) {
        fpga_loader_->initialize(FPGAType::SIMULATION);
        fpga_loader_->setSimulationReconfigTime(200);
        std::cout << "[Manager] FPGA bitstream loader initialized (simulation mode)\n";
        std::cout << "[Manager] Partial reconfiguration time: ~200ms, max 3 frames dropped\n";

        for (auto& [type, info] : model_manager_->getAllModelInfo()) {
            if (!info.bitstream_path.empty()) {
                fpga_loader_->registerBitstream(type, info.bitstream_path);
            }
        }
    }

    if (!config_.custom_model_path.empty() && model_manager_) {
        std::string error_msg;
        if (!uploadCustomModel(config_.custom_model_path,
                               config_.custom_model_name,
                               config_.custom_model_description,
                               error_msg)) {
            std::cerr << "[Manager] Warning: Failed to load custom model: "
                      << error_msg << "\n";
        }
    }

    if (config_.enable_motion_compensation) {
        temporal_fusion_.initDefaultWeights();
        std::cout << "[Manager] Temporal Fusion (3D Conv 3x3x3) initialized\n";
        size_t base_params = static_cast<size_t>(EDSR_NUM_FEATURES) * EDSR_NUM_FEATURES *
                             EDSR_NUM_RESIDUAL_BLOCKS * EDSR_KERNEL_SIZE * EDSR_KERNEL_SIZE * 2;
        size_t temporal_params = TemporalFusion::calculateWeightSize(temporal_fusion_.getConfig());
        double param_increase = static_cast<double>(temporal_params) / base_params * 100.0;
        std::cout << "[Manager] Temporal params: " << temporal_params
                  << " (increase: " << std::fixed << std::setprecision(1)
                  << param_increase << "%)\n";
    }

    setupStreams();

    for (auto& pipeline : pipelines_) {
        if (!pipeline->initialize(quant_model_, cpu_model_,
                                    config_.enable_motion_compensation ? &temporal_fusion_ : nullptr,
                                    scene_classifier_.get(),
                                    model_manager_.get(),
                                    fpga_loader_.get(),
                                    dashboard_.get())) {
            std::cerr << "[Manager] Failed to initialize pipeline "
                      << pipeline->getStreamId() << "\n";
            return false;
        }
    }

    std::cout << "[Manager] All pipelines initialized successfully\n";
    return true;
}

bool PipelineManager::loadModels() {
    std::cout << "[Manager] Loading model weights from: "
              << config_.model_path << "\n";

    auto& weights = const_cast<EDSRWeights&>(quant_model_.getWeights());

    if (!EDSRWeightsLoader::loadFromFile(config_.model_path, weights)) {
        std::cout << "[Manager] Failed to load from file, generating default weights\n";
        EDSRWeightsLoader::generateRandomWeights(weights, edsr_config_);
        cpu_model_.initDefaultWeights();
        quant_model_.initFromModel(cpu_model_);
    } else {
        cpu_model_.initDefaultWeights();
        quant_model_.initFromModel(cpu_model_);
    }

    if (!quant_model_.hasWeights()) {
        std::cerr << "[Manager] Quantized model weights not available\n";
        return false;
    }

    std::cout << "[Manager] Models loaded successfully\n";
    std::cout << "[Manager] Running HLS performance simulation...\n";
    hls_wrapper_.loadWeights(config_.model_path);

    auto perf = hls_wrapper_.getPerformanceInfo();
    std::cout << "[Manager] Estimated latency: "
              << std::fixed << std::setprecision(2)
              << perf.estimated_latency_ms << "ms\n";

    if (config_.enable_motion_compensation) {
        double mc_overhead = 5.0;
        std::cout << "[Manager] Motion compensation overhead: ~"
                  << std::fixed << std::setprecision(1)
                  << mc_overhead << "ms (optical flow + 3D conv)\n";
        std::cout << "[Manager] Total estimated latency: "
                  << std::fixed << std::setprecision(2)
                  << (perf.estimated_latency_ms + mc_overhead) << "ms\n";
    }

    return true;
}

void PipelineManager::setupStreams() {
    int num_streams = std::min(config_.num_streams, MAX_STREAMS);
    if (num_streams > static_cast<int>(config_.rtmp_urls.size())) {
        num_streams = static_cast<int>(config_.rtmp_urls.size());
    }

    std::cout << "[Manager] Setting up " << num_streams << " streams\n";

    for (int i = 0; i < num_streams; ++i) {
        const std::string& url = config_.rtmp_urls[i];
        auto pipeline = std::make_unique<StreamPipeline>(i, url, config_);

        if (global_output_callback_) {
            pipeline->setOutputCallback(global_output_callback_);
        }

        pipelines_.push_back(std::move(pipeline));
    }
}

void PipelineManager::setGlobalOutputCallback(FrameCallback callback) {
    global_output_callback_ = std::move(callback);
    for (auto& pipeline : pipelines_) {
        pipeline->setOutputCallback(global_output_callback_);
    }
}

void PipelineManager::startAll() {
    if (running_.exchange(true)) return;

    std::cout << "\n=== Starting all pipelines ===\n";
    for (auto& pipeline : pipelines_) {
        pipeline->start();
    }

    if (config_.enable_fps_stats) {
        stats_thread_ = std::thread(&PipelineManager::statsPrintingLoop, this);
    }
}

void PipelineManager::stopAll() {
    if (!running_.exchange(false)) return;

    std::cout << "\n=== Stopping all pipelines ===\n";
    for (auto& pipeline : pipelines_) {
        pipeline->stop();
    }

    if (stats_thread_.joinable()) {
        stats_thread_.join();
    }
}

void PipelineManager::statsPrintingLoop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (running_.load()) {
            printStatsReport();
        }
    }
}

GlobalStats PipelineManager::getGlobalStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    GlobalStats stats;
    stats.per_stream = getStreamStats();

    stats.total_bandwidth_mbps = 0.0;
    for (const auto& s : stats.per_stream) {
        double bytes_per_sec = s.current_fps * OUTPUT_WIDTH * OUTPUT_HEIGHT * 3;
        stats.total_bandwidth_mbps += bytes_per_sec * 8.0 / (1024.0 * 1024.0);
    }

    stats.system_cpu_usage = 0.0;
    stats.total_memory_mb = 0;
    return stats;
}

std::vector<StreamStats> PipelineManager::getStreamStats() const {
    std::vector<StreamStats> all_stats;
    all_stats.reserve(pipelines_.size());
    for (const auto& pipeline : pipelines_) {
        all_stats.push_back(pipeline->getStats());
    }
    return all_stats;
}

void PipelineManager::printStatsReport() const {
    auto stats = getStreamStats();
    std::cout << "\n" << std::string(140, '=') << "\n";
    std::cout << "                         MULTI-STREAM STATISTICS REPORT"
              << (config_.enable_motion_compensation ? " (Motion Compensation)" : "")
              << (config_.enable_scene_classification ? " (Adaptive Scene Switching)" : "")
              << "\n";
    std::cout << std::string(140, '=') << "\n";
    std::cout << std::setw(8) << "Stream"
              << std::setw(10) << "FPS (cur)"
              << std::setw(10) << "FPS (avg)"
              << std::setw(12) << "Lat(ms)"
              << std::setw(12) << "PSNR(dB)"
              << std::setw(12) << "SSIM"
              << std::setw(12) << "Scene"
              << std::setw(10) << "Frames"
              << std::setw(10) << "Dropped";

    if (config_.enable_motion_compensation) {
        std::cout << std::setw(10) << "Motion"
                  << std::setw(8) << "MC"
                  << std::setw(10) << "Fallback";
    }

    if (config_.enable_scene_classification) {
        std::cout << std::setw(8) << "Switches";
    }

    std::cout << "\n";
    std::cout << std::string(140, '-') << "\n";

    double total_fps = 0.0;
    double total_latency = 0.0;
    uint64_t total_frames = 0;
    uint64_t total_dropped = 0;
    double total_psnr = 0.0;
    double total_ssim = 0.0;
    int psnr_count = 0;
    int ssim_count = 0;
    uint64_t total_switches = 0;

    for (const auto& s : stats) {
        std::cout << std::setw(8) << s.stream_id
                  << std::setw(10) << std::fixed << std::setprecision(1) << s.current_fps
                  << std::setw(10) << std::fixed << std::setprecision(1) << s.average_fps
                  << std::setw(12) << std::fixed << std::setprecision(1) << s.average_latency_ms
                  << std::setw(12);
        if (s.average_psnr > 0.0) {
            std::cout << std::fixed << std::setprecision(2) << s.average_psnr;
            total_psnr += s.average_psnr;
            psnr_count++;
        } else {
            std::cout << "N/A";
        }
        std::cout << std::setw(12);
        if (s.average_ssim > 0.0) {
            std::cout << std::fixed << std::setprecision(4) << s.average_ssim;
            total_ssim += s.average_ssim;
            ssim_count++;
        } else {
            std::cout << "N/A";
        }
        std::cout << std::setw(12) << s.current_scene_name
                  << std::setw(10) << s.total_frames
                  << std::setw(10) << s.dropped_frames;

        if (config_.enable_motion_compensation) {
            std::cout << std::setw(10) << std::fixed << std::setprecision(1) << s.motion_score
                      << std::setw(8) << (s.motion_compensation_active ? "ON" : "OFF")
                      << std::setw(10) << s.fallback_count;
        }

        if (config_.enable_scene_classification) {
            std::cout << std::setw(8) << s.model_switch_count;
            total_switches += s.model_switch_count;
        }

        std::cout << "\n";
        total_fps += s.average_fps;
        total_latency += s.average_latency_ms;
        total_frames += s.total_frames;
        total_dropped += s.dropped_frames;
    }

    std::cout << std::string(140, '-') << "\n";
    std::cout << std::setw(8) << "TOTAL"
              << std::setw(10) << std::fixed << std::setprecision(1) << total_fps
              << std::setw(10) << std::fixed << std::setprecision(1)
              << (stats.empty() ? 0.0 : total_fps / stats.size())
              << std::setw(12) << std::fixed << std::setprecision(1)
              << (stats.empty() ? 0.0 : total_latency / stats.size())
              << std::setw(12);
    if (psnr_count > 0) {
        std::cout << std::fixed << std::setprecision(2) << (total_psnr / psnr_count);
    } else {
        std::cout << "N/A";
    }
    std::cout << std::setw(12);
    if (ssim_count > 0) {
        std::cout << std::fixed << std::setprecision(4) << (total_ssim / ssim_count);
    } else {
        std::cout << "N/A";
    }
    std::cout << std::setw(12) << "-"
              << std::setw(10) << total_frames
              << std::setw(10) << total_dropped;

    if (config_.enable_motion_compensation) {
        std::cout << std::setw(10) << "-"
                  << std::setw(8) << "-"
                  << std::setw(10) << "-";
    }

    if (config_.enable_scene_classification) {
        std::cout << std::setw(8) << total_switches;
    }

    std::cout << "\n";
    std::cout << std::string(140, '=') << "\n";

    if (config_.enable_dashboard && dashboard_) {
        std::cout << dashboard_->generateTextReport();
    }

    std::cout << "\n";
}

bool PipelineManager::uploadCustomModel(const std::string& source_path,
                                        const std::string& name,
                                        const std::string& description,
                                        std::string& error_msg) {
    if (!model_manager_) {
        error_msg = "Model manager not initialized";
        return false;
    }

    return model_manager_->uploadCustomModel(source_path, name, description, error_msg);
}

bool PipelineManager::exportDashboardReport(const std::string& filepath) {
    if (!dashboard_) {
        return false;
    }
    dashboard_->exportMetricsToFile(filepath);
    return true;
}