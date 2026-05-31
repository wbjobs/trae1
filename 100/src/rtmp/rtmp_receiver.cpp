#include "rtmp/rtmp_receiver.h"

RTMPReceiver::RTMPReceiver(int stream_id, const std::string& url)
    : stream_id_(stream_id), url_(url) {
}

RTMPReceiver::~RTMPReceiver() {
    disconnect();
}

bool RTMPReceiver::connect() {
    if (connected_) return true;

    cap_.release();

    cap_.open(url_, cv::CAP_FFMPEG);
    if (!cap_.isOpened()) {
        std::cerr << "[RTMP " << stream_id_ << "] Failed to open: " << url_ << "\n";
        return false;
    }

    cap_.set(cv::CAP_PROP_FRAME_WIDTH, INPUT_WIDTH);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, INPUT_HEIGHT);
    cap_.set(cv::CAP_PROP_FPS, TARGET_FPS);
    cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);

    connected_ = true;
    std::cout << "[RTMP " << stream_id_ << "] Connected: " << url_ << "\n";
    return true;
}

void RTMPReceiver::disconnect() {
    if (connected_) {
        cap_.release();
        connected_ = false;
        std::cout << "[RTMP " << stream_id_ << "] Disconnected\n";
    }
}

bool RTMPReceiver::readFrame(cv::Mat& frame) {
    if (!connected_) return false;

    if (!cap_.read(frame)) {
        std::cerr << "[RTMP " << stream_id_ << "] Failed to read frame\n";
        return false;
    }

    if (frame.cols != target_width_ || frame.rows != target_height_) {
        cv::resize(frame, frame, cv::Size(target_width_, target_height_));
    }

    return true;
}

std::vector<uint8_t> RTMPReceiver::readFrameRaw() {
    cv::Mat frame;
    if (!readFrame(frame)) {
        return {};
    }

    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);

    size_t size = static_cast<size_t>(rgb.total() * rgb.elemSize());
    std::vector<uint8_t> data(size);
    std::memcpy(data.data(), rgb.data, size);
    return data;
}

void RTMPReceiver::setTargetSize(int width, int height) {
    target_width_ = width;
    target_height_ = height;
}