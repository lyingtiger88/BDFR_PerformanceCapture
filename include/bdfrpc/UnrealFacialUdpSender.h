#pragma once

#include "bdfrpc/Types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bdfrpc {

struct UnrealFacialPacketOptions {
    std::string source_id{"bdfr_performance"};
    std::uint64_t sequence{0};
    std::uint32_t schema_version{1};
};

// Encodes the canonical inner BDFR facial frame v1. This is shared by the
// legacy BDFP v1 packet and the BDFP v2 performance packet.
std::vector<std::uint8_t> encode_unreal_facial_frame(
    const FusedPerformanceFrame& frame,
    std::uint32_t schema_version = 1);

// Encodes the existing BDFR FacialAnimation BDFP v1 wire format used by the
// Unreal plugin's UBDFRLiveReceiverComponent. Face/Head are taken from the
// fused frame, preserving the current Unreal/LiveLink compatibility path.
std::vector<std::uint8_t> encode_unreal_facial_packet(
    const FusedPerformanceFrame& frame,
    const UnrealFacialPacketOptions& options = {});

struct UnrealFacialUdpConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port{5000};
    std::string source_id{"bdfr_performance"};
};

struct UnrealFacialUdpStats {
    std::uint64_t frames_sent{0};
    std::uint64_t bytes_sent{0};
    std::uint64_t encode_failures{0};
    std::uint64_t socket_errors{0};
    std::uint64_t sequence{0};
};

class UnrealFacialUdpSender {
public:
    explicit UnrealFacialUdpSender(
        UnrealFacialUdpConfig config = {});
    ~UnrealFacialUdpSender();

    bool start();
    void stop() noexcept;
    bool running() const noexcept;

    bool send(const FusedPerformanceFrame& frame);

    const UnrealFacialUdpStats& stats() const noexcept;
    const std::string& last_error() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bdfrpc
