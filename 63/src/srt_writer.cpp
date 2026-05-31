#include "srt_writer.hpp"
#include <iostream>

namespace afp {

SrtWriter::SrtWriter() = default;

SrtWriter::~SrtWriter() {
    close();
}

bool SrtWriter::open(const std::string& path) {
    path_ = path;
    file_.open(path, std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
        std::cerr << "[SrtWriter] Cannot open file: " << path << std::endl;
        return false;
    }
    entry_count_ = 0;
    return true;
}

void SrtWriter::close() {
    if (file_.is_open()) {
        file_.close();
    }
}

std::string SrtWriter::formatTime(double seconds) {
    int total_ms = static_cast<int>(seconds * 1000);
    int h = total_ms / 3600000;
    int m = (total_ms % 3600000) / 60000;
    int s = (total_ms % 60000) / 1000;
    int ms = total_ms % 1000;

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << h << ":"
        << std::setw(2) << m << ":"
        << std::setw(2) << s << ","
        << std::setw(3) << ms;
    return oss.str();
}

void SrtWriter::writeEntry(const SrtEntry& entry) {
    if (!file_.is_open()) return;

    file_ << entry.index << "\n";
    file_ << formatTime(entry.start_time) << " --> " << formatTime(entry.end_time) << "\n";
    file_ << entry.text << "\n\n";
    file_.flush();

    entry_count_++;
}

void SrtWriter::writeEntry(const SegmentResult& result) {
    SrtEntry entry;
    entry.index = entry_count_ + 1;
    entry.start_time = result.start_time;
    entry.end_time = result.end_time;

    if (result.matched) {
        std::ostringstream oss;
        oss << result.song.title << " - " << result.song.artist;
        if (result.confidence > 0) {
            oss << " (" << std::fixed << std::setprecision(1)
                << (result.confidence * 100) << "%)";
        }
        entry.text = oss.str();
    } else {
        entry.text = "[No match]";
    }

    writeEntry(entry);
}

void SrtWriter::writeHeader() {
}

void SrtWriter::writeAllEntries(const std::vector<SrtEntry>& entries) {
    for (const auto& entry : entries) {
        writeEntry(entry);
    }
}

void SrtWriter::writeAllResults(const std::vector<SegmentResult>& results) {
    for (const auto& result : results) {
        writeEntry(result);
    }
}

}
