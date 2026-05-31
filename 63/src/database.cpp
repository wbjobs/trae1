#include "database.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstring>

namespace afp {

Database::Database() = default;

Database::~Database() {
    close();
}

bool Database::open(const std::string& path) {
    path_ = path;
    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    execute("PRAGMA journal_mode=WAL;");
    execute("PRAGMA synchronous=NORMAL;");
    execute("PRAGMA cache_size=10000;");

    return true;
}

void Database::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::isOpen() const {
    return db_ != nullptr;
}

bool Database::execute(const std::string& sql) {
    if (!db_) return false;
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        std::cerr << "SQL: " << sql << std::endl;
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

bool Database::createTables() {
    if (!db_) return false;

    std::string sql = R"(
        CREATE TABLE IF NOT EXISTS songs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            artist TEXT NOT NULL,
            album TEXT,
            duration REAL
        );

        CREATE TABLE IF NOT EXISTS fingerprints (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            hash INTEGER NOT NULL,
            time_offset INTEGER NOT NULL,
            song_id INTEGER NOT NULL,
            FOREIGN KEY (song_id) REFERENCES songs(id) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_fingerprints_hash ON fingerprints(hash);
        CREATE INDEX IF NOT EXISTS idx_fingerprints_song ON fingerprints(song_id);
    )";

    return execute(sql);
}

bool Database::addSong(const SongInfo& song, int* song_id) {
    if (!db_) return false;

    std::string sql = "INSERT INTO songs (title, artist, album, duration) VALUES (";
    sql += "'" + std::string(song.title) + "', ";
    sql += "'" + std::string(song.artist) + "', ";
    sql += "'" + std::string(song.album) + "', ";
    sql += std::to_string(song.duration) + ");";

    if (!execute(sql)) return false;

    if (song_id) {
        *song_id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    }
    return true;
}

bool Database::addFingerprints(int song_id, const std::vector<DbFingerprint>& fps) {
    if (!db_) return false;

    execute("BEGIN TRANSACTION;");

    sqlite3_stmt* stmt;
    std::string sql = "INSERT INTO fingerprints (hash, time_offset, song_id) VALUES (?, ?, ?);";
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Prepare failed: " << sqlite3_errmsg(db_) << std::endl;
        execute("ROLLBACK;");
        return false;
    }

    for (const auto& fp : fps) {
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(fp.hash));
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(fp.time_offset));
        sqlite3_bind_int(stmt, 3, fp.song_id);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            std::cerr << "Insert failed: " << sqlite3_errmsg(db_) << std::endl;
            sqlite3_finalize(stmt);
            execute("ROLLBACK;");
            return false;
        }
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    execute("COMMIT;");
    return true;
}

std::vector<DbMatchCandidate> Database::queryCandidates(const std::vector<FingerprintHash>& hashes) {
    std::vector<DbMatchCandidate> candidates;

    if (!db_ || hashes.empty()) return candidates;

    if (hashes.size() > 999) {
        std::vector<FingerprintHash> subset(hashes.begin(), hashes.begin() + 999);
        auto partial = queryCandidates(subset);
        return partial;
    }

    std::string hash_list;
    for (size_t i = 0; i < hashes.size(); ++i) {
        if (i > 0) hash_list += ",";
        hash_list += std::to_string(hashes[i].hash);
    }

    std::string sql = "SELECT hash, time_offset, song_id FROM fingerprints WHERE hash IN (" + hash_list + ");";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Query failed: " << sqlite3_errmsg(db_) << std::endl;
        return candidates;
    }

    std::unordered_map<int, DbMatchCandidate> candidate_map;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        uint32_t hash = static_cast<uint32_t>(sqlite3_column_int64(stmt, 0));
        uint32_t time_offset = static_cast<uint32_t>(sqlite3_column_int64(stmt, 1));
        int song_id = sqlite3_column_int(stmt, 2);

        auto& cand = candidate_map[song_id];
        cand.song_id = song_id;
        cand.offsets.emplace_back(hash, time_offset);
    }

    sqlite3_finalize(stmt);

    for (auto& [id, cand] : candidate_map) {
        candidates.push_back(std::move(cand));
    }

    return candidates;
}

AllFingerprints Database::loadAllFingerprints() {
    AllFingerprints result;

    if (!db_) return result;

    auto songs = getAllSongs();
    for (const auto& song : songs) {
        result.song_info[song.id] = song;
    }

    std::string sql = "SELECT hash, time_offset, song_id FROM fingerprints;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Load all fingerprints failed: " << sqlite3_errmsg(db_) << std::endl;
        return result;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        DbFingerprint fp;
        fp.hash = static_cast<uint32_t>(sqlite3_column_int64(stmt, 0));
        fp.time_offset = static_cast<uint32_t>(sqlite3_column_int64(stmt, 1));
        fp.song_id = sqlite3_column_int(stmt, 2);
        result.fingerprints.push_back(fp);
    }

    sqlite3_finalize(stmt);
    return result;
}

SongInfo Database::getSongInfo(int song_id) {
    SongInfo song;
    song.id = song_id;

    if (!db_) return song;

    std::string sql = "SELECT title, artist, album, duration FROM songs WHERE id = " + std::to_string(song_id) + ";";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return song;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* title = (const char*)sqlite3_column_text(stmt, 0);
        const char* artist = (const char*)sqlite3_column_text(stmt, 1);
        const char* album = (const char*)sqlite3_column_text(stmt, 2);
        double duration = sqlite3_column_double(stmt, 3);

        song.title = title ? title : "Unknown";
        song.artist = artist ? artist : "Unknown";
        song.album = album ? album : "";
        song.duration = duration;
    }

    sqlite3_finalize(stmt);
    return song;
}

std::vector<SongInfo> Database::getAllSongs() {
    std::vector<SongInfo> songs;

    if (!db_) return songs;

    std::string sql = "SELECT id, title, artist, album, duration FROM songs ORDER BY id;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return songs;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SongInfo song;
        song.id = sqlite3_column_int(stmt, 0);
        const char* title = (const char*)sqlite3_column_text(stmt, 1);
        const char* artist = (const char*)sqlite3_column_text(stmt, 2);
        const char* album = (const char*)sqlite3_column_text(stmt, 3);
        song.duration = sqlite3_column_double(stmt, 4);

        song.title = title ? title : "Unknown";
        song.artist = artist ? artist : "Unknown";
        song.album = album ? album : "";

        songs.push_back(song);
    }

    sqlite3_finalize(stmt);
    return songs;
}

int Database::getSongCount() const {
    if (!db_) return 0;

    int count = 0;
    std::string sql = "SELECT COUNT(*) FROM songs;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

int Database::getFingerprintCount() const {
    if (!db_) return 0;

    int count = 0;
    std::string sql = "SELECT COUNT(*) FROM fingerprints;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

}
