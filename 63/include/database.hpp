#pragma once

#include "common.hpp"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace afp {

struct DbFingerprint {
    uint32_t hash;
    uint32_t time_offset;
    int song_id;
};

struct DbMatchCandidate {
    int song_id;
    std::vector<std::pair<uint32_t, uint32_t>> offsets;
};

struct AllFingerprints {
    std::vector<DbFingerprint> fingerprints;
    std::unordered_map<int, SongInfo> song_info;
};

class Database {
public:
    Database();
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(const std::string& path);
    void close();
    bool isOpen() const;

    bool createTables();
    bool addSong(const SongInfo& song, int* song_id = nullptr);
    bool addFingerprints(int song_id, const std::vector<DbFingerprint>& fps);

    std::vector<DbMatchCandidate> queryCandidates(const std::vector<FingerprintHash>& hashes);

    AllFingerprints loadAllFingerprints();

    SongInfo getSongInfo(int song_id);
    std::vector<SongInfo> getAllSongs();

    int getSongCount() const;
    int getFingerprintCount() const;

private:
    sqlite3* db_ = nullptr;
    std::string path_;

    bool execute(const std::string& sql);
};

}
