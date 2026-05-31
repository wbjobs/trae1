#include "audio_capture.hpp"
#include <soundio/soundio.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace afp {

static AudioCapture* g_instance = nullptr;

AudioCapture::AudioCapture(int sample_rate, int frame_size)
    : sample_rate_(sample_rate), frame_size_(frame_size) {
    current_frame_.resize(frame_size, 0.0);

    soundio_ = soundio_create();
    if (!soundio_) {
        throw std::runtime_error("Failed to create soundio context");
    }

    int err = soundio_connect(soundio_);
    if (err) {
        soundio_destroy(soundio_);
        soundio_ = nullptr;
        throw std::runtime_error(std::string("Failed to connect to audio backend: ") + soundio_strerror(err));
    }

    soundio_flush_events(soundio_);
}

AudioCapture::~AudioCapture() {
    stop();
    if (instream_) {
        soundio_instream_destroy(instream_);
        instream_ = nullptr;
    }
    if (soundio_) {
        soundio_disconnect(soundio_);
        soundio_destroy(soundio_);
        soundio_ = nullptr;
    }
    g_instance = nullptr;
}

int AudioCapture::getDeviceCount() const {
    return soundio_input_device_count(soundio_);
}

std::string AudioCapture::getDeviceName(int index) const {
    SoundIoDevice* dev = soundio_get_input_device(soundio_, index);
    if (!dev) return "Unknown";
    std::string name = dev->name ? dev->name : "Unknown";
    soundio_device_unref(dev);
    return name;
}

void AudioCapture::setDeviceIndex(int index) {
    device_index_ = index;
}

bool AudioCapture::start(FrameCallback callback) {
    if (running_.load()) return false;

    int dev_idx = device_index_;
    if (dev_idx < 0) {
        dev_idx = soundio_default_input_device_index(soundio_);
    }

    device_ = soundio_get_input_device(soundio_, dev_idx);
    if (!device_) {
        std::cerr << "Failed to get input device" << std::endl;
        return false;
    }

    if (device_->probe_error) {
        std::cerr << "Device probe error: " << soundio_strerror(device_->probe_error) << std::endl;
        soundio_device_unref(device_);
        return false;
    }

    instream_ = soundio_instream_create(device_);
    if (!instream_) {
        std::cerr << "Failed to create input stream" << std::endl;
        soundio_device_unref(device_);
        return false;
    }

    instream_->format = SoundIoFormatFloat64NE;
    instream_->sample_rate = sample_rate_;
    instream_->layout = *soundio_channel_layout_get_builtin(SoundIoChannelIdMono);
    instream_->software_latency = 0.05;
    instream_->read_callback = AudioCapture::readCallback;
    instream_->userdata = this;

    int err = soundio_instream_open(instream_);
    if (err) {
        std::cerr << "Cannot open input stream: " << soundio_strerror(err) << std::endl;
        soundio_instream_destroy(instream_);
        instream_ = nullptr;
        soundio_device_unref(device_);
        return false;
    }

    err = soundio_instream_start(instream_);
    if (err) {
        std::cerr << "Cannot start input stream: " << soundio_strerror(err) << std::endl;
        soundio_instream_destroy(instream_);
        instream_ = nullptr;
        soundio_device_unref(device_);
        return false;
    }

    g_instance = this;
    callback_ = std::move(callback);
    running_.store(true);
    frame_offset_ = 0;

    return true;
}

void AudioCapture::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (instream_) {
        soundio_instream_pause(instream_, true);
    }
}

bool AudioCapture::isRunning() const {
    return running_.load();
}

void AudioCapture::readCallback(SoundIoInStream* stream, int frame_count_min, int frame_count_max) {
    auto* self = static_cast<AudioCapture*>(stream->userdata);
    if (self && self->running_.load()) {
        self->processFrames(frame_count_min, frame_count_max);
    }
}

void AudioCapture::processFrames(int frame_count_min, int frame_count_max) {
    struct SoundIoChannelArea* areas;
    int err;

    int frames_to_read = frame_count_max;

    while (frames_to_read > 0 && running_.load()) {
        int frame_count = frames_to_read;
        err = soundio_instream_beginread(instream_, &frame_count, &areas);
        if (err) {
            std::cerr << "Read error: " << soundio_strerror(err) << std::endl;
            return;
        }

        if (frame_count == 0) break;

        const double* buf = (const double*)areas[0].ptr;
        int step = areas[0].step / sizeof(double);

        for (int i = 0; i < frame_count; ++i) {
            current_frame_[frame_offset_] = buf[i * step];
            frame_offset_++;

            if (frame_offset_ >= frame_size_) {
                if (callback_) {
                    callback_(current_frame_);
                }
                frame_offset_ = 0;
            }
        }

        err = soundio_instream_endread(instream_);
        if (err) {
            std::cerr << "End read error: " << soundio_strerror(err) << std::endl;
            return;
        }

        frames_to_read -= frame_count;
    }
}

}
