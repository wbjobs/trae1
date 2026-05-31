#pragma once

#include "common.hpp"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <memory>

struct AVFormatContext;
struct AVCodecContext;
struct SwrContext;
struct AVPacket;
struct AVFrame;

namespace afp {

enum class StreamState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Error,
    Stopped
};

enum class StreamProtocol {
    HTTP,
    RTMP,
    HLS,
    Unknown
};

struct StreamInfo {
    std::string url;
    StreamProtocol protocol;
    int sample_rate = SAMPLE_RATE;
    int channels = NUM_CHANNELS;
    std::string codec_name;
    int64_t bitrate = 0;
    double duration = 0;
};

class StreamInput {
public:
    using AudioCallback = std::function<void(const std::vector<Sample>&, int64_t timestamp)>;

    StreamInput();
    ~StreamInput();

    StreamInput(const StreamInput&) = delete;
    StreamInput& operator=(const StreamInput&) = delete;

    bool open(const std::string& url);
    void close();

    bool start(AudioCallback callback);
    void stop();

    bool isRunning() const { return state_ == StreamState::Connected; }
    StreamState getState() const { return state_; }
    StreamInfo getStreamInfo() const { return stream_info_; }

    void setReconnectAttempts(int attempts) { max_reconnect_attempts_ = attempts; }
    void setReconnectDelay(int delay_ms) { reconnect_delay_ms_ = delay_ms; }
    void setReadTimeout(int timeout_ms) { read_timeout_ms_ = timeout_ms; }

    int64_t getCurrentTimestamp() const { return current_timestamp_; }
    int getReconnectCount() const { return reconnect_count_; }

    static StreamProtocol detectProtocol(const std::string& url);
    static std::string protocolToString(StreamProtocol proto);

private:
    void readLoop();
    bool connect();
    void disconnect();
    bool decodePacket(AVPacket* pkt);
    void processFrame(AVFrame* frame);

    std::string url_;
    StreamInfo stream_info_;
    std::atomic<StreamState> state_{StreamState::Disconnected};
    AudioCallback callback_;

    AVFormatContext* fmt_ctx_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    SwrContext* swr_ctx_ = nullptr;

    int audio_stream_index_ = -1;
    std::atomic<int64_t> current_timestamp_{0};
    std::atomic<int> reconnect_count_{0};
    std::atomic<bool> should_stop_{false};

    int max_reconnect_attempts_ = STREAM_MAX_RECONNECT_ATTEMPTS;
    int reconnect_delay_ms_ = STREAM_RECONNECT_DELAY_MS;
    int read_timeout_ms_ = STREAM_READ_TIMEOUT_MS;

    std::unique_ptr<std::thread> read_thread_;
    std::mutex mutex_;
};

}
