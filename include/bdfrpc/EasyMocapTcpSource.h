#pragma once

#include "bdfrpc/ICaptureSource.h"

#include <cstdint>
#include <memory>
#include <string>

namespace bdfrpc {

struct EasyMocapTcpConfig {
    std::string source_id{"easymocap"};
    std::string bind_address{"0.0.0.0"};
    std::uint16_t port{9999};
    std::size_t max_payload_bytes{4 * 1024 * 1024};
};

struct EasyMocapTcpStats {
    std::uint64_t connections{0};
    std::uint64_t disconnects{0};
    std::uint64_t frames_received{0};
    std::uint64_t invalid_payloads{0};
    std::uint64_t socket_errors{0};
};

class EasyMocapPayloadCodec {
public:
    // Decodes EasyMocap BaseSocketClient.send_smpl() JSON payloads.
    // Numeric arrays are flattened while preserving semantic channel names.
    static bool decode(
        const std::string& payload,
        const std::string& source_id,
        std::uint64_t sequence,
        TimestampNs timestamp_ns,
        CaptureFrame& frame);
};

class EasyMocapTcpSource final : public ICaptureSource {
public:
    explicit EasyMocapTcpSource(EasyMocapTcpConfig config = {});
    ~EasyMocapTcpSource() override;

    const std::string& id() const noexcept override;
    bool start() override;
    void stop() noexcept override;
    bool running() const noexcept override;
    std::optional<CaptureFrame> poll() override;

    std::uint16_t local_port() const noexcept;
    bool client_connected() const noexcept;
    const EasyMocapTcpStats& stats() const noexcept;
    const std::string& last_error() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bdfrpc
