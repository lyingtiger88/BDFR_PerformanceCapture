#pragma once

#include "bdfrpc/Types.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bdfrpc {

using FrameSubscriberId = std::uint64_t;

struct FrameSubscriberStats {
    std::uint64_t received{0};
    std::uint64_t dropped{0};
    std::size_t queued{0};
};

class FrameFanout {
public:
    explicit FrameFanout(std::size_t default_queue_depth = 4);

    FrameSubscriberId subscribe(
        std::string name,
        std::size_t queue_depth = 0);

    bool unsubscribe(FrameSubscriberId id);

    // Capture thread calls publish once. Image bytes remain shared through
    // ImageBuffer::shared_ptr, so subscribers do not duplicate pixel buffers.
    void publish(const CaptureFrame& frame);

    std::optional<CaptureFrame> poll(FrameSubscriberId id);
    std::optional<CaptureFrame> latest(FrameSubscriberId id);

    std::optional<FrameSubscriberStats> stats(
        FrameSubscriberId id) const;

    std::vector<std::pair<FrameSubscriberId, std::string>>
    subscribers() const;

    void clear();

private:
    struct Subscriber {
        std::string name;
        std::size_t max_depth{1};
        std::deque<CaptureFrame> queue;
        std::uint64_t received{0};
        std::uint64_t dropped{0};
    };

    std::size_t default_queue_depth_;
    mutable std::mutex mutex_;
    FrameSubscriberId next_id_{1};
    std::unordered_map<FrameSubscriberId, Subscriber> subscribers_;
};

} // namespace bdfrpc
