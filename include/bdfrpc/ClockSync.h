#pragma once

#include "bdfrpc/Types.h"
#include <cstdint>

namespace bdfrpc {

struct ClockSyncStats {
    bool initialized{false};
    TimestampNs offset_ns{0};
    TimestampNs jitter_ns{0};
    std::uint64_t accepted_samples{0};
    std::uint64_t rejected_samples{0};
};

class ClockOffsetEstimator {
public:
    explicit ClockOffsetEstimator(
        double alpha = 0.05,
        TimestampNs outlier_threshold_ns = 250'000'000);

    // Observes a remote timestamp and its local arrival timestamp, then returns
    // the remote timestamp mapped into the local clock domain.
    TimestampNs update(TimestampNs remote_ns, TimestampNs local_arrival_ns);

    TimestampNs to_local(TimestampNs remote_ns) const noexcept;
    void reset() noexcept;

    const ClockSyncStats& stats() const noexcept { return stats_; }

private:
    double alpha_;
    TimestampNs outlier_threshold_ns_;
    ClockSyncStats stats_;
};

} // namespace bdfrpc
