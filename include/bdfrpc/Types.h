#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bdfrpc {

using TimestampNs = std::int64_t;

enum class Domain {
    Face,
    Head,
    Body,
    LeftHand,
    RightHand
};

inline const char* to_string(Domain domain) noexcept {
    switch (domain) {
        case Domain::Face: return "face";
        case Domain::Head: return "head";
        case Domain::Body: return "body";
        case Domain::LeftHand: return "left_hand";
        case Domain::RightHand: return "right_hand";
    }
    return "unknown";
}

struct DomainSample {
    Domain domain{Domain::Body};
    float confidence{0.0f};
    std::vector<float> values;
};

struct CaptureFrame {
    std::string source_id;
    std::string stream_id;
    std::uint64_t sequence{0};
    TimestampNs timestamp_ns{0};
    std::vector<DomainSample> samples;
};

struct SyncedFrameSet {
    TimestampNs timestamp_ns{0};
    std::vector<CaptureFrame> frames;
    TimestampNs max_skew_ns{0};
};

struct FusedPerformanceFrame {
    TimestampNs timestamp_ns{0};
    std::vector<DomainSample> samples;
    std::vector<std::string> selected_sources;
};

} // namespace bdfrpc
