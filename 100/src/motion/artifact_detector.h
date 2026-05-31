#pragma once

#include "utils/common.h"
#include <opencv2/opencv.hpp>

enum class ArtifactType {
    NONE,
    GHOSTING,
    BLURRING,
    RINGING,
    SEVERE_MOTION
};

struct ArtifactDetectionResult {
    ArtifactType type;
    double severity;
    double ghosting_score;
    double blurring_score;
    double ringing_score;
    bool needs_fallback;
    std::string description;
};

class ArtifactDetector {
public:
    ArtifactDetector();
    ~ArtifactDetector();

    ArtifactDetectionResult detect(
        const cv::Mat& original_frame,
        const cv::Mat& processed_frame,
        double motion_score = 0.0);

    double computeGhostingScore(
        const cv::Mat& original,
        const cv::Mat& processed);

    double computeBlurringScore(
        const cv::Mat& original,
        const cv::Mat& processed);

    double computeRingingScore(
        const cv::Mat& original,
        const cv::Mat& processed);

    void setSeverityThreshold(double threshold) { severity_threshold_ = threshold; }
    void setGhostingThreshold(double threshold) { ghosting_threshold_ = threshold; }
    void setBlurringThreshold(double threshold) { blurring_threshold_ = threshold; }

    bool shouldFallback(const ArtifactDetectionResult& result) const;

    ArtifactType classifyArtifact(double ghosting, double blurring,
                                   double ringing, double motion) const;

    static std::string artifactTypeToString(ArtifactType type);

private:
    double computeLaplacianVariance(const cv::Mat& image) const;
    double computeGradientEnergy(const cv::Mat& image) const;
    double computeSSIM(const cv::Mat& img1, const cv::Mat& img2) const;

    double severity_threshold_ = 0.7;
    double ghosting_threshold_ = 0.5;
    double blurring_threshold_ = 0.4;

    double last_severity_ = 0.0;
    ArtifactType last_type_ = ArtifactType::NONE;
};