#include "matcher.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

namespace afp {

Matcher::Matcher(int match_threshold, int hamming_threshold)
    : match_threshold_(match_threshold), hamming_threshold_(hamming_threshold) {}

void Matcher::setMatchThreshold(int threshold) {
    match_threshold_ = threshold;
}

void Matcher::setHammingThreshold(int threshold) {
    hamming_threshold_ = threshold;
}

MatchResult Matcher::match(const std::vector<FingerprintHash>& hashes,
                           Database& db,
                           int current_frame_index) {
    MatchResult result;
    result.matched = false;
    result.confidence = 0.0;
    result.match_count = 0;

    if (hashes.empty()) return result;

    if (use_fuzzy_matching_) {
        auto matches = fuzzyMatchCandidates(hashes, db, current_frame_index);
        if (!matches.empty()) {
            return selectBest(matches, db);
        }
    }

    auto candidates = db.queryCandidates(hashes);
    if (candidates.empty()) return result;

    auto matches = analyzeCandidates(candidates, hashes, current_frame_index);
    if (matches.empty()) return result;

    return selectBest(matches, db);
}

std::vector<SongMatch> Matcher::analyzeCandidates(
    const std::vector<DbMatchCandidate>& candidates,
    const std::vector<FingerprintHash>& hashes,
    int current_frame_index) {

    std::vector<SongMatch> results;

    for (const auto& cand : candidates) {
        SongMatch sm;
        sm.song_id = cand.song_id;
        sm.count = 0;
        sm.exact_count = 0;
        sm.fuzzy_count = 0;
        sm.best_delta = 0;
        sm.confidence = 0.0;

        for (const auto& [db_hash, db_offset] : cand.offsets) {
            for (const auto& h : hashes) {
                if (h.hash == db_hash) {
                    int delta = static_cast<int>(db_offset) - h.time_delta;
                    sm.delta_histogram[delta]++;
                    sm.count++;
                    sm.exact_count++;
                }
            }
        }

        if (sm.count > 0) {
            int best_count = 0;
            for (const auto& [delta, count] : sm.delta_histogram) {
                if (count > best_count) {
                    best_count = count;
                    sm.best_delta = delta;
                }
            }

            double total_hashes = static_cast<double>(hashes.size());
            sm.confidence = static_cast<double>(best_count) / std::max(total_hashes, 1.0);

            if (sm.count >= match_threshold_) {
                results.push_back(sm);
            }
        }
    }

    return results;
}

std::vector<SongMatch> Matcher::fuzzyMatchCandidates(
    const std::vector<FingerprintHash>& hashes,
    Database& db,
    int current_frame_index) {

    std::vector<SongMatch> results;

    if (hashes.empty()) return results;

    std::unordered_map<int, SongMatch> song_matches;

    int batch_size = FUZZY_MATCH_BATCH_SIZE;
    for (size_t start = 0; start < hashes.size(); start += batch_size) {
        size_t end = std::min(start + batch_size, hashes.size());

        std::vector<FingerprintHash> batch_hashes(hashes.begin() + start, hashes.begin() + end);

        auto candidates = db.queryCandidates(batch_hashes);

        for (const auto& cand : candidates) {
            auto& sm = song_matches[cand.song_id];
            sm.song_id = cand.song_id;

            for (const auto& [db_hash, db_offset] : cand.offsets) {
                for (const auto& h : batch_hashes) {
                    if (h.hash == db_hash) {
                        int delta = static_cast<int>(db_offset) - h.time_delta;
                        sm.delta_histogram[delta]++;
                        sm.count++;
                        sm.exact_count++;
                    } else if (hammingThreshold_ > 0 &&
                               hammingDistance(h.hash, db_hash) <= hamming_threshold_) {
                        int delta = static_cast<int>(db_offset) - h.time_delta;
                        sm.delta_histogram[delta]++;
                        sm.count++;
                        sm.fuzzy_count++;
                    }
                }
            }
        }
    }

    for (auto& [song_id, sm] : song_matches) {
        if (sm.count > 0) {
            int best_count = 0;
            for (const auto& [delta, count] : sm.delta_histogram) {
                if (count > best_count) {
                    best_count = count;
                    sm.best_delta = delta;
                }
            }

            double total_hashes = static_cast<double>(hashes.size());
            double exact_weight = 1.0;
            double fuzzy_weight = 0.5;
            double weighted_count = sm.exact_count * exact_weight +
                                    sm.fuzzy_count * fuzzy_weight;

            sm.confidence = weighted_count / std::max(total_hashes, 1.0);

            if (sm.count >= match_threshold_) {
                results.push_back(sm);
            }
        }
    }

    return results;
}

MatchResult Matcher::selectBest(const std::vector<SongMatch>& matches, Database& db) {
    MatchResult result;
    result.matched = false;

    if (matches.empty()) return result;

    auto best = std::max_element(matches.begin(), matches.end(),
        [](const SongMatch& a, const SongMatch& b) {
            if (a.exact_count != b.exact_count) {
                return a.exact_count < b.exact_count;
            }
            return a.confidence < b.confidence;
        });

    if (best == matches.end() || best->confidence <= 0.0) return result;

    result.matched = true;
    result.song = db.getSongInfo(best->song_id);
    result.match_count = best->count;
    result.confidence = best->confidence;
    result.timestamp = static_cast<double>(best->best_delta) * FRAME_SIZE / SAMPLE_RATE;

    return result;
}

}
