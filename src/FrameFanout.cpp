#include "bdfrpc/FrameFanout.h"

#include <algorithm>
#include <utility>

namespace bdfrpc {

FrameFanout::FrameFanout(std::size_t default_queue_depth)
    : default_queue_depth_(
          std::max<std::size_t>(1, default_queue_depth)) {}

FrameSubscriberId FrameFanout::subscribe(
    std::string name,
    std::size_t queue_depth) {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto id = next_id_++;
    Subscriber subscriber;
    subscriber.name =
        name.empty()
            ? ("subscriber_" + std::to_string(id))
            : std::move(name);
    subscriber.max_depth =
        queue_depth == 0
            ? default_queue_depth_
            : std::max<std::size_t>(1, queue_depth);

    subscribers_.emplace(id, std::move(subscriber));
    return id;
}

bool FrameFanout::unsubscribe(FrameSubscriberId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscribers_.erase(id) > 0;
}

void FrameFanout::publish(const CaptureFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [id, subscriber] : subscribers_) {
        (void)id;
        subscriber.queue.push_back(frame);
        ++subscriber.received;

        while (subscriber.queue.size() > subscriber.max_depth) {
            subscriber.queue.pop_front();
            ++subscriber.dropped;
        }
    }
}

std::optional<CaptureFrame> FrameFanout::poll(
    FrameSubscriberId id) {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = subscribers_.find(id);
    if (it == subscribers_.end() || it->second.queue.empty()) {
        return std::nullopt;
    }

    CaptureFrame frame =
        std::move(it->second.queue.front());
    it->second.queue.pop_front();
    return frame;
}

std::optional<CaptureFrame> FrameFanout::latest(
    FrameSubscriberId id) {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = subscribers_.find(id);
    if (it == subscribers_.end() || it->second.queue.empty()) {
        return std::nullopt;
    }

    CaptureFrame frame =
        std::move(it->second.queue.back());

    if (it->second.queue.size() > 1) {
        it->second.dropped +=
            static_cast<std::uint64_t>(
                it->second.queue.size() - 1);
    }

    it->second.queue.clear();
    return frame;
}

std::optional<FrameSubscriberStats> FrameFanout::stats(
    FrameSubscriberId id) const {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = subscribers_.find(id);
    if (it == subscribers_.end()) return std::nullopt;

    return FrameSubscriberStats{
        it->second.received,
        it->second.dropped,
        it->second.queue.size()
    };
}

std::vector<std::pair<FrameSubscriberId, std::string>>
FrameFanout::subscribers() const {

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::pair<FrameSubscriberId, std::string>> out;
    out.reserve(subscribers_.size());

    for (const auto& [id, subscriber] : subscribers_) {
        out.emplace_back(id, subscriber.name);
    }

    std::sort(
        out.begin(),
        out.end(),
        [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

    return out;
}

void FrameFanout::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.clear();
}

} // namespace bdfrpc
