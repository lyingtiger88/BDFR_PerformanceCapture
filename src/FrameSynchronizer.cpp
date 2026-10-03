#include "bdfrpc/FrameSynchronizer.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace bdfrpc {

FrameSynchronizer::FrameSynchronizer(TimestampNs tolerance_ns,
                                     std::size_t max_queue_per_stream)
    : tolerance_ns_(tolerance_ns),
      max_queue_per_stream_(std::max<std::size_t>(1, max_queue_per_stream)) {}

void FrameSynchronizer::set_required_streams(std::vector<std::string> stream_ids) {
    required_streams_ = std::move(stream_ids);
}

void FrameSynchronizer::push(CaptureFrame frame) {
    auto& q = queues_[frame.stream_id];
    q.push_back(std::move(frame));
    while (q.size() > max_queue_per_stream_) q.pop_front();
}

std::optional<SyncedFrameSet> FrameSynchronizer::try_pop() {
    if (required_streams_.empty()) return std::nullopt;

    for (const auto& id : required_streams_) {
        auto it = queues_.find(id);
        if (it == queues_.end() || it->second.empty()) return std::nullopt;
    }

    // Drop frames that are provably too old relative to the newest queue front.
    bool changed = true;
    while (changed) {
        changed = false;
        TimestampNs newest_front = std::numeric_limits<TimestampNs>::min();
        for (const auto& id : required_streams_) {
            newest_front = std::max(newest_front, queues_[id].front().timestamp_ns);
        }

        for (const auto& id : required_streams_) {
            auto& q = queues_[id];
            while (q.size() > 1 &&
                   q.front().timestamp_ns < newest_front - tolerance_ns_) {
                q.pop_front();
                changed = true;
            }
            if (q.empty()) return std::nullopt;
        }
    }

    TimestampNs min_ts = std::numeric_limits<TimestampNs>::max();
    TimestampNs max_ts = std::numeric_limits<TimestampNs>::min();
    for (const auto& id : required_streams_) {
        const auto ts = queues_[id].front().timestamp_ns;
        min_ts = std::min(min_ts, ts);
        max_ts = std::max(max_ts, ts);
    }

    if (max_ts - min_ts > tolerance_ns_) return std::nullopt;

    SyncedFrameSet set;
    set.timestamp_ns = max_ts;
    set.max_skew_ns = max_ts - min_ts;
    set.frames.reserve(required_streams_.size());

    for (const auto& id : required_streams_) {
        auto& q = queues_[id];
        set.frames.push_back(std::move(q.front()));
        q.pop_front();
    }

    return set;
}

void FrameSynchronizer::clear() {
    queues_.clear();
}

} // namespace bdfrpc
