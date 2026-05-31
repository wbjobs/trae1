#include "motion/motion_compensation.h"

MotionCompensation::MotionCompensation() {
}

MotionCompensation::~MotionCompensation() = default;

MotionCompensationResult MotionCompensation::compensate(
    const cv::Mat& prev_frame,
    const cv::Mat& curr_frame,
    const cv::Mat& next_frame) {

    MotionCompensationResult result;

    auto flow_prev_curr = estimateOpticalFlow(prev_frame, curr_frame);
    auto flow_curr_next = estimateOpticalFlow(curr_frame, next_frame);

    cv::Mat prev_aligned = warpFrame(prev_frame, flow_prev_curr.flow_x, flow_prev_curr.flow_y);
    cv::Mat next_aligned = warpFrame(next_frame, -flow_curr_next.flow_x, -flow_curr_next.flow_y);

    result.aligned_frame = curr_frame.clone();
    result.flow = flow_prev_curr;
    result.has_significant_motion = isFastMotion(flow_prev_curr.motion_score);
    result.macro_blocks = computeMacroBlockVectors(
        flow_prev_curr.flow_x, flow_prev_curr.flow_y);

    last_motion_score_ = flow_prev_curr.motion_score;

    return result;
}

OpticalFlowResult MotionCompensation::estimateOpticalFlow(
    const cv::Mat& frame1,
    const cv::Mat& frame2) {

    OpticalFlowResult result;

    cv::Mat gray1, gray2;
    if (frame1.channels() == 3) {
        cv::cvtColor(frame1, gray1, cv::COLOR_BGR2GRAY);
        cv::cvtColor(frame2, gray2, cv::COLOR_BGR2GRAY);
    } else {
        gray1 = frame1.clone();
        gray2 = frame2.clone();
    }

    cv::Mat flow;
    cv::calcOpticalFlowFarneback(
        gray1, gray2, flow,
        0.5, 3, 15, 3, 5, 1.2, 0);

    std::vector<cv::Mat> flow_channels;
    cv::split(flow, flow_channels);
    result.flow_x = flow_channels[0].clone();
    result.flow_y = flow_channels[1].clone();

    cv::magnitude(result.flow_x, result.flow_y, result.magnitude);

    cv::Scalar avg_mag = cv::mean(result.magnitude);
    cv::Scalar max_mag;
    cv::minMaxLoc(result.magnitude, nullptr, &max_mag.val[0]);

    result.avg_magnitude = avg_mag.val[0];
    result.max_magnitude = max_mag.val[0];
    result.motion_score = estimateMotionScore(result);

    return result;
}

cv::Mat MotionCompensation::warpFrame(
    const cv::Mat& frame,
    const cv::Mat& flow_x,
    const cv::Mat& flow_y) {

    cv::Mat result = cv::Mat::zeros(frame.size(), frame.type());

    std::vector<cv::Mat> channels;
    if (frame.channels() == 3) {
        cv::split(frame, channels);
    } else {
        channels.push_back(frame);
    }

    cv::Mat map_x(frame.size(), CV_32FC1);
    cv::Mat map_y(frame.size(), CV_32FC1);

    for (int y = 0; y < frame.rows; ++y) {
        for (int x = 0; x < frame.cols; ++x) {
            map_x.at<float>(y, x) = static_cast<float>(x) + flow_x.at<float>(y, x);
            map_y.at<float>(y, x) = static_cast<float>(y) + flow_y.at<float>(y, x);
        }
    }

    std::vector<cv::Mat> warped_channels;
    for (auto& ch : channels) {
        cv::Mat warped;
        cv::remap(ch, warped, map_x, map_y, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        warped_channels.push_back(warped);
    }

    if (warped_channels.size() == 3) {
        cv::merge(warped_channels, result);
    } else {
        result = warped_channels[0];
    }

    return result;
}

std::vector<cv::Mat> MotionCompensation::buildTemporalVolume(
    const cv::Mat& prev_aligned,
    const cv::Mat& curr_frame,
    const cv::Mat& next_aligned) {

    return {prev_aligned.clone(), curr_frame.clone(), next_aligned.clone()};
}

double MotionCompensation::estimateMotionScore(const OpticalFlowResult& flow) {
    if (flow.magnitude.empty()) return 0.0;

    cv::Scalar mean, stddev;
    cv::meanStdDev(flow.magnitude, mean, stddev);

    double score = mean.val[0] * 0.6 + stddev.val[0] * 0.4;
    return score;
}

bool MotionCompensation::isFastMotion(double motion_score, double threshold) {
    return motion_score > threshold;
}

void MotionCompensation::setOpticalFlowMethod(int method) {
    flow_method_ = method;
}

void MotionCompensation::setMotionThreshold(double threshold) {
    motion_threshold_ = threshold;
}

cv::Mat MotionCompensation::computeDenseOpticalFlow(
    const cv::Mat& prev,
    const cv::Mat& curr) {

    cv::Mat flow;
    cv::calcOpticalFlowFarneback(
        prev, curr, flow,
        0.5, 3, 15, 3, 5, 1.2, 0);
    return flow;
}

std::vector<MotionVector> MotionCompensation::computeMacroBlockVectors(
    const cv::Mat& flow_x,
    const cv::Mat& flow_y,
    int block_size) {

    std::vector<MotionVector> vectors;
    int blocks_h = flow_x.rows / block_size;
    int blocks_w = flow_x.cols / block_size;

    for (int by = 0; by < blocks_h; ++by) {
        for (int bx = 0; bx < blocks_w; ++bx) {
            cv::Rect roi(bx * block_size, by * block_size, block_size, block_size);

            MotionVector mv;
            cv::Scalar mean_dx = cv::mean(flow_x(roi));
            cv::Scalar mean_dy = cv::mean(flow_y(roi));

            mv.dx = static_cast<float>(mean_dx.val[0]);
            mv.dy = static_cast<float>(mean_dy.val[0]);
            mv.magnitude = std::sqrt(mv.dx * mv.dx + mv.dy * mv.dy);
            mv.confidence = 1.0f / (1.0f + mv.magnitude * 0.1f);

            vectors.push_back(mv);
        }
    }

    return vectors;
}