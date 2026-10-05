#include "bdfrpc/SolverAdapters.h"

#include <algorithm>
#include <utility>

namespace bdfrpc {

BufferedAdapterSource::BufferedAdapterSource(
    std::string id,
    std::vector<Domain> allowed_domains,
    std::size_t max_queue)
    : id_(std::move(id)),
      allowed_domains_(std::move(allowed_domains)),
      max_queue_(std::max<std::size_t>(1, max_queue)) {
    status_.id = id_;
}

const std::string& BufferedAdapterSource::id() const noexcept {
    return id_;
}

bool BufferedAdapterSource::start() {
    running_ = true;
    status_.state = AdapterState::Running;
    status_.message.clear();
    return true;
}

void BufferedAdapterSource::stop() noexcept {
    running_ = false;
    queue_.clear();
    status_.state = AdapterState::Stopped;
}

bool BufferedAdapterSource::running() const noexcept {
    return running_;
}

std::optional<CaptureFrame> BufferedAdapterSource::poll() {
    if (!running_ || queue_.empty()) return std::nullopt;
    auto frame = std::move(queue_.front());
    queue_.pop_front();
    return frame;
}

bool BufferedAdapterSource::domain_allowed(Domain domain) const noexcept {
    return std::find(
        allowed_domains_.begin(),
        allowed_domains_.end(),
        domain) != allowed_domains_.end();
}

bool BufferedAdapterSource::submit(CaptureFrame frame) {
    if (!running_ || frame.timestamp_ns <= 0 || frame.samples.empty()) {
        ++status_.rejected_frames;
        if (running_) {
            status_.state = AdapterState::Degraded;
            status_.message = "invalid or empty adapter frame";
        }
        return false;
    }

    frame.samples.erase(
        std::remove_if(frame.samples.begin(), frame.samples.end(),
            [&](const DomainSample& s) {
                return !domain_allowed(s.domain);
            }),
        frame.samples.end());

    if (frame.samples.empty()) {
        ++status_.rejected_frames;
        status_.state = AdapterState::Degraded;
        status_.message = "frame has no supported domains";
        return false;
    }

    frame.source_id = id_;
    if (frame.stream_id.empty()) frame.stream_id = id_;

    queue_.push_back(std::move(frame));
    while (queue_.size() > max_queue_) queue_.pop_front();

    ++status_.accepted_frames;
    status_.last_frame_ns = queue_.back().timestamp_ns;
    status_.state = AdapterState::Running;
    status_.message.clear();
    return true;
}

const AdapterStatus& BufferedAdapterSource::status() const noexcept {
    return status_;
}

BDFRFacialAdapter::BDFRFacialAdapter(std::string id)
    : BufferedAdapterSource(
        std::move(id),
        {Domain::Face, Domain::Head}) {}

EasyMocapAdapter::EasyMocapAdapter(std::string id)
    : BufferedAdapterSource(
        std::move(id),
        {Domain::Face, Domain::Head, Domain::Body,
         Domain::LeftHand, Domain::RightHand}) {}

void configure_default_routes(
    OperatingMode mode,
    SourceRouter& router,
    const std::string& facial,
    const std::string& body) {

    if (mode == OperatingMode::FaceOnly) {
        router.set_route(Domain::Face, {{facial, 0.50f, 100'000'000}});
        router.set_route(Domain::Head, {{facial, 0.50f, 100'000'000}});
        router.set_route(Domain::Body, {});
        router.set_route(Domain::LeftHand, {});
        router.set_route(Domain::RightHand, {});
        return;
    }

    if (mode == OperatingMode::BodyOnly) {
        router.set_route(Domain::Face, {{body, 0.35f, 100'000'000}});
        router.set_route(Domain::Head, {{body, 0.35f, 100'000'000}});
        router.set_route(Domain::Body, {{body, 0.50f, 100'000'000}});
        router.set_route(Domain::LeftHand, {{body, 0.45f, 100'000'000}});
        router.set_route(Domain::RightHand, {{body, 0.45f, 100'000'000}});
        return;
    }

    router.set_route(Domain::Face, {
        {facial, 0.50f, 100'000'000},
        {body, 0.35f, 100'000'000}
    });
    router.set_route(Domain::Head, {
        {facial, 0.50f, 100'000'000},
        {body, 0.35f, 100'000'000}
    });
    router.set_route(Domain::Body, {{body, 0.50f, 100'000'000}});
    router.set_route(Domain::LeftHand, {{body, 0.45f, 100'000'000}});
    router.set_route(Domain::RightHand, {{body, 0.45f, 100'000'000}});
}

} // namespace bdfrpc
