#pragma once

#include "bdfrpc/ICaptureSource.h"
#include "bdfrpc/SourceRouter.h"

#include <deque>
#include <string>
#include <vector>

namespace bdfrpc {

enum class OperatingMode {
    FaceOnly,
    BodyOnly,
    Hybrid
};

enum class AdapterState {
    Stopped,
    Running,
    Degraded,
    Failed
};

struct AdapterStatus {
    std::string id;
    AdapterState state{AdapterState::Stopped};
    std::uint64_t accepted_frames{0};
    std::uint64_t rejected_frames{0};
    TimestampNs last_frame_ns{0};
    std::string message;
};

class BufferedAdapterSource : public ICaptureSource {
public:
    BufferedAdapterSource(
        std::string id,
        std::vector<Domain> allowed_domains,
        std::size_t max_queue = 8);

    const std::string& id() const noexcept override;
    bool start() override;
    void stop() noexcept override;
    bool running() const noexcept override;
    std::optional<CaptureFrame> poll() override;

    bool submit(CaptureFrame frame);
    const AdapterStatus& status() const noexcept;

protected:
    bool domain_allowed(Domain domain) const noexcept;

private:
    std::string id_;
    std::vector<Domain> allowed_domains_;
    std::size_t max_queue_;
    std::deque<CaptureFrame> queue_;
    AdapterStatus status_;
    bool running_{false};
};

class BDFRFacialAdapter final : public BufferedAdapterSource {
public:
    explicit BDFRFacialAdapter(std::string id = "bdfr_facial");
};

class EasyMocapAdapter final : public BufferedAdapterSource {
public:
    explicit EasyMocapAdapter(std::string id = "easymocap");
};

void configure_default_routes(
    OperatingMode mode,
    SourceRouter& router,
    const std::string& facial_source_id = "bdfr_facial",
    const std::string& body_source_id = "easymocap");

} // namespace bdfrpc
