#include "bdfrpc/FrameSynchronizer.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace bdfrpc {

FrameSynchronizer::FrameSynchronizer(TimestampNs tolerance_ns,
                                     std::size_t max_queue_per_stream)
    : tolerance_ns_(std::max<TimestampNs>(0, tolerance_ns)),
      max_queue_per_stream_(std::max<std::size_t>(1, max_queue_per_stream)) {}

void FrameSynchronizer::set_required_streams(std::vector<std::string> stream_ids) {
    required_streams_ = std::move(stream_ids);
}

void FrameSynchronizer::set_tolerance_ns(TimestampNs tolerance_ns) noexcept {
    tolerance_ns_ = std::max<TimestampNs>(0, tolerance_ns);
}

void FrameSynchronizer::push(CaptureFrame frame) {
    auto& stream_stats = stats_.streams[frame.stream_id];
    ++stream_stats.received_frames;
    stream_stats.last_timestamp_ns = frame.timestamp_ns;

    auto& q = queues_[frame.stream_id];
    q.push_back(std::move(frame));
    while (q.size() > max_queue_per_stream_) {
        q.pop_front();
        ++stream_stats.dropped_overflow;
    }
}

std::optional<SyncedFrameSet> FrameSynchronizer::try_pop() {
    if (required_streams_.empty()) return std::nullopt;

    for (const auto& id : required_streams_) {
        auto it = queues_.find(id);
        if (it == queues_.end() || it->second.empty()) return std::nullopt;
    }

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
                ++stats_.streams[id].dropped_stale;
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
        auto& stream_stats = stats_.streams[id];
        stream_stats.last_offset_ns = q.front().timestamp_ns - max_ts;
        ++stream_stats.emitted_frames;
        set.frames.push_back(std::move(q.front()));
        q.pop_front();
    }

    ++stats_.emitted_sets;
    stats_.last_set_skew_ns = set.max_skew_ns;
    stats_.max_set_skew_ns = std::max(stats_.max_set_skew_ns, set.max_skew_ns);
    return set;
}

void FrameSynchronizer::clear() {
    queues_.clear();
}

void FrameSynchronizer::reset_stats() noexcept {
    stats_ = {};
}

} // namespace bdfrpc
