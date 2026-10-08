#include "bdfrpc/BinaryTake.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace bdfrpc {
namespace {

constexpr std::array<char,8> kMagic{
    'B','D','F','R','T','A','K','E'
};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kEndianMarker = 0x01020304U;
constexpr std::uint32_t kFrameMarker = 0x4D415246U;
constexpr std::uint32_t kMaxSamples = 64;
constexpr std::uint32_t kMaxChannels = 1'000'000;
constexpr std::uint32_t kMaxStringBytes = 64 * 1024;

template <typename T>
bool write_raw(std::ofstream& out, const T& value) {
    out.write(
        reinterpret_cast<const char*>(&value),
        static_cast<std::streamsize>(sizeof(T)));
    return static_cast<bool>(out);
}

bool write_string(std::ofstream& out, const std::string& value) {
    if (value.size() > kMaxStringBytes) return false;
    const auto size = static_cast<std::uint32_t>(value.size());
    if (!write_raw(out, size)) return false;
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    return static_cast<bool>(out);
}

template <typename T>
bool read_raw(std::ifstream& in, T& value) {
    in.read(
        reinterpret_cast<char*>(&value),
        static_cast<std::streamsize>(sizeof(T)));
    return static_cast<bool>(in);
}

bool read_string(std::ifstream& in, std::string& value) {
    std::uint32_t size = 0;
    if (!read_raw(in, size) || size > kMaxStringBytes) return false;
    value.resize(size);
    if (size > 0) {
        in.read(value.data(), static_cast<std::streamsize>(size));
    }
    return static_cast<bool>(in);
}

std::uint8_t encode_domain(Domain domain) {
    return static_cast<std::uint8_t>(domain);
}

bool decode_domain(std::uint8_t raw, Domain& domain) {
    if (raw > static_cast<std::uint8_t>(Domain::RightHand)) return false;
    domain = static_cast<Domain>(raw);
    return true;
}

} // namespace

BinaryTakeWriter::~BinaryTakeWriter() {
    stop();
}

bool BinaryTakeWriter::start(const std::string& path) {
    stop();
    last_error_.clear();

    if (path.empty()) {
        last_error_ = "empty take path";
        return false;
    }

    stream_.open(
        path,
        std::ios::out | std::ios::binary | std::ios::trunc);
    if (!stream_) {
        last_error_ = "unable to create binary take";
        return false;
    }

    stream_.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    if (!write_raw(stream_, kVersion) ||
        !write_raw(stream_, kEndianMarker)) {
        last_error_ = "failed to write take header";
        stop();
        return false;
    }

    path_ = path;
    frames_written_ = 0;
    return true;
}

bool BinaryTakeWriter::append(const FusedPerformanceFrame& frame) {
    if (!stream_) {
        last_error_ = "take is not open";
        return false;
    }
    if (frame.samples.size() > kMaxSamples) {
        last_error_ = "too many samples in frame";
        return false;
    }

    if (!write_raw(stream_, kFrameMarker) ||
        !write_raw(stream_, frame.timestamp_ns)) {
        last_error_ = "failed to write frame header";
        return false;
    }

    const auto sample_count =
        static_cast<std::uint32_t>(frame.samples.size());
    if (!write_raw(stream_, sample_count)) {
        last_error_ = "failed to write sample count";
        return false;
    }

    for (std::size_t i = 0; i < frame.samples.size(); ++i) {
        const auto& sample = frame.samples[i];
        if (sample.values.size() > kMaxChannels ||
            (!sample.channels.empty() &&
             sample.channels.size() != sample.values.size())) {
            last_error_ = "invalid channel/value layout";
            return false;
        }

        const std::uint8_t domain = encode_domain(sample.domain);
        if (!write_raw(stream_, domain) ||
            !write_raw(stream_, sample.confidence)) {
            last_error_ = "failed to write sample header";
            return false;
        }

        const std::string source =
            i < frame.selected_sources.size()
                ? frame.selected_sources[i]
                : std::string{};
        if (!write_string(stream_, source)) {
            last_error_ = "failed to write source id";
            return false;
        }

        const auto channel_count =
            static_cast<std::uint32_t>(sample.values.size());
        if (!write_raw(stream_, channel_count)) {
            last_error_ = "failed to write channel count";
            return false;
        }

        for (std::size_t c = 0; c < sample.values.size(); ++c) {
            const std::string channel =
                sample.channels.empty()
                    ? ("value." + std::to_string(c))
                    : sample.channels[c];

            if (!write_string(stream_, channel) ||
                !write_raw(stream_, sample.values[c])) {
                last_error_ = "failed to write channel";
                return false;
            }
        }
    }

    if (!stream_) {
        last_error_ = "binary take write failed";
        return false;
    }

    ++frames_written_;
    return true;
}

bool BinaryTakeWriter::flush() {
    if (!stream_) return false;
    stream_.flush();
    if (!stream_) {
        last_error_ = "binary take flush failed";
        return false;
    }
    return true;
}

void BinaryTakeWriter::stop() {
    if (stream_.is_open()) {
        stream_.flush();
        stream_.close();
    }
}

bool BinaryTakeReader::load(
    const std::string& path,
    bool recover_truncated_tail) {
    clear();

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        last_error_ = "unable to open binary take";
        return false;
    }

    std::array<char,8> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));

    std::uint32_t version = 0;
    std::uint32_t endian = 0;
    if (!input ||
        magic != kMagic ||
        !read_raw(input, version) ||
        !read_raw(input, endian)) {
        last_error_ = "invalid binary take header";
        return false;
    }

    if (version != kVersion) {
        last_error_ = "unsupported binary take version";
        return false;
    }
    if (endian != kEndianMarker) {
        last_error_ = "unsupported binary take endianness";
        return false;
    }

    auto recover_tail = [&](const std::string& message) -> bool {
        if (recover_truncated_tail && !frames_.empty()) {
            recovered_truncated_tail_ = true;
            last_error_ = message;
            path_ = path;
            return true;
        }
        last_error_ = message;
        clear();
        return false;
    };

    while (true) {
        std::uint32_t marker = 0;
        input.read(
            reinterpret_cast<char*>(&marker),
            static_cast<std::streamsize>(sizeof(marker)));

        if (input.eof()) break;
        if (!input || marker != kFrameMarker) {
            return recover_tail(
                "recovered take with truncated final frame marker");
        }

        FusedPerformanceFrame frame;
        std::uint32_t sample_count = 0;
        if (!read_raw(input, frame.timestamp_ns) ||
            !read_raw(input, sample_count) ||
            sample_count > kMaxSamples) {
            return recover_tail(
                "recovered take with truncated final frame header");
        }

        frame.samples.reserve(sample_count);
        frame.selected_sources.reserve(sample_count);

        for (std::uint32_t i = 0; i < sample_count; ++i) {
            std::uint8_t raw_domain = 0;
            DomainSample sample;
            std::string source;
            std::uint32_t channel_count = 0;

            if (!read_raw(input, raw_domain) ||
                !decode_domain(raw_domain, sample.domain) ||
                !read_raw(input, sample.confidence) ||
                !read_string(input, source) ||
                !read_raw(input, channel_count) ||
                channel_count > kMaxChannels) {
                return recover_tail(
                    "recovered take with truncated final sample");
            }

            sample.channels.reserve(channel_count);
            sample.values.reserve(channel_count);

            for (std::uint32_t c = 0; c < channel_count; ++c) {
                std::string channel;
                float value = 0.0F;
                if (!read_string(input, channel) ||
                    !read_raw(input, value)) {
                    return recover_tail(
                        "recovered take with truncated final channel");
                }
                sample.channels.push_back(std::move(channel));
                sample.values.push_back(value);
            }

            frame.samples.push_back(std::move(sample));
            frame.selected_sources.push_back(std::move(source));
        }

        frames_.push_back(std::move(frame));
    }

    if (frames_.empty()) {
        last_error_ = "binary take contains no frames";
        return false;
    }

    if (!std::is_sorted(
            frames_.begin(),
            frames_.end(),
            [](const auto& a, const auto& b) {
                return a.timestamp_ns < b.timestamp_ns;
            })) {
        last_error_ = "binary take timestamps are not monotonic";
        clear();
        return false;
    }

    path_ = path;
    last_error_.clear();
    return true;
}

void BinaryTakeReader::clear() {
    path_.clear();
    frames_.clear();
    last_error_.clear();
    recovered_truncated_tail_ = false;
}

TimestampNs BinaryTakeReader::start_time_ns() const noexcept {
    return frames_.empty() ? 0 : frames_.front().timestamp_ns;
}

TimestampNs BinaryTakeReader::end_time_ns() const noexcept {
    return frames_.empty() ? 0 : frames_.back().timestamp_ns;
}

TimestampNs BinaryTakeReader::duration_ns() const noexcept {
    if (frames_.size() < 2) return 0;
    return end_time_ns() - start_time_ns();
}

const FusedPerformanceFrame* BinaryTakeReader::frame(
    std::size_t index) const noexcept {

    if (index >= frames_.size()) return nullptr;
    return &frames_[index];
}

std::size_t BinaryTakeReader::lower_bound_index(
    TimestampNs timestamp_ns) const noexcept {

    const auto it = std::lower_bound(
        frames_.begin(),
        frames_.end(),
        timestamp_ns,
        [](const FusedPerformanceFrame& frame, TimestampNs value) {
            return frame.timestamp_ns < value;
        });

    return static_cast<std::size_t>(
        std::distance(frames_.begin(), it));
}

} // namespace bdfrpc
