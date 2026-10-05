#include "bdfrpc/LatestFrameStore.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace bdfrpc {

LatestFrameStore::LatestFrameStore(TimestampNs retention_ns)
    : retention_ns_(std::max<TimestampNs>(1, retention_ns)) {}

bool LatestFrameStore::push(CaptureFrame frame) {
    if (frame.source_id.empty() || frame.timestamp_ns <= 0) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = latest_.find(frame.source_id);
    if (it != latest_.end()) {
        const auto& previous = it->second;
        if (frame.timestamp_ns < previous.timestamp_ns ||
            (frame.timestamp_ns == previous.timestamp_ns &&
             frame.sequence < previous.sequence)) {
            ++stats_.rejected_out_of_order;
            return false;
        }
    }

    latest_[frame.source_id] = std::move(frame);
    ++stats_.accepted_frames;
    return true;
}

SyncedFrameSet LatestFrameStore::snapshot(TimestampNs now_ns) const {
    std::lock_guard<std::mutex> lock(mutex_);

    SyncedFrameSet set;
    set.timestamp_ns = now_ns;

    TimestampNs min_ts = std::numeric_limits<TimestampNs>::max();
    TimestampNs max_ts = std::numeric_limits<TimestampNs>::min();

    for (const auto& [id, frame] : latest_) {
        (void)id;

        TimestampNs delta = now_ns - frame.timestamp_ns;
        if (delta < 0) delta = -delta;
        if (delta > retention_ns_) continue;

        set.frames.push_back(frame);
        min_ts = std::min(min_ts, frame.timestamp_ns);
        max_ts = std::max(max_ts, frame.timestamp_ns);
    }

    if (!set.frames.empty()) {
        set.max_skew_ns = max_ts - min_ts;
    }

    return set;
}

void LatestFrameStore::set_retention_ns(TimestampNs retention_ns) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    retention_ns_ = std::max<TimestampNs>(1, retention_ns);
}

void LatestFrameStore::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_.clear();
}

LatestFrameStoreStats LatestFrameStore::stats() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

} // namespace bdfrpc
