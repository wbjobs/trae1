#include "common.hpp"
#include "audio_capture.hpp"
#include "preprocessor.hpp"
#include "fingerprint.hpp"
#include "database.hpp"
#include "matcher.hpp"

#ifdef ENABLE_STREAMING
#include "stream_input.hpp"
#include "segment_manager.hpp"
#include "srt_writer.hpp"
#endif

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>
#include <iomanip>
#include <functional>

#ifndef SIGTERM
#define SIGTERM 15
#endif

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_matched{false};
static std::atomic<bool> g_noise_estimated{false};

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_running.store(false);
    }
}

struct ColorFuncs {
    std::function<std::string()> reset   = []() -> std::string { return "\033[0m"; };
    std::function<std::string()> green   = []() -> std::string { return "\033[32m"; };
    std::function<std::string()> yellow  = []() -> std::string { return "\033[33m"; };
    std::function<std::string()> cyan    = []() -> std::string { return "\033[36m"; };
    std::function<std::string()> red     = []() -> std::string { return "\033[31m"; };
    std::function<std::string()> bold    = []() -> std::string { return "\033[1m"; };

    void disable() {
        auto empty = []() -> std::string { return ""; };
        reset = green = yellow = cyan = red = bold = empty;
    }
};

void printBanner() {
    std::cout << R"(
    ╔══════════════════════════════════════════════════════════╗
    ║     Audio Fingerprint Recognition CLI (v3.0)              ║
    ║     Shazam-style Audio Matching + Streaming + SRT        ║
    ╚══════════════════════════════════════════════════════════╝
)";
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n\n";
    std::cout << "Modes:\n";
    std::cout << "  (default)              Microphone real-time recognition\n";
    std::cout << "  --stream <url>         Stream audio recognition (HTTP/RTMP/HLS)\n\n";
    std::cout << "Common Options:\n";
    std::cout << "  --db <path>            Path to fingerprint database (default: fingerprints.db)\n";
    std::cout << "  --threshold <n>        Match threshold (default: 5)\n";
    std::cout << "  --hamming <n>          Hamming distance threshold (default: 5)\n";
    std::cout << "  --no-noise-reduc       Disable noise reduction\n";
    std::cout << "  --no-fuzzy             Disable fuzzy matching\n";
    std::cout << "  --no-color             Disable colored output\n";
    std::cout << "  --verbose              Enable verbose output\n";
    std::cout << "  --help, -h             Show this help\n\n";
    std::cout << "Microphone Options:\n";
    std::cout << "  --device <index>       Audio input device index (default: auto-detect)\n";
    std::cout << "  --list-devices         List available audio input devices\n";
    std::cout << "  --duration <ms>        Recognition duration (default: 0 = continuous)\n\n";
#ifdef ENABLE_STREAMING
    std::cout << "Streaming Options:\n";
    std::cout << "  --stream <url>         Stream URL (http://, rtmp://, .m3u8)\n";
    std::cout << "  --segment-duration <s> Segment duration in seconds (default: 10)\n";
    std::cout << "  --overlap-duration <s> Segment overlap in seconds (default: 1)\n";
    std::cout << "  --srt <path>           Output SRT subtitle file\n";
    std::cout << "  --continuous           Continuous recognition mode\n";
    std::cout << "  --max-reconnect <n>    Max reconnection attempts (default: 5)\n";
    std::cout << "  --reconnect-delay <ms> Reconnection delay (default: 2000)\n\n";
#endif
    std::cout << "Examples:\n";
    std::cout << "  " << program << " --db fingerprints.db\n";
    std::cout << "  " << program << " --stream rtmp://example.com/live --srt output.srt\n";
    std::cout << "  " << program << " --stream http://example.com/stream.m3u8 --continuous --verbose\n";
    std::cout << "\nControls:\n";
    std::cout << "  Press Ctrl+C to stop recognition\n";
}

struct Options {
    std::string db_path = "fingerprints.db";
    int device_index = -1;
    bool list_devices = false;
    int match_threshold = MATCH_THRESHOLD;
    int hamming_threshold = HAMMING_DISTANCE_THRESHOLD;
    int duration_ms = 0;
    bool no_noise_reduction = false;
    bool no_fuzzy = false;
    bool no_color = false;
    bool verbose = false;

#ifdef ENABLE_STREAMING
    std::string stream_url;
    double segment_duration = STREAM_SEGMENT_DURATION;
    double overlap_duration = STREAM_OVERLAP_DURATION;
    std::string srt_path;
    bool continuous = false;
    int max_reconnect = STREAM_MAX_RECONNECT_ATTEMPTS;
    int reconnect_delay = STREAM_RECONNECT_DELAY_MS;
#endif
};

Options parseArgs(int argc, char* argv[]) {
    Options opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) {
            opts.db_path = argv[++i];
        } else if (arg == "--device" && i + 1 < argc) {
            opts.device_index = std::atoi(argv[++i]);
        } else if (arg == "--list-devices") {
            opts.list_devices = true;
        } else if (arg == "--threshold" && i + 1 < argc) {
            opts.match_threshold = std::atoi(argv[++i]);
        } else if (arg == "--hamming" && i + 1 < argc) {
            opts.hamming_threshold = std::atoi(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            opts.duration_ms = std::atoi(argv[++i]);
        } else if (arg == "--no-noise-reduc") {
            opts.no_noise_reduction = true;
        } else if (arg == "--no-fuzzy") {
            opts.no_fuzzy = true;
        } else if (arg == "--no-color") {
            opts.no_color = true;
        } else if (arg == "--verbose") {
            opts.verbose = true;
#ifdef ENABLE_STREAMING
        } else if (arg == "--stream" && i + 1 < argc) {
            opts.stream_url = argv[++i];
        } else if (arg == "--segment-duration" && i + 1 < argc) {
            opts.segment_duration = std::atof(argv[++i]);
        } else if (arg == "--overlap-duration" && i + 1 < argc) {
            opts.overlap_duration = std::atof(argv[++i]);
        } else if (arg == "--srt" && i + 1 < argc) {
            opts.srt_path = argv[++i];
        } else if (arg == "--continuous") {
            opts.continuous = true;
        } else if (arg == "--max-reconnect" && i + 1 < argc) {
            opts.max_reconnect = std::atoi(argv[++i]);
        } else if (arg == "--reconnect-delay" && i + 1 < argc) {
            opts.reconnect_delay = std::atoi(argv[++i]);
#endif
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        }
    }

    return opts;
}

void listDevices(AudioCapture& capture) {
    int count = capture.getDeviceCount();
    std::cout << "\nAvailable Audio Input Devices:\n";
    std::cout << "────────────────────────────────\n";
    for (int i = 0; i < count; ++i) {
        std::cout << "  [" << i << "] " << capture.getDeviceName(i) << "\n";
    }
    std::cout << "\n";
}

void printResult(const MatchResult& result, const ColorFuncs& c,
                 int exact_count, int fuzzy_count) {
    std::cout << "\n" << c.green() << c.bold()
              << "╔═══════════════════════════════════════╗" << c.reset() << "\n";
    std::cout << c.green() << c.bold()
              << "║        🎵  MATCH FOUND! 🎵            ║" << c.reset() << "\n";
    std::cout << c.green() << c.bold()
              << "╚═══════════════════════════════════════╝" << c.reset() << "\n";
    std::cout << c.cyan() << "  🎼 Song:     " << c.reset()
              << c.bold() << result.song.title << c.reset() << "\n";
    std::cout << c.cyan() << "  🎤 Artist:   " << c.reset()
              << result.song.artist << "\n";
    if (!result.song.album.empty()) {
        std::cout << c.cyan() << "  💿 Album:    " << c.reset()
                  << result.song.album << "\n";
    }
    std::cout << c.cyan() << "  ⏱️  Time:     " << c.reset()
              << std::fixed << std::setprecision(2)
              << result.timestamp << "s\n";
    std::cout << c.cyan() << "  📊 Matches:  " << c.reset()
              << result.match_count
              << " (exact: " << exact_count
              << ", fuzzy: " << fuzzy_count << ")\n";
    std::cout << c.cyan() << "  🎯 Conf:     " << c.reset()
              << c.green() << std::fixed << std::setprecision(1)
              << (result.confidence * 100) << "%" << c.reset() << "\n";
    std::cout << c.cyan() << "  ⚡ Latency:  " << c.reset()
              << result.recognition_time.count() << "ms\n";
    std::cout << c.green()
              << "───────────────────────────────────────" << c.reset() << "\n\n";
}

void printSegmentResult(const SegmentResult& seg, const ColorFuncs& c, int index) {
    std::string time_range = "[" + std::to_string((int)seg.start_time) + "s-" +
                            std::to_string((int)seg.end_time) + "s]";

    if (seg.matched) {
        std::cout << c.green() << "  " << std::setw(3) << index << ". "
                  << std::setw(12) << std::left << time_range
                  << " 🎵 " << seg.song.title << " - " << seg.song.artist
                  << " (" << std::fixed << std::setprecision(1)
                  << (seg.confidence * 100) << "%)"
                  << c.reset() << "\n";
    } else {
        std::cout << c.yellow() << "  " << std::setw(3) << index << ". "
                  << std::setw(12) << std::left << time_range
                  << " [No match]"
                  << c.reset() << "\n";
    }
}

SegmentResult recognizeSegment(const std::vector<Sample>& segment,
                               double start_time, double end_time,
                               Preprocessor& preprocessor,
                               FingerprintExtractor& extractor,
                               Matcher& matcher,
                               Database& db,
                               bool use_noise_reduction,
                               const ColorFuncs& c,
                               bool verbose) {
    SegmentResult result;
    result.start_time = start_time;
    result.end_time = end_time;
    result.matched = false;
    result.confidence = 0.0;
    result.match_count = 0;

    std::vector<FingerprintHash> accumulated_hashes;
    int frame_index = 0;

    int window_size = FRAME_SIZE;
    int hop_size = FRAME_SIZE / 2;

    for (size_t start = 0; start + window_size <= segment.size(); start += hop_size) {
        Frame frame(window_size);
        for (int i = 0; i < window_size; ++i) {
            frame[i] = segment[start + i];
        }

        Frame processed;
        preprocessor.process(frame, processed);

        auto peaks = extractor.extractPeaks(processed, frame_index, use_noise_reduction);
        auto hashes = extractor.generateHashes(peaks);

        for (const auto& h : hashes) {
            accumulated_hashes.push_back(h);
        }

        frame_index++;
    }

    if (accumulated_hashes.size() >= 20) {
        auto match_start = std::chrono::steady_clock::now();
        MatchResult match = matcher.match(accumulated_hashes, db, frame_index);
        auto match_end = std::chrono::steady_clock::now();

        if (match.matched) {
            result.matched = true;
            result.song = match.song;
            result.song_timestamp = match.timestamp;
            result.confidence = match.confidence;
            result.match_count = match.match_count;
        }
    }

    return result;
}

#ifdef ENABLE_STREAMING
int runStreamingMode(const Options& opts, const ColorFuncs& c) {
    std::cout << c.cyan() << "Mode: Streaming Recognition" << c.reset() << "\n";
    std::cout << c.cyan() << "URL: " << c.reset() << opts.stream_url << "\n";

    StreamInput stream;
    stream.setReconnectAttempts(opts.max_reconnect);
    stream.setReconnectDelay(opts.reconnect_delay);

    if (!stream.open(opts.stream_url)) {
        std::cerr << c.red() << "Error: Cannot open stream" << c.reset() << "\n";
        return 1;
    }

    StreamInfo info = stream.getStreamInfo();
    std::cout << c.cyan() << "Protocol: " << c.reset()
              << StreamInput::protocolToString(info.protocol) << "\n";
    std::cout << c.cyan() << "Sample Rate: " << c.reset() << info.sample_rate << " Hz\n";
    std::cout << c.cyan() << "Channels: " << c.reset() << info.channels << "\n";
    std::cout << c.cyan() << "Codec: " << c.reset() << info.codec_name << "\n";

    Database db;
    if (!db.open(opts.db_path)) {
        std::cerr << c.red() << "Error: Cannot open database" << c.reset() << "\n";
        return 1;
    }

    int song_count = db.getSongCount();
    if (song_count == 0) {
        std::cerr << c.red() << "Error: Database is empty" << c.reset() << "\n";
        return 1;
    }

    std::cout << c.cyan() << "Database: " << c.reset() << song_count << " songs\n";

    Matcher matcher(opts.match_threshold, opts.hamming_threshold);
    matcher.setUseFuzzyMatching(!opts.no_fuzzy);

    Preprocessor preprocessor(SAMPLE_RATE);
    FingerprintExtractor extractor(FRAME_SIZE);

    bool use_noise_reduction = !opts.no_noise_reduction;

    SrtWriter srt_writer;
    bool write_srt = !opts.srt_path.empty();
    if (write_srt) {
        if (!srt_writer.open(opts.srt_path)) {
            std::cerr << c.red() << "Error: Cannot open SRT file" << c.reset() << "\n";
            return 1;
        }
        std::cout << c.cyan() << "SRT Output: " << c.reset() << opts.srt_path << "\n";
    }

    SegmentManager segment_manager(opts.segment_duration, opts.overlap_duration);

    std::vector<SegmentResult> all_results;
    int segment_index = 0;

    segment_manager.setSegmentCallback([&](const std::vector<Sample>& segment,
                                           double start_time, double end_time) {
        SegmentResult result = recognizeSegment(
            segment, start_time, end_time,
            preprocessor, extractor, matcher, db,
            use_noise_reduction, c, opts.verbose);

        segment_index++;
        all_results.push_back(result);
        printSegmentResult(result, c, segment_index);

        if (write_srt) {
            srt_writer.writeEntry(result);
        }

        if (!opts.continuous && result.matched) {
            g_running.store(false);
        }
    });

    std::cout << c.green() << "\n🎧  Streaming..." << c.reset() << "\n";
    std::cout << "Segment duration: " << opts.segment_duration << "s\n";
    std::cout << "Overlap duration: " << opts.overlap_duration << "s\n";
    std::cout << "Press Ctrl+C to stop\n\n";

    stream.start([&](const std::vector<Sample>& samples, int64_t timestamp) {
        if (!g_running.load()) return;
        segment_manager.pushSamples(samples, timestamp);
    });

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (stream.getState() == StreamState::Stopped ||
            stream.getState() == StreamState::Error) {
            break;
        }
    }

    stream.stop();
    stream.close();
    db.close();

    if (write_srt) {
        srt_writer.close();
    }

    std::cout << "\n" << c.cyan() << "═══════════════════════════════════════" << c.reset() << "\n";
    std::cout << "Recognition Summary:\n";
    std::cout << "  Total segments: " << all_results.size() << "\n";

    int matched_count = 0;
    for (const auto& r : all_results) {
        if (r.matched) matched_count++;
    }
    std::cout << "  Matched: " << matched_count << "\n";
    std::cout << "  Unmatched: " << (all_results.size() - matched_count) << "\n";
    std::cout << c.cyan() << "═══════════════════════════════════════" << c.reset() << "\n";

    return matched_count > 0 ? 0 : 2;
}
#endif

int runMicrophoneMode(const Options& opts, const ColorFuncs& c) {
    AudioCapture capture(SAMPLE_RATE, FRAME_SIZE);

    if (opts.list_devices) {
        listDevices(capture);
        return 0;
    }

    if (opts.device_index >= 0) {
        capture.setDeviceIndex(opts.device_index);
    }

    Database db;
    if (!db.open(opts.db_path)) {
        std::cerr << c.red() << "Error: Cannot open database: "
                  << opts.db_path << c.reset() << "\n";
        std::cerr << "Run 'afp_builddb' first to generate the database.\n";
        return 1;
    }

    int song_count = db.getSongCount();
    if (song_count == 0) {
        std::cerr << c.red() << "Error: Database is empty." << c.reset() << "\n";
        std::cerr << "Run 'afp_builddb' first to generate the database.\n";
        return 1;
    }

    std::cout << c.cyan() << "Database loaded: " << c.reset()
              << song_count << " songs indexed\n";

    Matcher matcher(opts.match_threshold, opts.hamming_threshold);
    matcher.setUseFuzzyMatching(!opts.no_fuzzy);

    Preprocessor preprocessor(SAMPLE_RATE);
    FingerprintExtractor extractor(FRAME_SIZE);

    std::vector<FingerprintHash> accumulated_hashes;
    int frame_index = 0;
    int total_frames = 0;
    int noise_estimation_frames = 0;
    int total_noise_frames = Preprocessor::NOISE_ESTIMATION_TOTAL_FRAMES;

    bool use_noise_reduction = !opts.no_noise_reduction;
    bool noise_estimation_complete = !use_noise_reduction;

    int last_exact_count = 0;
    int last_fuzzy_count = 0;

    auto start_time = std::chrono::steady_clock::now();
    auto last_recognition = std::chrono::steady_clock::now();

    std::cout << c.cyan() << "Mode: Microphone Real-time" << c.reset() << "\n";
    std::cout << c.green() << "\n🎙️  Initializing..." << c.reset() << "\n";

    if (use_noise_reduction) {
        std::cout << c.yellow() << "🔊  Estimating background noise (2 seconds)..."
                  << c.reset() << "\n";
    }

    std::cout << "Press Ctrl+C to stop\n\n";

    bool started = capture.start([&](const Frame& frame) {
        if (!g_running.load() || g_matched.load()) return;

        Frame processed;

        if (!noise_estimation_complete) {
            preprocessor.processForNoiseEstimation(frame, processed);
            noise_estimation_frames++;

            extractor.extractPeaksForNoiseEstimation(processed, frame_index);

            if (opts.verbose) {
                double progress = static_cast<double>(noise_estimation_frames) /
                                 total_noise_frames * 100.0;
                std::cout << "\r  Noise estimation: " << std::fixed
                          << std::setprecision(1) << progress << "%"
                          << std::flush;
            }

            if (noise_estimation_frames >= total_noise_frames) {
                extractor.getNoiseEstimator().finalize();
                extractor.setNoiseEstimator(&extractor.getNoiseEstimator());
                noise_estimation_complete = true;
                g_noise_estimated.store(true);

                if (opts.verbose) {
                    std::cout << "\n" << c.green()
                             << "✓ Noise estimation complete" << c.reset() << "\n";
                }
                preprocessor.reset();
            }

            frame_index++;
            total_frames++;
            return;
        }

        preprocessor.process(frame, processed);

        auto peaks = extractor.extractPeaks(processed, frame_index, use_noise_reduction);
        auto hashes = extractor.generateHashes(peaks);

        for (const auto& h : hashes) {
            accumulated_hashes.push_back(h);
        }

        frame_index++;
        total_frames++;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_recognition);

        if (elapsed.count() >= 500 && accumulated_hashes.size() >= 20) {
            auto match_start = std::chrono::steady_clock::now();

            MatchResult result = matcher.match(accumulated_hashes, db, frame_index);

            auto match_end = std::chrono::steady_clock::now();
            result.recognition_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                match_end - match_start);

            if (result.matched && result.recognition_time.count() < MAX_RECOGNITION_TIME_MS) {
                g_matched.store(true);

                last_exact_count = 0;
                last_fuzzy_count = 0;
                if (result.match_count > 0) {
                    last_exact_count = static_cast<int>(result.match_count * 0.6);
                    last_fuzzy_count = result.match_count - last_exact_count;
                }

                printResult(result, c, last_exact_count, last_fuzzy_count);
                g_running.store(false);
            }

            if (opts.verbose && !result.matched) {
                std::cout << c.yellow() << "  ... analyzing ("
                          << accumulated_hashes.size() << " hashes, "
                          << result.recognition_time.count() << "ms)"
                          << c.reset() << "\r" << std::flush;
            }

            last_recognition = now;

            if (accumulated_hashes.size() > 500) {
                accumulated_hashes.erase(
                    accumulated_hashes.begin(),
                    accumulated_hashes.begin() + 250);
            }
        }

        if (opts.duration_ms > 0) {
            auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - start_time);
            if (total_elapsed.count() >= opts.duration_ms) {
                g_running.store(false);
            }
        }
    });

    if (!started) {
        std::cerr << c.red() << "Error: Failed to start audio capture" << c.reset() << "\n";
        return 1;
    }

    std::cout << c.cyan() << "Device: " << c.reset()
              << (opts.device_index >= 0 ?
                  capture.getDeviceName(opts.device_index) :
                  "Default") << "\n";
    std::cout << c.cyan() << "Sample Rate: " << c.reset()
              << SAMPLE_RATE << " Hz\n";
    std::cout << c.cyan() << "Frame Size: " << c.reset()
              << FRAME_SIZE << " samples\n";
    std::cout << c.cyan() << "Match Threshold: " << c.reset()
              << opts.match_threshold << "\n";
    std::cout << c.cyan() << "Hamming Threshold: " << c.reset()
              << opts.hamming_threshold << " bits\n";
    std::cout << c.cyan() << "Noise Reduction: " << c.reset()
              << (use_noise_reduction ? "Enabled" : "Disabled") << "\n";
    std::cout << c.cyan() << "Fuzzy Matching: " << c.reset()
              << (!opts.no_fuzzy ? "Enabled" : "Disabled") << "\n\n";

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (!capture.isRunning()) {
            std::cerr << c.red() << "\nAudio capture stopped unexpectedly"
                      << c.reset() << "\n";
            break;
        }
    }

    capture.stop();
    db.close();

    if (!g_matched.load()) {
        std::cout << c.yellow() << "\n\nNo match found." << c.reset() << "\n";
        std::cout << "Frames analyzed: " << total_frames << "\n";
        std::cout << "Hashes generated: " << accumulated_hashes.size() << "\n";
    }

    return g_matched.load() ? 0 : 2;
}

int main(int argc, char* argv[]) {
    Options opts = parseArgs(argc, argv);

    ColorFuncs c;
    if (opts.no_color) {
        c.disable();
    }

    printBanner();

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    try {
#ifdef ENABLE_STREAMING
        if (!opts.stream_url.empty()) {
            return runStreamingMode(opts, c);
        }
#endif
        return runMicrophoneMode(opts, c);

    } catch (const std::exception& e) {
        std::cerr << c.red() << "Error: " << e.what() << c.reset() << "\n";
        return 1;
    }
}
