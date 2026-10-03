#include "bdfrpc/FusionCore.h"
#include <array>
#include <utility>

namespace bdfrpc {

FusionCore::FusionCore(SourceRouter router) : router_(std::move(router)) {}

FusedPerformanceFrame FusionCore::fuse(const SyncedFrameSet& synced) const {
    FusedPerformanceFrame out;
    out.timestamp_ns = synced.timestamp_ns;

    constexpr std::array<Domain, 5> domains{
        Domain::Face, Domain::Head, Domain::Body,
        Domain::LeftHand, Domain::RightHand
    };

    for (const auto domain : domains) {
        if (auto routed = router_.select(domain, synced.timestamp_ns, synced.frames)) {
            out.samples.push_back(std::move(routed->sample));
            out.selected_sources.push_back(std::move(routed->source_id));
        }
    }

    return out;
}

} // namespace bdfrpc
