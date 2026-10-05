#include "bdfrpc/FusionRuntime.h"

#include <utility>

namespace bdfrpc {

FusionRuntime::FusionRuntime(
    OperatingMode mode,
    TimestampNs retention_ns)
    : mode_(mode),
      frames_(retention_ns) {
    configure_default_routes(mode_, fusion_.router());
}

void FusionRuntime::set_mode(OperatingMode mode) {
    mode_ = mode;
    configure_default_routes(mode_, fusion_.router());
}

bool FusionRuntime::submit(CaptureFrame frame) {
    const std::string source_id = frame.source_id;
    const TimestampNs timestamp_ns = frame.timestamp_ns;

    if (!frames_.push(std::move(frame))) {
        if (!source_id.empty()) {
            health_.observe_failure(
                source_id,
                timestamp_ns,
                "rejected stale/out-of-order solver frame");
        }
        return false;
    }

    health_.configure(source_id, 500'000'000);
    health_.observe_frame(source_id, timestamp_ns);
    return true;
}

FusedPerformanceFrame FusionRuntime::evaluate(TimestampNs now_ns) const {
    return fusion_.fuse(frames_.snapshot(now_ns));
}

} // namespace bdfrpc
