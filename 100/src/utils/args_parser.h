#pragma once

#include "utils/common.h"

class ArgsParser {
public:
    static ServiceConfig parse(int argc, char* argv[]);
    static void printUsage(const char* programName);
    static void printConfig(const ServiceConfig& config);

private:
    static std::string parseStringArg(int argc, char* argv[], const std::string& flag,
                                       const std::string& defaultVal);
    static int parseIntArg(int argc, char* argv[], const std::string& flag, int defaultVal);
    static double parseDoubleArg(int argc, char* argv[], const std::string& flag, double defaultVal);
    static bool parseBoolArg(int argc, char* argv[], const std::string& flag);
    static std::vector<std::string> parseCSVArgs(int argc, char* argv[], const std::string& flag);
};