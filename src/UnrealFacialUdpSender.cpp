#include "bdfrpc/UnrealFacialUdpSender.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using UnrealSocket = SOCKET;
constexpr UnrealSocket kInvalidUnrealSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using UnrealSocket = int;
constexpr UnrealSocket kInvalidUnrealSocket = -1;
#endif

namespace bdfrpc {
namespace {

constexpr std::uint32_t kPacketMagic = 0x50464442U; // BDFP
constexpr std::uint16_t kPacketVersion = 1;
constexpr std::uint32_t kFrameMagic = 0x52464442U; // BDFR
constexpr std::uint16_t kFrameVersion = 1;
constexpr std::size_t kMaxCurves = 4096;
constexpr std::size_t kMaxNameBytes = 1024;
constexpr std::size_t kMaxSourceBytes = 1024;

void append_u16(
    std::vector<std::uint8_t>& out,
    std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
}

void append_u32(
    std::vector<std::uint8_t>& out,
    std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(
            static_cast<std::uint8_t>(
                (value >> (i * 8)) & 0xffu));
    }
}

void append_u64(
    std::vector<std::uint8_t>& out,
    std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(
            static_cast<std::uint8_t>(
                (value >> (i * 8)) & 0xffu));
    }
}

void append_float(
    std::vector<std::uint8_t>& out,
    float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(out, bits);
}

void append_double(
    std::vector<std::uint8_t>& out,
    double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    append_u64(out, bits);
}

bool append_utf8_u16(
    std::vector<std::uint8_t>& out,
    const std::string& value,
    std::size_t max_bytes) {
    if (value.empty() ||
        value.size() > max_bytes ||
        value.size() >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint16_t>::max())) {
        return false;
    }

    append_u16(
        out,
        static_cast<std::uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    return true;
}

const DomainSample* find_sample(
    const FusedPerformanceFrame& frame,
    Domain domain) {
    for (const auto& sample : frame.samples) {
        if (sample.domain == domain) return &sample;
    }
    return nullptr;
}

float named_value(
    const DomainSample* sample,
    const char* name,
    float fallback = 0.0F) {
    if (!sample) return fallback;

    const auto count =
        std::min(sample->channels.size(), sample->values.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (sample->channels[i] == name) {
            return sample->values[i];
        }
    }
    return fallback;
}

bool is_transport_metadata_curve(const std::string& name) {
    return name == "gazeX" ||
           name == "gazeY" ||
           name == "gazeConfidence";
}

void close_unreal_socket(UnrealSocket socket) {
    if (socket == kInvalidUnrealSocket) return;
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}

#ifdef _WIN32
class WinsockRuntime {
public:
    WinsockRuntime() {
        WSADATA data{};
        ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockRuntime() {
        if (ok_) WSACleanup();
    }
    bool ok() const noexcept { return ok_; }
private:
    bool ok_{false};
};

WinsockRuntime& winsock_runtime() {
    static WinsockRuntime runtime;
    return runtime;
}
#endif

bool socket_runtime_ready() {
#ifdef _WIN32
    return winsock_runtime().ok();
#else
    return true;
#endif
}

bool set_non_blocking(UnrealSocket socket) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 &&
           fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

std::string socket_error() {
#ifdef _WIN32
    return "socket error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

} // namespace

std::vector<std::uint8_t> encode_unreal_facial_frame(
    const FusedPerformanceFrame& fused,
    std::uint32_t schema_version) {

    const auto* face = find_sample(fused, Domain::Face);
    if (!face || fused.timestamp_ns <= 0) {
        return {};
    }

    const auto* head = find_sample(fused, Domain::Head);

    struct Curve {
        std::string name;
        float value{0.0F};
    };
    std::vector<Curve> curves;

    const auto count =
        std::min(face->channels.size(), face->values.size());
    curves.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const auto& name = face->channels[i];
        if (name.empty() ||
            name.size() > kMaxNameBytes ||
            is_transport_metadata_curve(name)) {
            continue;
        }
        curves.push_back({name, face->values[i]});
    }

    if (curves.size() > kMaxCurves) return {};

    std::vector<std::uint8_t> inner;
    inner.reserve(64 + curves.size() * 24);

    append_u32(inner, kFrameMagic);
    append_u16(inner, kFrameVersion);
    append_u32(inner, schema_version);
    append_double(
        inner,
        static_cast<double>(fused.timestamp_ns) / 1.0e9);
    append_float(inner, face->confidence);

    append_float(inner, named_value(head, "pitch"));
    append_float(inner, named_value(head, "yaw"));
    append_float(inner, named_value(head, "roll"));

    append_float(inner, named_value(face, "gazeX"));
    append_float(inner, named_value(face, "gazeY"));
    append_float(inner, named_value(face, "gazeConfidence"));

    append_u32(
        inner,
        static_cast<std::uint32_t>(curves.size()));

    for (const auto& curve : curves) {
        if (!append_utf8_u16(
                inner,
                curve.name,
                kMaxNameBytes)) {
            return {};
        }
        append_float(inner, curve.value);
    }

    return inner;
}

std::vector<std::uint8_t> encode_unreal_facial_packet(
    const FusedPerformanceFrame& fused,
    const UnrealFacialPacketOptions& options) {

    if (options.source_id.empty() ||
        options.source_id.size() > kMaxSourceBytes) {
        return {};
    }

    const auto inner =
        encode_unreal_facial_frame(
            fused,
            options.schema_version);
    if (inner.empty() ||
        inner.size() >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max())) {
        return {};
    }

    std::vector<std::uint8_t> packet;
    packet.reserve(
        4 + 2 + 8 + 2 +
        options.source_id.size() +
        4 + inner.size());

    append_u32(packet, kPacketMagic);
    append_u16(packet, kPacketVersion);
    append_u64(packet, options.sequence);

    if (!append_utf8_u16(
            packet,
            options.source_id,
            kMaxSourceBytes)) {
        return {};
    }

    append_u32(
        packet,
        static_cast<std::uint32_t>(inner.size()));
    packet.insert(
        packet.end(),
        inner.begin(),
        inner.end());

    return packet;
}

class UnrealFacialUdpSender::Impl {
public:
    explicit Impl(UnrealFacialUdpConfig cfg)
        : config(std::move(cfg)) {}

    UnrealFacialUdpConfig config;
    UnrealSocket socket{kInvalidUnrealSocket};
    sockaddr_in destination{};
    UnrealFacialUdpStats stats;
    std::string last_error;
};

UnrealFacialUdpSender::UnrealFacialUdpSender(
    UnrealFacialUdpConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

UnrealFacialUdpSender::~UnrealFacialUdpSender() {
    stop();
}

bool UnrealFacialUdpSender::start() {
    if (running()) return true;
    impl_->last_error.clear();

    if (!socket_runtime_ready()) {
        impl_->last_error =
            "socket runtime initialization failed";
        return false;
    }

    if (impl_->config.port == 0 ||
        impl_->config.host.empty()) {
        impl_->last_error =
            "invalid Unreal UDP destination";
        return false;
    }

    impl_->socket =
        ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->socket == kInvalidUnrealSocket) {
        impl_->last_error = socket_error();
        return false;
    }

    if (!set_non_blocking(impl_->socket)) {
        impl_->last_error = socket_error();
        stop();
        return false;
    }

    impl_->destination = {};
    impl_->destination.sin_family = AF_INET;
    impl_->destination.sin_port =
        htons(impl_->config.port);

    if (inet_pton(
            AF_INET,
            impl_->config.host.c_str(),
            &impl_->destination.sin_addr) != 1) {
        impl_->last_error =
            "Unreal destination must be an IPv4 address";
        stop();
        return false;
    }

    return true;
}

void UnrealFacialUdpSender::stop() noexcept {
    close_unreal_socket(impl_->socket);
    impl_->socket = kInvalidUnrealSocket;
}

bool UnrealFacialUdpSender::running() const noexcept {
    return impl_->socket != kInvalidUnrealSocket;
}

bool UnrealFacialUdpSender::send(
    const FusedPerformanceFrame& frame) {

    if (!running()) {
        impl_->last_error = "sender is not running";
        return false;
    }

    const auto sequence = impl_->stats.sequence + 1;
    const auto packet =
        encode_unreal_facial_packet(
            frame,
            {
                impl_->config.source_id,
                sequence,
                1
            });

    if (packet.empty()) {
        ++impl_->stats.encode_failures;
        impl_->last_error =
            "fused frame has no encodable face payload";
        return false;
    }

#ifdef _WIN32
    const int sent = sendto(
        impl_->socket,
        reinterpret_cast<const char*>(packet.data()),
        static_cast<int>(packet.size()),
        0,
        reinterpret_cast<const sockaddr*>(
            &impl_->destination),
        static_cast<int>(sizeof(impl_->destination)));
#else
    const ssize_t sent = sendto(
        impl_->socket,
        packet.data(),
        packet.size(),
        0,
        reinterpret_cast<const sockaddr*>(
            &impl_->destination),
        sizeof(impl_->destination));
#endif

    if (sent < 0 ||
        static_cast<std::size_t>(sent) != packet.size()) {
        ++impl_->stats.socket_errors;
        impl_->last_error = socket_error();
        return false;
    }

    impl_->stats.sequence = sequence;
    ++impl_->stats.frames_sent;
    impl_->stats.bytes_sent +=
        static_cast<std::uint64_t>(packet.size());
    impl_->last_error.clear();
    return true;
}

const UnrealFacialUdpStats&
UnrealFacialUdpSender::stats() const noexcept {
    return impl_->stats;
}

const std::string&
UnrealFacialUdpSender::last_error() const noexcept {
    return impl_->last_error;
}

} // namespace bdfrpc
