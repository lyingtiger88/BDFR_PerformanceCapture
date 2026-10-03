#pragma once

#include "bdfrpc/Types.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bdfrpc {

struct RouteCandidate {
    std::string source_id;
    float minimum_confidence{0.0f};
    TimestampNs stale_after_ns{100'000'000};
};

struct RoutedSample {
    DomainSample sample;
    std::string source_id;
};

class SourceRouter {
public:
    void set_route(Domain domain, std::vector<RouteCandidate> candidates);

    std::optional<RoutedSample> select(
        Domain domain,
        TimestampNs output_time_ns,
        const std::vector<CaptureFrame>& frames) const;

private:
    struct DomainHash {
        std::size_t operator()(Domain d) const noexcept {
            return static_cast<std::size_t>(d);
        }
    };

    std::unordered_map<Domain, std::vector<RouteCandidate>, DomainHash> routes_;
};

} // namespace bdfrpc
