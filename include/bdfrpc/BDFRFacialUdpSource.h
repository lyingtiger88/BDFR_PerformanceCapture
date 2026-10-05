#pragma once

#include "bdfrpc/ICaptureSource.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bdfrpc {

struct BDFRFacialUdpConfig {
    std::string source_id{"bdfr_facial"};
    std::string bind_address{"0.0.0.0"};
    std::uint16_t port{0};
    bool rebase_remote_clock{true};
};

struct BDFRFacialUdpStats {
    std::uint64_t packets_received{0};
    std::uint64_t packets_invalid{0};
    std::uint64_t socket_errors{0};
    std::uint64_t last_sequence{0};
    std::string last_remote_source;
    TimestampNs clock_offset_ns{0};
    TimestampNs clock_jitter_ns{0};
    std::uint64_t clock_samples_rejected{0};
};

class BDFRFacialPacketCodec {
public:
    static bool decode(
        const std::vector<std::uint8_t>& bytes,
        const std::string& routed_source_id,
        CaptureFrame& frame,
        std::uint64_t* sequence = nullptr,
        std::string* remote_source = nullptr);
};

class BDFRFacialUdpSource final : public ICaptureSource {
public:
    explicit BDFRFacialUdpSource(BDFRFacialUdpConfig config = {});
    ~BDFRFacialUdpSource() override;

    const std::string& id() const noexcept override;
    bool start() override;
    void stop() noexcept override;
    bool running() const noexcept override;
    std::optional<CaptureFrame> poll() override;

    std::uint16_t local_port() const noexcept;
    const BDFRFacialUdpStats& stats() const noexcept;
    const std::string& last_error() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bdfrpc
