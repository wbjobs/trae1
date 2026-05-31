#pragma once

#include "common.hpp"
#include <functional>
#include <memory>
#include <atomic>

struct SoundIo;
struct SoundIoDevice;
struct SoundIoInStream;

namespace afp {

class AudioCapture {
public:
    using FrameCallback = std::function<void(const Frame&)>;

    AudioCapture(int sample_rate = SAMPLE_RATE, int frame_size = FRAME_SIZE);
    ~AudioCapture();

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    bool start(FrameCallback callback);
    void stop();
    bool isRunning() const;

    void setDeviceIndex(int index);
    int getDeviceCount() const;
    std::string getDeviceName(int index) const;

private:
    static void readCallback(SoundIoInStream* stream, int frame_count_min, int frame_count_max);
    void processFrames(int frame_count_min, int frame_count_max);

    SoundIo* soundio_ = nullptr;
    SoundIoDevice* device_ = nullptr;
    SoundIoInStream* instream_ = nullptr;
    int sample_rate_;
    int frame_size_;
    int device_index_ = -1;
    std::atomic<bool> running_{false};
    FrameCallback callback_;
    Frame current_frame_;
    int frame_offset_ = 0;
};

}
