#pragma once

#include "common.hpp"
#include "database.hpp"
#include <unordered_map>
#include <vector>

namespace afp {

struct SongMatch {
    int song_id;
    int count;
    int exact_count;
    int fuzzy_count;
    int best_delta;
    double confidence;
    std::unordered_map<int, int> delta_histogram;
};

class Matcher {
public:
    Matcher(int match_threshold = MATCH_THRESHOLD,
            int hamming_threshold = HAMMING_DISTANCE_THRESHOLD);

    MatchResult match(const std::vector<FingerprintHash>& hashes,
                      Database& db,
                      int current_frame_index);

    void setMatchThreshold(int threshold);
    void setHammingThreshold(int threshold);

    bool getUseFuzzyMatching() const { return use_fuzzy_matching_; }
    void setUseFuzzyMatching(bool use) { use_fuzzy_matching_ = use; }

private:
    int match_threshold_;
    int hamming_threshold_;
    bool use_fuzzy_matching_ = true;

    std::vector<SongMatch> analyzeCandidates(
        const std::vector<DbMatchCandidate>& candidates,
        const std::vector<FingerprintHash>& hashes,
        int current_frame_index);

    std::vector<SongMatch> fuzzyMatchCandidates(
        const std::vector<FingerprintHash>& hashes,
        Database& db,
        int current_frame_index);

    MatchResult selectBest(const std::vector<SongMatch>& matches, Database& db);
};

}
