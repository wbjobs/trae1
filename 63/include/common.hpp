#pragma once

#include <vector>
#include <complex>
#include <cstdint>
#include <string>
#include <chrono>
#include <cmath>
#include <functional>

namespace afp {

constexpr int SAMPLE_RATE = 44100;
constexpr int FRAME_SIZE = 1024;
constexpr int NUM_CHANNELS = 1;
constexpr double HIGH_PASS_CUTOFF = 150.0;
constexpr int FINGERPRINT_BINS = 64;
constexpr int FAN_VALUE = 5;
constexpr int TARGET_ZONE_START = 1;
constexpr int TARGET_ZONE_END = 6;
constexpr double PEAK_THRESHOLD = 0.3;
constexpr int MATCH_THRESHOLD = 5;
constexpr int MAX_RECOGNITION_TIME_MS = 3000;
constexpr int DB_FINGERPRINT_WINDOW = 4096;
constexpr int DB_FINGERPRINT_HOP = 1024;

constexpr int NOISE_ESTIMATION_FRAMES = 86;
constexpr double SPECTRAL_SUBTRACTION_FACTOR = 1.5;
constexpr double SPECTRAL_SUBTRACTION_FLOOR = 0.02;
constexpr int HAMMING_DISTANCE_THRESHOLD = 5;
constexpr int FUZZY_MATCH_BATCH_SIZE = 1000;
constexpr double NOISE_ESTIMATION_DURATION = 2.0;

constexpr double STREAM_SEGMENT_DURATION = 10.0;
constexpr int STREAM_BUFFER_SIZE = 44100 * 10;
constexpr int STREAM_MAX_RECONNECT_ATTEMPTS = 5;
constexpr int STREAM_RECONNECT_DELAY_MS = 2000;
constexpr int STREAM_READ_TIMEOUT_MS = 5000;
constexpr double STREAM_OVERLAP_DURATION = 1.0;

using Sample = double;
using Frame = std::vector<Sample>;
using Complex = std::complex<double>;
using Spectrum = std::vector<Complex>;
using Magnitudes = std::vector<double>;
using NoiseSpectrum = std::vector<double>;

struct Peak {
    int bin;
    double magnitude;
    int frame_index;
};

struct FingerprintHash {
    uint32_t hash;
    uint32_t time_delta;
};

struct FuzzyHash {
    uint32_t hash;
    uint32_t mask;
    uint32_t time_delta;
};

struct SongInfo {
    int id;
    std::string title;
    std::string artist;
    std::string album;
    double duration;
};

struct MatchResult {
    bool matched;
    SongInfo song;
    double timestamp;
    int match_count;
    double confidence;
    std::chrono::milliseconds recognition_time;
};

struct SegmentResult {
    double start_time;
    double end_time;
    bool matched;
    SongInfo song;
    double song_timestamp;
    double confidence;
    int match_count;
};

struct NoiseEstimator {
    NoiseSpectrum noise_spectrum;
    int frame_count = 0;
    bool ready = false;

    void init(int size) {
        noise_spectrum.assign(size, 0.0);
        frame_count = 0;
        ready = false;
    }

    void accumulate(const Magnitudes& mags) {
        if (mags.size() != noise_spectrum.size()) return;
        for (size_t i = 0; i < mags.size(); ++i) {
            noise_spectrum[i] += mags[i];
        }
        frame_count++;
    }

    void finalize() {
        if (frame_count > 0) {
            for (auto& n : noise_spectrum) {
                n /= frame_count;
            }
            ready = true;
        }
    }

    void subtract(Magnitudes& mags, double factor = SPECTRAL_SUBTRACTION_FACTOR,
                  double floor = SPECTRAL_SUBTRACTION_FLOOR) const {
        if (!ready || mags.size() != noise_spectrum.size()) return;
        for (size_t i = 0; i < mags.size(); ++i) {
            double subtracted = mags[i] - factor * noise_spectrum[i];
            double floor_val = floor * mags[i];
            mags[i] = std::max(subtracted, floor_val);
        }
    }
};

inline int hammingDistance(uint32_t a, uint32_t b) {
    uint32_t xor_val = a ^ b;
    int count = 0;
    while (xor_val) {
        xor_val &= (xor_val - 1);
        count++;
    }
    return count;
}

inline bool fuzzyMatch(uint32_t a, uint32_t b, int threshold = HAMMING_DISTANCE_THRESHOLD) {
    return hammingDistance(a, b) <= threshold;
}

}
