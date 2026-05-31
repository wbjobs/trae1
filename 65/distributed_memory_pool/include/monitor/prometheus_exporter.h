#pragma once

#include "common/types.h"
#include "memory/distributed_lock_manager.h"
#include <string>
#include <sstream>
#include <functional>

namespace dmp {

class PrometheusExporter {
public:
    using StatsProvider = std::function<LockStats()>;

    PrometheusExporter();
    ~PrometheusExporter();

    void set_stats_provider(StatsProvider provider);

    std::string get_metrics() const;

    static std::string format_lock_stats(const LockStats& stats);

private:
    StatsProvider stats_provider_;
};

}
