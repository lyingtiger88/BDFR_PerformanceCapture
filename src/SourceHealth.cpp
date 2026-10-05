#include "bdfrpc/SourceHealth.h"

#include <algorithm>
#include <utility>

namespace bdfrpc {

void SourceHealthMonitor::configure(
    std::string source_id,
    TimestampNs timeout_ns) {

    if (source_id.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);

    auto& info = sources_[source_id];
    info.source_id = std::move(source_id);
    info.timeout_ns = std::max<TimestampNs>(1, timeout_ns);
}

void SourceHealthMonitor::observe_frame(
    const std::string& source_id,
    TimestampNs timestamp_ns) {

    if (source_id.empty() || timestamp_ns <= 0) return;
    std::lock_guard<std::mutex> lock(mutex_);

    auto& info = sources_[source_id];
    info.source_id = source_id;
    if (info.timeout_ns <= 0) info.timeout_ns = 500'000'000;
    ++info.frames_observed;
    info.last_frame_ns = timestamp_ns;
    info.state = SourceHealthState::Healthy;
    info.message.clear();
}

void SourceHealthMonitor::observe_failure(
    const std::string& source_id,
    TimestampNs timestamp_ns,
    std::string message) {

    if (source_id.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);

    auto& info = sources_[source_id];
    info.source_id = source_id;
    if (info.timeout_ns <= 0) info.timeout_ns = 500'000'000;
    ++info.failures;
    if (timestamp_ns > 0) info.last_frame_ns = timestamp_ns;
    info.state = SourceHealthState::Failed;
    info.message = std::move(message);
}

bool SourceHealthMonitor::remove(const std::string& source_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return sources_.erase(source_id) > 0;
}

std::vector<SourceHealthInfo> SourceHealthMonitor::snapshot(
    TimestampNs now_ns) const {

    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SourceHealthInfo> out;
    out.reserve(sources_.size());

    for (const auto& [id, stored] : sources_) {
        (void)id;
        auto info = stored;

        if (info.state != SourceHealthState::Failed) {
            if (info.last_frame_ns <= 0) {
                info.state = SourceHealthState::Unknown;
            } else if (
                now_ns > info.last_frame_ns &&
                now_ns - info.last_frame_ns > info.timeout_ns) {
                info.state = SourceHealthState::Stale;
                if (info.message.empty()) info.message = "source timeout";
            } else {
                info.state = SourceHealthState::Healthy;
                if (info.message == "source timeout") info.message.clear();
            }
        }

        out.push_back(std::move(info));
    }

    std::sort(
        out.begin(), out.end(),
        [](const SourceHealthInfo& a, const SourceHealthInfo& b) {
            return a.source_id < b.source_id;
        });

    return out;
}

const char* to_string(SourceHealthState state) noexcept {
    switch (state) {
        case SourceHealthState::Unknown: return "unknown";
        case SourceHealthState::Healthy: return "healthy";
        case SourceHealthState::Stale: return "stale";
        case SourceHealthState::Failed: return "failed";
    }
    return "unknown";
}

} // namespace bdfrpc
