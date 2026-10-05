#pragma once

#include "bdfrpc/Types.h"
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bdfrpc {

struct StreamSyncStats {
    std::uint64_t received_frames{0};
    std::uint64_t dropped_overflow{0};
    std::uint64_t dropped_stale{0};
    std::uint64_t emitted_frames{0};
    TimestampNs last_timestamp_ns{0};
    TimestampNs last_offset_ns{0};
};

struct SyncStats {
    std::uint64_t emitted_sets{0};
    TimestampNs last_set_skew_ns{0};
    TimestampNs max_set_skew_ns{0};
    std::unordered_map<std::string, StreamSyncStats> streams;
};

class FrameSynchronizer {
public:
    explicit FrameSynchronizer(TimestampNs tolerance_ns = 8'000'000,
                               std::size_t max_queue_per_stream = 8);

    void set_required_streams(std::vector<std::string> stream_ids);
    void set_tolerance_ns(TimestampNs tolerance_ns) noexcept;
    void push(CaptureFrame frame);
    std::optional<SyncedFrameSet> try_pop();
    void clear();

    TimestampNs tolerance_ns() const noexcept { return tolerance_ns_; }
    const SyncStats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept;

private:
    TimestampNs tolerance_ns_;
    std::size_t max_queue_per_stream_;
    std::vector<std::string> required_streams_;
    std::unordered_map<std::string, std::deque<CaptureFrame>> queues_;
    SyncStats stats_;
};

} // namespace bdfrpc
