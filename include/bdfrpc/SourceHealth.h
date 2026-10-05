#pragma once

#include "bdfrpc/Types.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bdfrpc {

enum class SourceHealthState {
    Unknown,
    Healthy,
    Stale,
    Failed
};

struct SourceHealthInfo {
    std::string source_id;
    SourceHealthState state{SourceHealthState::Unknown};
    std::uint64_t frames_observed{0};
    std::uint64_t failures{0};
    TimestampNs last_frame_ns{0};
    TimestampNs timeout_ns{500'000'000};
    std::string message;
};

class SourceHealthMonitor {
public:
    void configure(
        std::string source_id,
        TimestampNs timeout_ns = 500'000'000);

    void observe_frame(
        const std::string& source_id,
        TimestampNs timestamp_ns);

    void observe_failure(
        const std::string& source_id,
        TimestampNs timestamp_ns,
        std::string message);

    bool remove(const std::string& source_id);

    std::vector<SourceHealthInfo> snapshot(
        TimestampNs now_ns) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SourceHealthInfo> sources_;
};

const char* to_string(SourceHealthState state) noexcept;

} // namespace bdfrpc
