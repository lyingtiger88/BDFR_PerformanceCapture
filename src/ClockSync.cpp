#include "bdfrpc/ClockSync.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bdfrpc {

namespace {

TimestampNs clamp_ns(long double value) noexcept {
    const long double lo =
        static_cast<long double>(std::numeric_limits<TimestampNs>::min());
    const long double hi =
        static_cast<long double>(std::numeric_limits<TimestampNs>::max());
    if (value <= lo) return std::numeric_limits<TimestampNs>::min();
    if (value >= hi) return std::numeric_limits<TimestampNs>::max();
    return static_cast<TimestampNs>(std::llround(value));
}

TimestampNs absolute_ns(TimestampNs value) noexcept {
    if (value == std::numeric_limits<TimestampNs>::min()) {
        return std::numeric_limits<TimestampNs>::max();
    }
    return value < 0 ? -value : value;
}

} // namespace

ClockOffsetEstimator::ClockOffsetEstimator(
    double alpha,
    TimestampNs outlier_threshold_ns)
    : alpha_(std::clamp(alpha, 0.001, 1.0)),
      outlier_threshold_ns_(std::max<TimestampNs>(0, outlier_threshold_ns)) {}

TimestampNs ClockOffsetEstimator::update(
    TimestampNs remote_ns,
    TimestampNs local_arrival_ns) {

    const TimestampNs observed =
        clamp_ns(static_cast<long double>(local_arrival_ns) -
                 static_cast<long double>(remote_ns));

    if (!stats_.initialized) {
        stats_.initialized = true;
        stats_.offset_ns = observed;
        stats_.jitter_ns = 0;
        stats_.accepted_samples = 1;
        return to_local(remote_ns);
    }

    const TimestampNs residual =
        clamp_ns(static_cast<long double>(observed) -
                 static_cast<long double>(stats_.offset_ns));

    if (outlier_threshold_ns_ > 0 &&
        absolute_ns(residual) > outlier_threshold_ns_) {
        ++stats_.rejected_samples;
        return to_local(remote_ns);
    }

    const long double updated_offset =
        static_cast<long double>(stats_.offset_ns) +
        static_cast<long double>(alpha_) *
            static_cast<long double>(residual);

    const long double updated_jitter =
        (1.0L - static_cast<long double>(alpha_)) *
            static_cast<long double>(stats_.jitter_ns) +
        static_cast<long double>(alpha_) *
            static_cast<long double>(absolute_ns(residual));

    stats_.offset_ns = clamp_ns(updated_offset);
    stats_.jitter_ns = clamp_ns(updated_jitter);
    ++stats_.accepted_samples;
    return to_local(remote_ns);
}

TimestampNs ClockOffsetEstimator::to_local(TimestampNs remote_ns) const noexcept {
    if (!stats_.initialized) return remote_ns;
    return clamp_ns(
        static_cast<long double>(remote_ns) +
        static_cast<long double>(stats_.offset_ns));
}

void ClockOffsetEstimator::reset() noexcept {
    stats_ = {};
}

} // namespace bdfrpc
