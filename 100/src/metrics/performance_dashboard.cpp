#include "metrics/performance_dashboard.h"

#include <sstream>
#include <iomanip>
#include <fstream>

PerformanceDashboard::PerformanceDashboard() {
    last_update_time_ = std::chrono::system_clock::now();
}

PerformanceDashboard::~PerformanceDashboard() {}

bool PerformanceDashboard::initialize(const DashboardConfig& config) {
    config_ = config;

    initializeSceneMetrics(SceneType::ANIMATION);
    initializeSceneMetrics(SceneType::SPORTS);
    initializeSceneMetrics(SceneType::MOVIE);
    initializeSceneMetrics(SceneType::SURVEILLANCE);
    initializeSceneMetrics(SceneType::CUSTOM);

    std::cout << "[Dashboard] Initialized with " << scene_metrics_.size()
              << " scene metrics trackers\n";

    return true;
}

void PerformanceDashboard::updateMetrics(SceneType scene, double psnr, double ssim, double latency_ms) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    auto it = scene_metrics_.find(scene);
    if (it == scene_metrics_.end()) {
        initializeSceneMetrics(scene);
        it = scene_metrics_.find(scene);
    }

    ScenePerfMetrics& metrics = it->second;
    updateRunningAverage(metrics, psnr, ssim, latency_ms);

    total_frames_processed_++;
}

void PerformanceDashboard::recordFrameProcessed(SceneType scene, bool below_threshold) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    auto it = scene_metrics_.find(scene);
    if (it == scene_metrics_.end()) {
        initializeSceneMetrics(scene);
        it = scene_metrics_.find(scene);
    }

    ScenePerfMetrics& metrics = it->second;
    metrics.total_frames++;
    if (below_threshold) {
        metrics.frames_below_threshold++;
    }
}

ScenePerfMetrics* PerformanceDashboard::getMetrics(SceneType scene) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    auto it = scene_metrics_.find(scene);
    if (it != scene_metrics_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::string PerformanceDashboard::generateTextReport() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    std::ostringstream oss;
    oss << "\n" << std::string(80, '=') << "\n";
    oss << "                  PERFORMANCE DASHBOARD - SCENE MODEL COMPARISON\n";
    oss << std::string(80, '=') << "\n\n";

    oss << "Total Frames Processed: " << total_frames_processed_ << "\n";
    oss << "Overall Average PSNR: " << std::fixed << std::setprecision(2)
        << getOverallAveragePSNR() << " dB\n";
    oss << "Overall Average SSIM: " << std::fixed << std::setprecision(4)
        << getOverallAverageSSIM() << "\n\n";

    oss << generateComparisonTable();

    oss << "\n" << std::string(80, '-') << "\n";
    oss << "Quality Legend:\n";
    oss << "  PSNR: >40dB=Excellent | >35dB=Good | >30dB=Fair | <30dB=Poor\n";
    oss << "  SSIM: >0.99=Excellent | >0.98=Good | >0.95=Fair | <0.95=Poor\n";
    oss << std::string(80, '=') << "\n";

    return oss.str();
}

std::string PerformanceDashboard::generateComparisonTable() {
    std::ostringstream oss;

    oss << std::left
        << std::setw(16) << "Scene"
        << std::setw(12) << "PSNR (dB)"
        << std::setw(10) << "Δ vs Ref"
        << std::setw(12) << "SSIM"
        << std::setw(10) << "Δ vs Ref"
        << std::setw(10) << "Latency"
        << std::setw(10) << "Frames"
        << std::setw(12) << "Quality\n";
    oss << std::string(92, '-') << "\n";

    double ref_psnr = 0.0;
    double ref_ssim = 0.0;
    auto ref_it = scene_metrics_.find(reference_scene_);
    if (ref_it != scene_metrics_.end()) {
        ref_psnr = ref_it->second.psnr_average;
        ref_ssim = ref_it->second.ssim_average;
    }

    std::vector<SceneType> scene_order = {
        SceneType::MOVIE,
        SceneType::ANIMATION,
        SceneType::SPORTS,
        SceneType::SURVEILLANCE,
        SceneType::CUSTOM
    };

    for (SceneType type : scene_order) {
        auto it = scene_metrics_.find(type);
        if (it != scene_metrics_.end()) {
            bool is_ref = (type == reference_scene_);
            oss << formatMetricsRow(it->second, is_ref);
        }
    }

    return oss.str();
}

std::string PerformanceDashboard::generateJSONReport() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"overall\": {\n";
    oss << "    \"total_frames\": " << total_frames_processed_ << ",\n";
    oss << "    \"avg_psnr\": " << std::fixed << std::setprecision(4)
        << getOverallAveragePSNR() << ",\n";
    oss << "    \"avg_ssim\": " << std::fixed << std::setprecision(6)
        << getOverallAverageSSIM() << ",\n";
    oss << "    \"reference_scene\": \""
        << SceneClassifier::sceneTypeToString(reference_scene_) << "\",\n";
    oss << "    \"best_scene\": \"" << getBestScene() << "\",\n";
    oss << "    \"worst_scene\": \"" << getWorstScene() << "\"\n";
    oss << "  },\n";
    oss << "  \"scenes\": {\n";

    bool first = true;
    for (auto& [type, metrics] : scene_metrics_) {
        if (!first) oss << ",\n";
        first = false;

        oss << "    \"" << SceneClassifier::sceneTypeToString(type) << "\": {\n";
        oss << "      \"name\": \"" << metrics.name << "\",\n";
        oss << "      \"psnr\": {\n";
        oss << "        \"current\": " << std::fixed << std::setprecision(4)
            << metrics.psnr_current << ",\n";
        oss << "        \"average\": " << std::fixed << std::setprecision(4)
            << metrics.psnr_average << ",\n";
        oss << "        \"best\": " << std::fixed << std::setprecision(4)
            << metrics.psnr_best << ",\n";
        oss << "        \"worst\": " << std::fixed << std::setprecision(4)
            << metrics.psnr_worst << "\n";
        oss << "      },\n";
        oss << "      \"ssim\": {\n";
        oss << "        \"current\": " << std::fixed << std::setprecision(6)
            << metrics.ssim_current << ",\n";
        oss << "        \"average\": " << std::fixed << std::setprecision(6)
            << metrics.ssim_average << ",\n";
        oss << "        \"best\": " << std::fixed << std::setprecision(6)
            << metrics.ssim_best << ",\n";
        oss << "        \"worst\": " << std::fixed << std::setprecision(6)
            << metrics.ssim_worst << "\n";
        oss << "      },\n";
        oss << "      \"total_frames\": " << metrics.total_frames << ",\n";
        oss << "      \"frames_below_threshold\": " << metrics.frames_below_threshold << ",\n";
        oss << "      \"avg_latency_ms\": " << std::fixed << std::setprecision(2)
            << metrics.average_latency_ms << "\n";
        oss << "    }";
    }

    oss << "\n  }\n";
    oss << "}\n";

    return oss.str();
}

std::string PerformanceDashboard::getBestScene() const {
    double best_psnr = 0.0;
    std::string best_name = "N/A";

    for (auto& [type, metrics] : scene_metrics_) {
        if (metrics.total_frames > 0 && metrics.psnr_average > best_psnr) {
            best_psnr = metrics.psnr_average;
            best_name = metrics.name;
        }
    }

    return best_name;
}

std::string PerformanceDashboard::getWorstScene() const {
    double worst_psnr = 1000.0;
    std::string worst_name = "N/A";

    for (auto& [type, metrics] : scene_metrics_) {
        if (metrics.total_frames > 0 && metrics.psnr_average < worst_psnr) {
            worst_psnr = metrics.psnr_average;
            worst_name = metrics.name;
        }
    }

    return worst_name;
}

double PerformanceDashboard::getOverallAveragePSNR() const {
    double sum = 0.0;
    int count = 0;

    for (auto& [type, metrics] : scene_metrics_) {
        if (metrics.total_frames > 0) {
            sum += metrics.psnr_average;
            count++;
        }
    }

    return count > 0 ? sum / count : 0.0;
}

double PerformanceDashboard::getOverallAverageSSIM() const {
    double sum = 0.0;
    int count = 0;

    for (auto& [type, metrics] : scene_metrics_) {
        if (metrics.total_frames > 0) {
            sum += metrics.ssim_average;
            count++;
        }
    }

    return count > 0 ? sum / count : 0.0;
}

void PerformanceDashboard::resetAllMetrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    for (auto& [type, metrics] : scene_metrics_) {
        metrics = ScenePerfMetrics();
        metrics.type = type;
        metrics.name = SceneClassifier::sceneTypeToString(type);
    }

    total_frames_processed_ = 0;
    last_update_time_ = std::chrono::system_clock::now();
}

void PerformanceDashboard::resetSceneMetrics(SceneType scene) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    initializeSceneMetrics(scene);
}

void PerformanceDashboard::updateDisplay() {
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_update_time_);

    if (elapsed.count() >= config_.update_interval_ms) {
        printSummary();
        last_update_time_ = now;
    }
}

void PerformanceDashboard::printSummary() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    std::cout << "\r" << std::string(120, ' ') << "\r";
    std::cout << "[Dashboard] ";

    bool first = true;
    for (auto& [type, metrics] : scene_metrics_) {
        if (metrics.total_frames > 0) {
            if (!first) std::cout << " | ";
            first = false;
            std::cout << metrics.name << ": PSNR="
                      << std::fixed << std::setprecision(1) << metrics.psnr_average
                      << " SSIM=" << std::fixed << std::setprecision(3)
                      << metrics.ssim_average;
        }
    }

    std::cout << std::flush;
}

void PerformanceDashboard::exportMetricsToFile(const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[Dashboard] Failed to open file for export: " << filepath << "\n";
        return;
    }

    file << generateJSONReport();
    std::cout << "[Dashboard] Metrics exported to: " << filepath << "\n";
}

void PerformanceDashboard::initializeSceneMetrics(SceneType type) {
    ScenePerfMetrics metrics;
    metrics.type = type;
    metrics.name = SceneClassifier::sceneTypeToString(type);
    scene_metrics_[type] = metrics;
}

void PerformanceDashboard::updateRunningAverage(ScenePerfMetrics& metrics,
                                                double psnr, double ssim, double latency) {
    metrics.psnr_current = psnr;
    metrics.ssim_current = ssim;
    metrics.inference_time_ms = latency;

    if (psnr > metrics.psnr_best) metrics.psnr_best = psnr;
    if (psnr < metrics.psnr_worst || metrics.psnr_worst == 0.0) metrics.psnr_worst = psnr;
    if (ssim > metrics.ssim_best) metrics.ssim_best = ssim;
    if (ssim < metrics.ssim_worst || metrics.ssim_worst == 0.0) metrics.ssim_worst = ssim;

    metrics.psnr_average = (metrics.psnr_average * metrics.total_frames + psnr) /
                           (metrics.total_frames + 1);
    metrics.ssim_average = (metrics.ssim_average * metrics.total_frames + ssim) /
                           (metrics.total_frames + 1);
    metrics.average_latency_ms = (metrics.average_latency_ms * metrics.total_frames + latency) /
                                  (metrics.total_frames + 1);

    metrics.recent_samples.push_back({psnr, ssim});
    if (metrics.recent_samples.size() > ScenePerfMetrics::MAX_RECENT_SAMPLES) {
        metrics.recent_samples.erase(metrics.recent_samples.begin());
    }

    metrics.total_frames++;

    if (psnr < config_.psnr_warning_threshold || ssim < config_.ssim_warning_threshold) {
        metrics.frames_below_threshold++;
    }
}

std::string PerformanceDashboard::formatMetricsRow(const ScenePerfMetrics& metrics,
                                                   bool is_reference) {
    std::ostringstream oss;

    double ref_psnr = 0.0;
    double ref_ssim = 0.0;
    auto ref_it = scene_metrics_.find(reference_scene_);
    if (ref_it != scene_metrics_.end()) {
        ref_psnr = ref_it->second.psnr_average;
        ref_ssim = ref_it->second.ssim_average;
    }

    double psnr_delta = metrics.psnr_average - ref_psnr;
    double ssim_delta = metrics.ssim_average - ref_ssim;

    std::string name = metrics.name;
    if (is_reference) name += " *";

    oss << std::left
        << std::setw(16) << name
        << std::setw(12) << (std::ostringstream() << std::fixed << std::setprecision(2)
                              << metrics.psnr_average).str();

    std::ostringstream delta_psnr;
    delta_psnr << std::fixed << std::setprecision(2)
               << (psnr_delta >= 0 ? "+" : "") << psnr_delta;
    oss << std::setw(10) << delta_psnr.str();

    oss << std::setw(12) << (std::ostringstream() << std::fixed << std::setprecision(4)
                              << metrics.ssim_average).str();

    std::ostringstream delta_ssim;
    delta_ssim << std::fixed << std::setprecision(4)
               << (ssim_delta >= 0 ? "+" : "") << ssim_delta;
    oss << std::setw(10) << delta_ssim.str();

    oss << std::setw(10) << (std::ostringstream() << std::fixed << std::setprecision(1)
                              << metrics.average_latency_ms << "ms").str()
        << std::setw(10) << metrics.total_frames
        << std::setw(12) << getPSNRQualityLabel(metrics.psnr_average)
        << "\n";

    return oss.str();
}

std::string PerformanceDashboard::getSSIMQualityLabel(double ssim) const {
    if (ssim >= 0.99) return "Excellent";
    if (ssim >= 0.98) return "Good";
    if (ssim >= 0.95) return "Fair";
    return "Poor";
}

std::string PerformanceDashboard::getPSNRQualityLabel(double psnr) const {
    if (psnr >= 40.0) return "Excellent";
    if (psnr >= 35.0) return "Good";
    if (psnr >= 30.0) return "Fair";
    return "Poor";
}

double PerformanceDashboard::computeSSIM(const cv::Mat& img1, const cv::Mat& img2) {
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