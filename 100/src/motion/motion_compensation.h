#pragma once

#include "utils/common.h"
#include <opencv2/opencv.hpp>

struct MotionVector {
    float dx;
    float dy;
    float magnitude;
    float confidence;
};

struct OpticalFlowResult {
    cv::Mat flow_x;
    cv::Mat flow_y;
    cv::Mat magnitude;
    double avg_magnitude;
    double max_magnitude;
    double motion_score;
};

struct MotionCompensationResult {
    cv::Mat aligned_frame;
    OpticalFlowResult flow;
    bool has_significant_motion;
    std::vector<MotionVector> macro_blocks;
};

class MotionCompensation {
public:
    MotionCompensation();
    ~MotionCompensation();

    MotionCompensationResult compensate(
        const cv::Mat& prev_frame,
        const cv::Mat& curr_frame,
        const cv::Mat& next_frame);

    OpticalFlowResult estimateOpticalFlow(
        const cv::Mat& frame1,
        const cv::Mat& frame2);

    cv::Mat warpFrame(
        const cv::Mat& frame,
        const cv::Mat& flow_x,
        const cv::Mat& flow_y);

    std::vector<cv::Mat> buildTemporalVolume(
        const cv::Mat& prev_aligned,
        const cv::Mat& curr_frame,
        const cv::Mat& next_aligned);

    static double estimateMotionScore(const OpticalFlowResult& flow);
    static bool isFastMotion(double motion_score, double threshold = 15.0);

    void setOpticalFlowMethod(int method);
    void setMotionThreshold(double threshold);

    double getLastMotionScore() const { return last_motion_score_; }

private:
    cv::Mat computeDenseOpticalFlow(
        const cv::Mat& prev,
        const cv::Mat& curr);

    std::vector<MotionVector> computeMacroBlockVectors(
        const cv::Mat& flow_x,
        const cv::Mat& flow_y,
        int block_size = 16);

    int flow_method_ = cv::OPTFLOW_FARNEBACK_GAUSSIAN;
    double motion_threshold_ = 15.0;
    double last_motion_score_ = 0.0;
};