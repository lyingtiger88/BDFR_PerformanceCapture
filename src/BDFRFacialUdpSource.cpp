#include "bdfrpc/BDFRFacialUdpSource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using BDFRPCSocket = SOCKET;
constexpr BDFRPCSocket kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using BDFRPCSocket = int;
constexpr BDFRPCSocket kInvalidSocket = -1;
#endif

namespace bdfrpc {

namespace {

constexpr std::uint32_t kPacketMagic = 0x50464442U;
constexpr std::uint16_t kPacketVersion = 1;
constexpr std::uint32_t kFrameMagic = 0x52464442U;
constexpr std::uint16_t kFrameCodecVersion = 1;
constexpr std::size_t kMaxSourceLength = 1024;
constexpr std::size_t kMaxFrameBytes = 1024 * 1024;
constexpr std::size_t kMaxCurves = 4096;
constexpr std::size_t kMaxStringLength = 1024;

bool read_u16(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
              std::uint16_t& value) {
    if (offset + 2 > bytes.size()) return false;
    value = static_cast<std::uint16_t>(bytes[offset]) |
            static_cast<std::uint16_t>(bytes[offset + 1] << 8);
    offset += 2;
    return true;
}

bool read_u32(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
              std::uint32_t& value) {
    if (offset + 4 > bytes.size()) return false;
    value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[offset + i]) << (i * 8);
    }
    offset += 4;
    return true;
}

bool read_u64(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
              std::uint64_t& value) {
    if (offset + 8 > bytes.size()) return false;
    value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8);
    }
    offset += 8;
    return true;
}

bool read_float(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                float& value) {
    std::uint32_t bits = 0;
    if (!read_u32(bytes, offset, bits)) return false;
    std::memcpy(&value, &bits, sizeof(value));
    return std::isfinite(value);
}

bool read_double(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                 double& value) {
    std::uint64_t bits = 0;
    if (!read_u64(bytes, offset, bits)) return false;
    std::memcpy(&value, &bits, sizeof(value));
    return std::isfinite(value);
}

bool read_string(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                 std::string& value) {
    std::uint16_t length = 0;
    if (!read_u16(bytes, offset, length) ||
        length > kMaxStringLength ||
        offset + length > bytes.size()) {
        return false;
    }
    value.assign(
        reinterpret_cast<const char*>(bytes.data() + offset),
        static_cast<std::size_t>(length));
    offset += length;
    return true;
}

void close_socket(BDFRPCSocket socket) {
    if (socket == kInvalidSocket) return;
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

std::string socket_error() {
#ifdef _WIN32
    return "socket error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

} // namespace

bool BDFRFacialPacketCodec::decode(
    const std::vector<std::uint8_t>& bytes,
    const std::string& routed_source_id,
    CaptureFrame& out,
    std::uint64_t* sequence_out,
    std::string* remote_source_out) {

    std::size_t offset = 0;
    std::uint32_t packet_magic = 0;
    std::uint16_t packet_version = 0;
    std::uint64_t sequence = 0;
    std::uint16_t source_length = 0;
    std::uint32_t frame_length = 0;

    if (!read_u32(bytes, offset, packet_magic) ||
        packet_magic != kPacketMagic ||
        !read_u16(bytes, offset, packet_version) ||
        packet_version != kPacketVersion ||
        !read_u64(bytes, offset, sequence) ||
        !read_u16(bytes, offset, source_length) ||
        source_length > kMaxSourceLength ||
        offset + source_length > bytes.size()) {
        return false;
    }

    std::string remote_source(
        reinterpret_cast<const char*>(bytes.data() + offset),
        source_length);
    offset += source_length;

    if (!read_u32(bytes, offset, frame_length) ||
        frame_length > kMaxFrameBytes ||
        offset + frame_length != bytes.size()) {
        return false;
    }

    const std::size_t frame_end = offset + frame_length;
    std::uint32_t frame_magic = 0;
    std::uint16_t codec_version = 0;
    std::uint32_t schema_version = 0;
    double timestamp_seconds = 0.0;
    float confidence = 0.0F;
    float pitch = 0.0F;
    float yaw = 0.0F;
    float roll = 0.0F;
    float gaze_x = 0.0F;
    float gaze_y = 0.0F;
    float gaze_confidence = 0.0F;

    if (!read_u32(bytes, offset, frame_magic) ||
        frame_magic != kFrameMagic ||
        !read_u16(bytes, offset, codec_version) ||
        codec_version != kFrameCodecVersion ||
        !read_u32(bytes, offset, schema_version) ||
        !read_double(bytes, offset, timestamp_seconds) ||
        !read_float(bytes, offset, confidence) ||
        !read_float(bytes, offset, pitch) ||
        !read_float(bytes, offset, yaw) ||
        !read_float(bytes, offset, roll) ||
        !read_float(bytes, offset, gaze_x) ||
        !read_float(bytes, offset, gaze_y) ||
        !read_float(bytes, offset, gaze_confidence)) {
        return false;
    }

    std::uint32_t curve_count = 0;
    if (!read_u32(bytes, offset, curve_count) || curve_count > kMaxCurves) {
        return false;
    }

    DomainSample face;
    face.domain = Domain::Face;
    face.confidence = confidence;
    face.values.reserve(static_cast<std::size_t>(curve_count) + 3);
    face.channels.reserve(static_cast<std::size_t>(curve_count) + 3);

    for (std::uint32_t i = 0; i < curve_count; ++i) {
        std::string name;
        float value = 0.0F;
        if (!read_string(bytes, offset, name) ||
            !read_float(bytes, offset, value)) {
            return false;
        }
        face.channels.push_back(std::move(name));
        face.values.push_back(value);
    }

    face.channels.push_back("gazeX");
    face.values.push_back(gaze_x);
    face.channels.push_back("gazeY");
    face.values.push_back(gaze_y);
    face.channels.push_back("gazeConfidence");
    face.values.push_back(gaze_confidence);

    if (offset != frame_end) return false;

    DomainSample head;
    head.domain = Domain::Head;
    head.confidence = confidence;
    head.channels = {"pitch", "yaw", "roll"};
    head.values = {pitch, yaw, roll};

    if (timestamp_seconds < 0.0 ||
        timestamp_seconds >
            static_cast<double>(std::numeric_limits<TimestampNs>::max()) / 1.0e9) {
        return false;
    }

    CaptureFrame frame;
    frame.source_id =
        routed_source_id.empty() ? std::string("bdfr_facial") : routed_source_id;
    frame.stream_id =
        remote_source.empty() ? std::string("bdfr_facial_stream") : remote_source;
    frame.sequence = sequence;
    frame.timestamp_ns =
        static_cast<TimestampNs>(std::llround(timestamp_seconds * 1.0e9));
    frame.samples.push_back(std::move(face));
    frame.samples.push_back(std::move(head));

    out = std::move(frame);
    if (sequence_out) *sequence_out = sequence;
    if (remote_source_out) *remote_source_out = std::move(remote_source);
    (void)schema_version;
    return true;
}

class BDFRFacialUdpSource::Impl {
public:
    explicit Impl(BDFRFacialUdpConfig cfg) : config(std::move(cfg)) {}

    BDFRFacialUdpConfig config;
    BDFRPCSocket socket{kInvalidSocket};
    std::uint16_t local_port{0};
    BDFRFacialUdpStats stats;
    std::string last_error;
};

BDFRFacialUdpSource::BDFRFacialUdpSource(BDFRFacialUdpConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

BDFRFacialUdpSource::~BDFRFacialUdpSource() {
    stop();
}

const std::string& BDFRFacialUdpSource::id() const noexcept {
    return impl_->config.source_id;
}

bool BDFRFacialUdpSource::start() {
    if (running()) return true;
    impl_->last_error.clear();

    if (!socket_runtime_ready()) {
        impl_->last_error = "socket runtime initialization failed";
        return false;
    }

    impl_->socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->socket == kInvalidSocket) {
        impl_->last_error = socket_error();
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(impl_->config.port);

    if (impl_->config.bind_address.empty() ||
        impl_->config.bind_address == "0.0.0.0") {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(
                   AF_INET,
                   impl_->config.bind_address.c_str(),
                   &address.sin_addr) != 1) {
        impl_->last_error = "invalid IPv4 bind address";
        stop();
        return false;
    }

#ifdef _WIN32
    const int bind_length = static_cast<int>(sizeof(address));
#else
    const socklen_t bind_length = sizeof(address);
#endif

    if (::bind(
            impl_->socket,
            reinterpret_cast<const sockaddr*>(&address),
            bind_length) != 0) {
        impl_->last_error = socket_error();
        stop();
        return false;
    }

    sockaddr_in bound{};
#ifdef _WIN32
    int length = static_cast<int>(sizeof(bound));
#else
    socklen_t length = sizeof(bound);
#endif
    if (getsockname(
            impl_->socket,
            reinterpret_cast<sockaddr*>(&bound),
            &length) != 0) {
        impl_->last_error = socket_error();
        stop();
        return false;
    }

    impl_->local_port = ntohs(bound.sin_port);
    return true;
}

void BDFRFacialUdpSource::stop() noexcept {
    close_socket(impl_->socket);
    impl_->socket = kInvalidSocket;
    impl_->local_port = 0;
}

bool BDFRFacialUdpSource::running() const noexcept {
    return impl_->socket != kInvalidSocket;
}

std::optional<CaptureFrame> BDFRFacialUdpSource::poll() {
    if (!running()) return std::nullopt;

    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(impl_->socket, &read_set);
    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

#ifdef _WIN32
    const int ready = select(0, &read_set, nullptr, nullptr, &timeout);
#else
    const int ready = select(
        impl_->socket + 1, &read_set, nullptr, nullptr, &timeout);
#endif
    if (ready == 0) return std::nullopt;
    if (ready < 0) {
        ++impl_->stats.socket_errors;
        impl_->last_error = socket_error();
        return std::nullopt;
    }

    std::array<std::uint8_t, 65507> buffer{};
#ifdef _WIN32
    const int received = recvfrom(
        impl_->socket,
        reinterpret_cast<char*>(buffer.data()),
        static_cast<int>(buffer.size()),
        0, nullptr, nullptr);
#else
    const ssize_t received = recvfrom(
        impl_->socket, buffer.data(), buffer.size(), 0, nullptr, nullptr);
#endif

    if (received <= 0) {
        ++impl_->stats.socket_errors;
        impl_->last_error = socket_error();
        return std::nullopt;
    }

    std::vector<std::uint8_t> bytes(
        buffer.begin(),
        buffer.begin() + static_cast<std::ptrdiff_t>(received));

    CaptureFrame frame;
    std::uint64_t sequence = 0;
    std::string remote_source;
    if (!BDFRFacialPacketCodec::decode(
            bytes, impl_->config.source_id, frame, &sequence, &remote_source)) {
        ++impl_->stats.packets_invalid;
        impl_->last_error = "invalid BDFR FacialAnimation UDP packet";
        return std::nullopt;
    }

    ++impl_->stats.packets_received;
    impl_->stats.last_sequence = sequence;
    impl_->stats.last_remote_source = std::move(remote_source);
    impl_->last_error.clear();
    return frame;
}

std::uint16_t BDFRFacialUdpSource::local_port() const noexcept {
    return impl_->local_port;
}

const BDFRFacialUdpStats& BDFRFacialUdpSource::stats() const noexcept {
    return impl_->stats;
}

const std::string& BDFRFacialUdpSource::last_error() const noexcept {
    return impl_->last_error;
}

} // namespace bdfrpc
