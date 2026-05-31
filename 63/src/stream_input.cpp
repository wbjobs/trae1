#include "stream_input.hpp"
#include <iostream>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/error.h>
}

namespace afp {

StreamInput::StreamInput() {
    avformat_network_init();
}

StreamInput::~StreamInput() {
    stop();
    close();
    avformat_network_deinit();
}

StreamProtocol StreamInput::detectProtocol(const std::string& url) {
    if (url.find("rtmp://") == 0 || url.find("rtmps://") == 0) {
        return StreamProtocol::RTMP;
    } else if (url.find("http://") == 0 || url.find("https://") == 0) {
        if (url.find(".m3u8") != std::string::npos || url.find(".m3u") != std::string::npos) {
            return StreamProtocol::HLS;
        }
        return StreamProtocol::HTTP;
    } else if (url.find(".m3u8") != std::string::npos) {
        return StreamProtocol::HLS;
    }
    return StreamProtocol::Unknown;
}

std::string StreamInput::protocolToString(StreamProtocol proto) {
    switch (proto) {
        case StreamProtocol::HTTP: return "HTTP";
        case StreamProtocol::RTMP: return "RTMP";
        case StreamProtocol::HLS: return "HLS";
        default: return "Unknown";
    }
}

bool StreamInput::open(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    url_ = url;
    stream_info_.url = url;
    stream_info_.protocol = detectProtocol(url);
    return connect();
}

bool StreamInput::connect() {
    state_ = StreamState::Connecting;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rw_timeout", std::to_string(read_timeout_ms_ * 1000).c_str(), 0);
    av_dict_set(&opts, "stimeout", std::to_string(read_timeout_ms_ * 1000).c_str(), 0);

    if (stream_info_.protocol == StreamProtocol::RTMP) {
        av_dict_set(&opts, "rtmp_transport", "tcp", 0);
        av_dict_set(&opts, "rtmp_playpath", "", 0);
    } else if (stream_info_.protocol == StreamProtocol::HLS) {
        av_dict_set(&opts, "multiple_requests", "1", 0);
        av_dict_set(&opts, "reconnect", "1", 0);
        av_dict_set(&opts, "reconnect_streamed", "1", 0);
        av_dict_set(&opts, "reconnect_on_network_error", "1", 0);
    }

    int ret = avformat_open_input(&fmt_ctx_, url_.c_str(), nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[StreamInput] Cannot open stream: " << errbuf << std::endl;
        state_ = StreamState::Error;
        return false;
    }

    ret = avformat_find_stream_info(fmt_ctx_, nullptr);
    if (ret < 0) {
        std::cerr << "[StreamInput] Cannot find stream info" << std::endl;
        avformat_close_input(&fmt_ctx_);
        state_ = StreamState::Error;
        return false;
    }

    audio_stream_index_ = -1;
    for (unsigned int i = 0; i < fmt_ctx_->nb_streams; i++) {
        if (fmt_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index_ = i;
            break;
        }
    }

    if (audio_stream_index_ < 0) {
        std::cerr << "[StreamInput] No audio stream found" << std::endl;
        avformat_close_input(&fmt_ctx_);
        state_ = StreamState::Error;
        return false;
    }

    AVCodecParameters* codecpar = fmt_ctx_->streams[audio_stream_index_]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        std::cerr << "[StreamInput] Unsupported codec" << std::endl;
        avformat_close_input(&fmt_ctx_);
        state_ = StreamState::Error;
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        std::cerr << "[StreamInput] Cannot allocate codec context" << std::endl;
        avformat_close_input(&fmt_ctx_);
        state_ = StreamState::Error;
        return false;
    }

    ret = avcodec_parameters_to_context(codec_ctx_, codecpar);
    if (ret < 0) {
        std::cerr << "[StreamInput] Cannot copy codec parameters" << std::endl;
        avcodec_free_context(&codec_ctx_);
        avformat_close_input(&fmt_ctx_);
        state_ = StreamState::Error;
        return false;
    }

    ret = avcodec_open2(codec_ctx_, codec, nullptr);
    if (ret < 0) {
        std::cerr << "[StreamInput] Cannot open codec" << std::endl;
        avcodec_free_context(&codec_ctx_);
        avformat_close_input(&fmt_ctx_);
        state_ = StreamState::Error;
        return false;
    }

    swr_ctx_ = swr_alloc();
    if (!swr_ctx_) {
        std::cerr << "[StreamInput] Cannot allocate resampler" << std::endl;
        avcodec_free_context(&codec_ctx_);
        avformat_close_input(&fmt_ctx_);
        state_ = StreamState::Error;
        return false;
    }

    av_opt_set_int(swr_ctx_, "in_channel_count", codec_ctx_->ch_layout.nb_channels, 0);
    av_opt_set_int(swr_ctx_, "in_sample_rate", codec_ctx_->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx_, "in_sample_fmt", codec_ctx_->sample_fmt, 0);

    av_opt_set_int(swr_ctx_, "out_channel_count", NUM_CHANNELS, 0);
    av_opt_set_int(swr_ctx_, "out_sample_rate", SAMPLE_RATE, 0);
    av_opt_set_sample_fmt(swr_ctx_, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

    ret = swr_init(swr_ctx_);
    if (ret < 0) {
        std::cerr << "[StreamInput] Cannot initialize resampler" << std::endl;
        swr_free(&swr_ctx_);
        avcodec_free_context(&codec_ctx_);
        avformat_close_input(&fmt_ctx_);
        state_ = StreamState::Error;
        return false;
    }

    stream_info_.sample_rate = codec_ctx_->sample_rate;
    stream_info_.channels = codec_ctx_->ch_layout.nb_channels;
    stream_info_.codec_name = codec->name ? codec->name : "unknown";
    stream_info_.bitrate = codec_ctx_->bit_rate;
    stream_info_.duration = fmt_ctx_->duration / (double)AV_TIME_BASE;

    state_ = StreamState::Connected;
    return true;
}

void StreamInput::disconnect() {
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
    }
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
    if (fmt_ctx_) {
        avformat_close_input(&fmt_ctx_);
    }
}

void StreamInput::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    disconnect();
    state_ = StreamState::Disconnected;
}

bool StreamInput::start(AudioCallback callback) {
    if (state_ != StreamState::Connected) {
        std::cerr << "[StreamInput] Stream not connected" << std::endl;
        return false;
    }

    callback_ = std::move(callback);
    should_stop_ = false;
    state_ = StreamState::Connected;

    read_thread_ = std::make_unique<std::thread>(&StreamInput::readLoop, this);
    return true;
}

void StreamInput::stop() {
    should_stop_ = true;
    if (read_thread_ && read_thread_->joinable()) {
        read_thread_->join();
    }
    read_thread_.reset();
}

void StreamInput::readLoop() {
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    if (!pkt || !frame) {
        std::cerr << "[StreamInput] Cannot allocate packet/frame" << std::endl;
        state_ = StreamState::Error;
        av_packet_free(&pkt);
        av_frame_free(&frame);
        return;
    }

    int reconnect_attempts = 0;

    while (!should_stop_.load()) {
        int ret = av_read_frame(fmt_ctx_, pkt);

        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                std::cout << "[StreamInput] Stream ended" << std::endl;
                break;
            }

            if (reconnect_attempts < max_reconnect_attempts_) {
                state_ = StreamState::Reconnecting;
                reconnect_attempts++;
                reconnect_count_++;

                std::cout << "[StreamInput] Reconnection attempt " << reconnect_attempts
                         << "/" << max_reconnect_attempts_ << std::endl;

                disconnect();
                std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_delay_ms_));

                std::lock_guard<std::mutex> lock(mutex_);
                if (connect()) {
                    reconnect_attempts = 0;
                    continue;
                }
            } else {
                std::cerr << "[StreamInput] Max reconnection attempts reached" << std::endl;
                state_ = StreamState::Error;
                break;
            }

            continue;
        }

        if (pkt->stream_index == audio_stream_index_) {
            ret = avcodec_send_packet(codec_ctx_, pkt);
            if (ret < 0) {
                std::cerr << "[StreamInput] Error sending packet" << std::endl;
                av_packet_unref(pkt);
                continue;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx_, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    std::cerr << "[StreamInput] Error receiving frame" << std::endl;
                    break;
                }

                processFrame(frame);
                av_frame_unref(frame);
            }
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    state_ = StreamState::Stopped;
}

void StreamInput::processFrame(AVFrame* frame) {
    if (!swr_ctx_ || !callback_) return;

    int64_t delay = swr_get_delay(swr_ctx_, frame->sample_rate);
    int out_samples = (int)av_rescale_rnd(
        delay + frame->nb_samples, SAMPLE_RATE, frame->sample_rate, AV_ROUND_UP);

    if (out_samples <= 0) return;

    std::vector<float> out_buffer(out_samples * NUM_CHANNELS);
    float* out_ptr = out_buffer.data();

    int converted = swr_convert(swr_ctx_, (uint8_t**)&out_ptr, out_samples,
                               (const uint8_t**)frame->data, frame->nb_samples);

    if (converted <= 0) return;

    std::vector<Sample> samples(converted * NUM_CHANNELS);
    for (int i = 0; i < converted * NUM_CHANNELS; ++i) {
        samples[i] = static_cast<Sample>(out_buffer[i]);
    }

    int64_t pts = frame->pts;
    if (pts == AV_NOPTS_VALUE) {
        pts = current_timestamp_.load() + converted;
    }

    current_timestamp_ = pts;

    callback_(samples, pts);
}

}
