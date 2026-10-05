#pragma once

#include "bdfrpc/FusionCore.h"
#include "bdfrpc/LatestFrameStore.h"
#include "bdfrpc/SolverAdapters.h"
#include "bdfrpc/SourceHealth.h"

namespace bdfrpc {

class FusionRuntime {
public:
    explicit FusionRuntime(
        OperatingMode mode = OperatingMode::Hybrid,
        TimestampNs retention_ns = 1'000'000'000);

    void set_mode(OperatingMode mode);
    OperatingMode mode() const noexcept { return mode_; }

    bool submit(CaptureFrame frame);
    FusedPerformanceFrame evaluate(TimestampNs now_ns) const;

    LatestFrameStore& frame_store() noexcept { return frames_; }
    const LatestFrameStore& frame_store() const noexcept { return frames_; }

    SourceHealthMonitor& health() noexcept { return health_; }
    const SourceHealthMonitor& health() const noexcept { return health_; }

    SourceRouter& router() noexcept { return fusion_.router(); }
    const SourceRouter& router() const noexcept { return fusion_.router(); }

private:
    OperatingMode mode_;
    LatestFrameStore frames_;
    mutable SourceHealthMonitor health_;
    FusionCore fusion_;
};

} // namespace bdfrpc
