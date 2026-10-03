#include "bdfrpc/SourceRouter.h"
#include <cmath>
#include <utility>

namespace bdfrpc {

void SourceRouter::set_route(Domain domain, std::vector<RouteCandidate> candidates) {
    routes_[domain] = std::move(candidates);
}

std::optional<RoutedSample> SourceRouter::select(
    Domain domain,
    TimestampNs output_time_ns,
    const std::vector<CaptureFrame>& frames) const {

    const auto route_it = routes_.find(domain);
    if (route_it == routes_.end()) return std::nullopt;

    for (const auto& candidate : route_it->second) {
        const CaptureFrame* best_frame = nullptr;
        const DomainSample* best_sample = nullptr;

        for (const auto& frame : frames) {
            if (frame.source_id != candidate.source_id) continue;
            const auto age = std::llabs(output_time_ns - frame.timestamp_ns);
            if (age > candidate.stale_after_ns) continue;

            for (const auto& sample : frame.samples) {
                if (sample.domain != domain) continue;
                if (sample.confidence < candidate.minimum_confidence) continue;
                if (!best_sample || sample.confidence > best_sample->confidence) {
                    best_frame = &frame;
                    best_sample = &sample;
                }
            }
        }

        if (best_frame && best_sample) {
            return RoutedSample{*best_sample, best_frame->source_id};
        }
    }

    return std::nullopt;
}

} // namespace bdfrpc
