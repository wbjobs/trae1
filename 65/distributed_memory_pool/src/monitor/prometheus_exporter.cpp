#include "monitor/prometheus_exporter.h"

namespace dmp {

PrometheusExporter::PrometheusExporter() = default;

PrometheusExporter::~PrometheusExporter() = default;

void PrometheusExporter::set_stats_provider(StatsProvider provider) {
    stats_provider_ = std::move(provider);
}

std::string PrometheusExporter::get_metrics() const {
    if (!stats_provider_) {
        return "";
    }

    LockStats stats = stats_provider_();
    return format_lock_stats(stats);
}

std::string PrometheusExporter::format_lock_stats(const LockStats& stats) {
    std::ostringstream oss;

    oss << "# HELP dmp_lock_total Total number of locks created\n";
    oss << "# TYPE dmp_lock_total counter\n";
    oss << "dmp_lock_total " << stats.total_locks << "\n";

    oss << "# HELP dmp_lock_active Current number of active locks\n";
    oss << "# TYPE dmp_lock_active gauge\n";
    oss << "dmp_lock_active " << stats.active_locks << "\n";

    oss << "# HELP dmp_lock_acquisitions_total Total number of lock acquisitions\n";
    oss << "# TYPE dmp_lock_acquisitions_total counter\n";
    oss << "dmp_lock_acquisitions_total " << stats.lock_acquisitions << "\n";

    oss << "# HELP dmp_lock_releases_total Total number of lock releases\n";
    oss << "# TYPE dmp_lock_releases_total counter\n";
    oss << "dmp_lock_releases_total " << stats.lock_releases << "\n";

    oss << "# HELP dmp_lock_timeouts_total Total number of lock timeouts\n";
    oss << "# TYPE dmp_lock_timeouts_total counter\n";
    oss << "dmp_lock_timeouts_total " << stats.lock_timeouts << "\n";

    oss << "# HELP dmp_lock_contentions_total Total number of lock contentions\n";
    oss << "# TYPE dmp_lock_contentions_total counter\n";
    oss << "dmp_lock_contentions_total " << stats.lock_contentions << "\n";

    oss << "# HELP dmp_deadlocks_detected_total Total number of deadlocks detected\n";
    oss << "# TYPE dmp_deadlocks_detected_total counter\n";
    oss << "dmp_deadlocks_detected_total " << stats.deadlocks_detected << "\n";

    oss << "# HELP dmp_lock_avg_hold_time_ms Average lock hold time in milliseconds\n";
    oss << "# TYPE dmp_lock_avg_hold_time_ms gauge\n";
    oss << "dmp_lock_avg_hold_time_ms " << stats.avg_lock_hold_time_ms << "\n";

    return oss.str();
}

}
