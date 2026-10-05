#include "bdfrpc/EasyMocapTcpSource.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using EasySocket = SOCKET;
constexpr EasySocket kInvalidEasySocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using EasySocket = int;
constexpr EasySocket kInvalidEasySocket = -1;
#endif

namespace bdfrpc {

namespace {

TimestampNs monotonic_now_ns() {
    return static_cast<TimestampNs>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string key_token(const std::string& key) {
    return """ + key + """;
}

bool extract_value_span(
    const std::string& object,
    const std::string& key,
    std::string& span) {

    const auto key_pos = object.find(key_token(key));
    if (key_pos == std::string::npos) return false;

    const auto colon = object.find(':', key_pos);
    if (colon == std::string::npos) return false;

    const auto first = object.find_first_not_of(" \t\r\n", colon + 1);
    if (first == std::string::npos) return false;

    if (object[first] != '[') {
        auto end = object.find_first_of(",}", first);
        if (end == std::string::npos) end = object.size();
        span = object.substr(first, end - first);
        return true;
    }

    int depth = 0;
    bool in_string = false;
    bool escape = false;
    for (std::size_t i = first; i < object.size(); ++i) {
        const char ch = object[i];
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
        } else if (ch == '[') {
            ++depth;
        } else if (ch == ']') {
            --depth;
            if (depth == 0) {
                span = object.substr(first, i - first + 1);
                return true;
            }
        }
    }
    return false;
}

std::vector<double> parse_numbers(const std::string& span) {
    std::vector<double> values;
    const char* p = span.c_str();
    const char* end = p + span.size();

    while (p < end) {
        while (p < end &&
               !((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.')) {
            ++p;
        }
        if (p >= end) break;

        char* parsed_end = nullptr;
        const double value = std::strtod(p, &parsed_end);
        if (parsed_end == p) {
            ++p;
            continue;
        }
        if (std::isfinite(value)) values.push_back(value);
        p = parsed_end;
    }
    return values;
}

bool parse_subject_id(const std::string& object, int& id) {
    std::string span;
    if (!extract_value_span(object, "id", span)) return false;
    try {
        std::size_t used = 0;
        id = std::stoi(span, &used);
        return used > 0;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> split_subject_objects(const std::string& payload) {
    std::vector<std::string> objects;
    int brace_depth = 0;
    bool in_string = false;
    bool escape = false;
    std::size_t start = std::string::npos;

    for (std::size_t i = 0; i < payload.size(); ++i) {
        const char ch = payload[i];
        if (in_string) {
            if (escape) escape = false;
            else if (ch == '\\') escape = true;
            else if (ch == '"') in_string = false;
            continue;
        }

        if (ch == '"') {
            in_string = true;
        } else if (ch == '{') {
            if (brace_depth == 0) start = i;
            ++brace_depth;
        } else if (ch == '}') {
            if (brace_depth <= 0) return {};
            --brace_depth;
            if (brace_depth == 0 && start != std::string::npos) {
                objects.push_back(payload.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }

    if (brace_depth != 0 || objects.empty()) return {};
    return objects;
}

void append_channels(
    DomainSample& sample,
    int subject_id,
    const std::string& group,
    const std::vector<double>& values) {

    const std::string prefix =
        "subject." + std::to_string(subject_id) + "." + group + ".";
    for (std::size_t i = 0; i < values.size(); ++i) {
        sample.channels.push_back(prefix + std::to_string(i));
        sample.values.push_back(static_cast<float>(values[i]));
    }
}

void close_easy_socket(EasySocket socket) {
    if (socket == kInvalidEasySocket) return;
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}

#ifdef _WIN32
class EasyWinsockRuntime {
public:
    EasyWinsockRuntime() {
        WSADATA data{};
        ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~EasyWinsockRuntime() {
        if (ok_) WSACleanup();
    }
    bool ok() const noexcept { return ok_; }
private:
    bool ok_{false};
};

EasyWinsockRuntime& easy_winsock() {
    static EasyWinsockRuntime runtime;
    return runtime;
}
#endif

bool socket_runtime_ready() {
#ifdef _WIN32
    return easy_winsock().ok();
#else
    return true;
#endif
}

bool set_non_blocking(EasySocket socket) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool would_block() {
#ifdef _WIN32
    const int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN;
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

bool EasyMocapPayloadCodec::decode(
    const std::string& payload,
    const std::string& source_id,
    std::uint64_t sequence,
    TimestampNs timestamp_ns,
    CaptureFrame& frame) {

    if (payload.empty() || timestamp_ns <= 0) return false;
    const auto objects = split_subject_objects(payload);
    if (objects.empty()) return false;

    DomainSample body;
    body.domain = Domain::Body;
    body.confidence = 1.0F;

    DomainSample face;
    face.domain = Domain::Face;
    face.confidence = 1.0F;

    for (const auto& object : objects) {
        int subject_id = 0;
        if (!parse_subject_id(object, subject_id)) return false;

        for (const auto& group : {"Rh", "Th", "poses", "shapes"}) {
            std::string span;
            if (!extract_value_span(object, group, span)) continue;
            append_channels(body, subject_id, group, parse_numbers(span));
        }

        std::string expression_span;
        if (extract_value_span(object, "expression", expression_span)) {
            append_channels(
                face, subject_id, "expression", parse_numbers(expression_span));
        }
    }

    if (body.values.empty() && face.values.empty()) return false;

    CaptureFrame out;
    out.source_id = source_id.empty() ? "easymocap" : source_id;
    out.stream_id = "easymocap_tcp";
    out.sequence = sequence;
    out.timestamp_ns = timestamp_ns;

    if (!body.values.empty()) out.samples.push_back(std::move(body));
    if (!face.values.empty()) out.samples.push_back(std::move(face));

    frame = std::move(out);
    return true;
}

class EasyMocapTcpSource::Impl {
public:
    explicit Impl(EasyMocapTcpConfig cfg) : config(std::move(cfg)) {}

    EasyMocapTcpConfig config;
    EasySocket server{kInvalidEasySocket};
    EasySocket client{kInvalidEasySocket};
    std::uint16_t local_port{0};
    std::uint64_t sequence{0};
    std::string receive_buffer;
    std::size_t expected_payload{0};
    EasyMocapTcpStats stats;
    std::string last_error;

    void disconnect_client() {
        if (client != kInvalidEasySocket) {
            close_easy_socket(client);
            client = kInvalidEasySocket;
            ++stats.disconnects;
        }
        receive_buffer.clear();
        expected_payload = 0;
    }
};

EasyMocapTcpSource::EasyMocapTcpSource(EasyMocapTcpConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

EasyMocapTcpSource::~EasyMocapTcpSource() {
    stop();
}

const std::string& EasyMocapTcpSource::id() const noexcept {
    return impl_->config.source_id;
}

bool EasyMocapTcpSource::start() {
    if (running()) return true;
    impl_->last_error.clear();

    if (!socket_runtime_ready()) {
        impl_->last_error = "socket runtime initialization failed";
        return false;
    }

    impl_->server = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl_->server == kInvalidEasySocket) {
        impl_->last_error = socket_error();
        return false;
    }

    int reuse = 1;
#ifdef _WIN32
    setsockopt(impl_->server, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    setsockopt(impl_->server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

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
            impl_->server,
            reinterpret_cast<const sockaddr*>(&address),
            bind_length) != 0 ||
        ::listen(impl_->server, 1) != 0 ||
        !set_non_blocking(impl_->server)) {
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
            impl_->server,
            reinterpret_cast<sockaddr*>(&bound),
            &length) != 0) {
        impl_->last_error = socket_error();
        stop();
        return false;
    }

    impl_->local_port = ntohs(bound.sin_port);
    return true;
}

void EasyMocapTcpSource::stop() noexcept {
    impl_->disconnect_client();
    close_easy_socket(impl_->server);
    impl_->server = kInvalidEasySocket;
    impl_->local_port = 0;
}

bool EasyMocapTcpSource::running() const noexcept {
    return impl_->server != kInvalidEasySocket;
}

std::optional<CaptureFrame> EasyMocapTcpSource::poll() {
    if (!running()) return std::nullopt;

    if (impl_->client == kInvalidEasySocket) {
        sockaddr_in peer{};
#ifdef _WIN32
        int peer_length = static_cast<int>(sizeof(peer));
#else
        socklen_t peer_length = sizeof(peer);
#endif
        const auto client = ::accept(
            impl_->server,
            reinterpret_cast<sockaddr*>(&peer),
            &peer_length);

        if (client != kInvalidEasySocket) {
            if (!set_non_blocking(client)) {
                close_easy_socket(client);
                ++impl_->stats.socket_errors;
                impl_->last_error = "failed to set EasyMocap client non-blocking";
                return std::nullopt;
            }
            impl_->client = client;
            ++impl_->stats.connections;
            impl_->last_error.clear();
        } else if (!would_block()) {
            ++impl_->stats.socket_errors;
            impl_->last_error = socket_error();
        }
        return std::nullopt;
    }

    std::array<char, 64 * 1024> buffer{};
#ifdef _WIN32
    const int received = recv(
        impl_->client, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
    const ssize_t received = recv(
        impl_->client, buffer.data(), buffer.size(), 0);
#endif

    if (received == 0) {
        impl_->disconnect_client();
        return std::nullopt;
    }
    if (received < 0) {
        if (!would_block()) {
            ++impl_->stats.socket_errors;
            impl_->last_error = socket_error();
            impl_->disconnect_client();
        }
    } else {
        impl_->receive_buffer.append(
            buffer.data(), static_cast<std::size_t>(received));
    }

    if (impl_->expected_payload == 0) {
        const auto newline = impl_->receive_buffer.find('\n');
        if (newline == std::string::npos) {
            if (impl_->receive_buffer.size() > 32) {
                ++impl_->stats.invalid_payloads;
                impl_->last_error = "invalid EasyMocap length prefix";
                impl_->disconnect_client();
            }
            return std::nullopt;
        }

        try {
            std::size_t used = 0;
            const auto length = std::stoull(
                impl_->receive_buffer.substr(0, newline), &used);
            if (used != newline ||
                length == 0 ||
                length > impl_->config.max_payload_bytes) {
                throw std::out_of_range("payload");
            }
            impl_->expected_payload = static_cast<std::size_t>(length);
        } catch (...) {
            ++impl_->stats.invalid_payloads;
            impl_->last_error = "invalid EasyMocap payload length";
            impl_->disconnect_client();
            return std::nullopt;
        }

        impl_->receive_buffer.erase(0, newline + 1);
    }

    if (impl_->receive_buffer.size() < impl_->expected_payload) {
        return std::nullopt;
    }

    const std::string payload =
        impl_->receive_buffer.substr(0, impl_->expected_payload);
    impl_->receive_buffer.erase(0, impl_->expected_payload);
    impl_->expected_payload = 0;

    CaptureFrame frame;
    if (!EasyMocapPayloadCodec::decode(
            payload,
            impl_->config.source_id,
            ++impl_->sequence,
            monotonic_now_ns(),
            frame)) {
        ++impl_->stats.invalid_payloads;
        impl_->last_error = "invalid EasyMocap SMPL payload";
        return std::nullopt;
    }

    ++impl_->stats.frames_received;
    impl_->last_error.clear();
    return frame;
}

std::uint16_t EasyMocapTcpSource::local_port() const noexcept {
    return impl_->local_port;
}

bool EasyMocapTcpSource::client_connected() const noexcept {
    return impl_->client != kInvalidEasySocket;
}

const EasyMocapTcpStats& EasyMocapTcpSource::stats() const noexcept {
    return impl_->stats;
}

const std::string& EasyMocapTcpSource::last_error() const noexcept {
    return impl_->last_error;
}

} // namespace bdfrpc
