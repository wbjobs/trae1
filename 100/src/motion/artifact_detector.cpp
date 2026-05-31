#include "motion/artifact_detector.h"

ArtifactDetector::ArtifactDetector() {
}

ArtifactDetector::~ArtifactDetector() = default;

ArtifactDetectionResult ArtifactDetector::detect(
    const cv::Mat& original_frame,
    const cv::Mat& processed_frame,
    double motion_score) {

    ArtifactDetectionResult result;
    result.type = ArtifactType::NONE;
    result.severity = 0.0;
    result.ghosting_score = 0.0;
    result.blurring_score = 0.0;
    result.ringing_score = 0.0;
    result.needs_fallback = false;

    if (original_frame.empty() || processed_frame.empty()) {
        result.description = "Empty frames";
        return result;
    }

    cv::Mat orig_gray, proc_gray;
    if (original_frame.channels() == 3) {
        cv::cvtColor(original_frame, orig_gray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(processed_frame, proc_gray, cv::COLOR_BGR2GRAY);
    } else {
        orig_gray = original_frame.clone();
        proc_gray = processed_frame.clone();
    }

    orig_gray.convertTo(orig_gray, CV_32F, 1.0 / 255.0);
    proc_gray.convertTo(proc_gray, CV_32F, 1.0 / 255.0);

    result.ghosting_score = computeGhostingScore(orig_gray, proc_gray);
    result.blurring_score = computeBlurringScore(orig_gray, proc_gray);
    result.ringing_score = computeRingingScore(orig_gray, proc_gray);

    result.type = classifyArtifact(
        result.ghosting_score,
        result.blurring_score,
        result.ringing_score,
        motion_score);

    double weight_sum = result.ghosting_score * 0.4 +
                        result.blurring_score * 0.3 +
                        result.ringing_score * 0.15 +
                        std::min(motion_score / 30.0, 1.0) * 0.15;

    result.severity = std::min(1.0, weight_sum);
    result.needs_fallback = shouldFallback(result);

    result.description = artifactTypeToString(result.type);
    if (result.needs_fallback) {
        result.description += " (fallback triggered)";
    }

    last_severity_ = result.severity;
    last_type_ = result.type;

    return result;
}

double ArtifactDetector::computeGhostingScore(
    const cv::Mat& original,
    const cv::Mat& processed) {

    cv::Mat diff;
    cv::absdiff(original, processed, diff);

    cv::Mat edge_orig, edge_proc;
    cv::Laplacian(original, edge_orig, CV_32F, 3);
    cv::Laplacian(processed, edge_proc, CV_32F, 3);

    cv::Mat edge_diff;
    cv::absdiff(edge_orig, edge_proc, edge_diff);

    double mean_diff = cv::mean(diff)[0];
    double mean_edge_diff = cv::mean(edge_diff)[0];

    double score = std::min(1.0, mean_diff * 3.0 + mean_edge_diff * 2.0);
    return score;
}

double ArtifactDetector::computeBlurringScore(
    const cv::Mat& original,
    const cv::Mat& processed) {

    double var_orig = computeLaplacianVariance(original);
    double var_proc = computeLaplacianVariance(processed);

    if (var_orig < 1e-6) return 0.0;

    double ratio = var_proc / var_orig;
    double score = std::min(1.0, std::max(0.0, 1.0 - ratio));
    return score;
}

double ArtifactDetector::computeRingingScore(
    const cv::Mat& original,
    const cv::Mat& processed) {

    cv::Mat diff;
    cv::absdiff(original, processed, diff);

    cv::Mat high_freq_mask;
    cv::Laplacian(original, high_freq_mask, CV_32F, 3);
    high_freq_mask = cv::abs(high_freq_mask) > 0.1;

    double ringing_energy = 0.0;
    double total_energy = 0.0;

    for (int y = 1; y < diff.rows - 1; ++y) {
        for (int x = 1; x < diff.cols - 1; ++x) {
            float val = diff.at<float>(y, x);
            float neighbors = (diff.at<float>(y-1, x) + diff.at<float>(y+1, x) +
                              diff.at<float>(y, x-1) + diff.at<float>(y, x+1)) / 4.0f;
            if (val > neighbors * 1.5 && high_freq_mask.at<uchar>(y, x)) {
                ringing_energy += static_cast<double>(val);
            }
            total_energy += static_cast<double>(val);
        }
    }

    if (total_energy < 1e-6) return 0.0;
    double score = std::min(1.0, ringing_energy / total_energy * 3.0);
    return score;
}

bool ArtifactDetector::shouldFallback(const ArtifactDetectionResult& result) const {
    if (result.severity > severity_threshold_) return true;
    if (result.ghosting_score > ghosting_threshold_) return true;
    if (result.blurring_score > blurring_threshold_) return true;
    return false;
}

ArtifactType ArtifactDetector::classifyArtifact(
    double ghosting, double blurring,
    double ringing, double motion) const {

    if (ghosting > 0.6 && motion > 15.0) return ArtifactType::GHOSTING;
    if (blurring > 0.5 && ghosting < 0.3) return ArtifactType::BLURRING;
    if (ringing > 0.4 && ghosting < 0.3) return ArtifactType::RINGING;
    if (motion > 25.0) return ArtifactType::SEVERE_MOTION;
    if (ghosting > 0.4 || blurring > 0.3 || ringing > 0.25) return ArtifactType::GHOSTING;
    return ArtifactType::NONE;
}

std::string ArtifactDetector::artifactTypeToString(ArtifactType type) {
    switch (type) {
        case ArtifactType::NONE: return "None";
        case ArtifactType::GHOSTING: return "Ghosting";
        case ArtifactType::BLURRING: return "Blurring";
        case ArtifactType::RINGING: return "Ringing";
        case ArtifactType::SEVERE_MOTION: return "Severe Motion";
        default: return "Unknown";
    }
}

double ArtifactDetector::computeLaplacianVariance(const cv::Mat& image) const {
    cv::Mat laplacian;
    cv::Laplacian(image, laplacian, CV_32F, 3);
    cv::Scalar mean, stddev;
    cv::meanStdDev(laplacian, mean, stddev);
    return stddev.val[0] * stddev.val[0];
}

double ArtifactDetector::computeGradientEnergy(const cv::Mat& image) const {
    cv::Mat grad_x, grad_y;
    cv::Sobel(image, grad_x, CV_32F, 1, 0, 3);
    cv::Sobel(image, grad_y, CV_32F, 0, 1, 3);
    cv::Mat magnitude;
    cv::magnitude(grad_x, grad_y, magnitude);
    return static_cast<double>(cv::sum(magnitude)[0]) / (magnitude.rows * magnitude.cols);
}

double ArtifactDetector::computeSSIM(const cv::Mat& img1, const cv::Mat& img2) const {
    const double C1 = 0.01 * 0.01;
    const double C2 = 0.03 * 0.03;

    cv::Mat img1_sq, img2_sq, img1_img2;
    cv::pow(img1, 2, img1_sq);
    cv::pow(img2, 2, img2_sq);
    img1_img2 = img1.mul(img2);

    cv::Mat mu1, mu2, mu1_sq, mu2_sq, mu1_mu2;
    cv::GaussianBlur(img1, mu1, cv::Size(11, 11), 1.5);
    cv::GaussianBlur(img2, mu2, cv::Size(11, 11), 1.5);
    cv::pow(mu1, 2, mu1_sq);
    cv::pow(mu2, 2, mu2_sq);
    mu1_mu2 = mu1.mul(mu2);

    cv::Mat sigma1_sq, sigma2_sq, sigma12;
    cv::GaussianBlur(img1_sq, sigma1_sq, cv::Size(11, 11), 1.5);
    sigma1_sq -= mu1_sq;
    cv::GaussianBlur(img2_sq, sigma2_sq, cv::Size(11, 11), 1.5);
    sigma2_sq -= mu2_sq;
    cv::GaussianBlur(img1_img2, sigma12, cv::Size(11, 11), 1.5);
    sigma12 -= mu1_mu2;

    cv::Mat ssim_map;
    ssim_map = ((2 * mu1_mu2 + C1).mul(2 * sigma12 + C2)) /
               ((mu1_sq + mu2_sq + C1).mul(sigma1_sq + sigma2_sq + C2));

    return static_cast<double>(cv::mean(ssim_map)[0]);
}