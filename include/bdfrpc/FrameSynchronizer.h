#pragma once

#include "bdfrpc/Types.h"
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bdfrpc {

class FrameSynchronizer {
public:
    explicit FrameSynchronizer(TimestampNs tolerance_ns = 8'000'000,
                               std::size_t max_queue_per_stream = 8);

    void set_required_streams(std::vector<std::string> stream_ids);
    void push(CaptureFrame frame);
    std::optional<SyncedFrameSet> try_pop();
    void clear();

    TimestampNs tolerance_ns() const noexcept { return tolerance_ns_; }

private:
    TimestampNs tolerance_ns_;
    std::size_t max_queue_per_stream_;
    std::vector<std::string> required_streams_;
    std::unordered_map<std::string, std::deque<CaptureFrame>> queues_;
};

} // namespace bdfrpc
