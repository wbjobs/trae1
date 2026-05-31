#pragma once

#include "common.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace afp {

struct SrtEntry {
    int index;
    double start_time;
    double end_time;
    std::string text;
};

class SrtWriter {
public:
    SrtWriter();
    ~SrtWriter();

    bool open(const std::string& path);
    void close();

    void writeEntry(const SrtEntry& entry);
    void writeEntry(const SegmentResult& result);

    void writeHeader();
    void writeAllEntries(const std::vector<SrtEntry>& entries);
    void writeAllResults(const std::vector<SegmentResult>& results);

    int getEntryCount() const { return entry_count_; }

    static std::string formatTime(double seconds);

private:
    std::ofstream file_;
    int entry_count_ = 0;
    std::string path_;
};

}
