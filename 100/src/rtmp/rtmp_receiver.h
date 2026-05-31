#pragma once

#include "utils/common.h"
#include <opencv2/opencv.hpp>

class RTMPReceiver {
public:
    RTMPReceiver(int stream_id, const std::string& url);
    ~RTMPReceiver();

    bool connect();
    void disconnect();
    bool isConnected() const { return connected_; }

    bool readFrame(cv::Mat& frame);
    std::vector<uint8_t> readFrameRaw();

    int getStreamId() const { return stream_id_; }
    const std::string& getUrl() const { return url_; }

    void setTargetSize(int width, int height);

private:
    int stream_id_;
    std::string url_;
    cv::VideoCapture cap_;
    bool connected_ = false;
    int target_width_ = INPUT_WIDTH;
    int target_height_ = INPUT_HEIGHT;
};