#include "bdfrpc/TakeRecorder.h"
#include "bdfrpc/BinaryTake.h"

#include <iomanip>
#include <sstream>

namespace bdfrpc {
namespace {

std::string csv_field(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;

    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char ch : value) {
        if (ch == '"') escaped.push_back('"');
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

} // namespace

TakeRecorder::~TakeRecorder() {
    stop();
}

bool TakeRecorder::start(const std::string& path) {
    stop();
    if (path.empty()) return false;

    path_ = path;
    frames_written_ = 0;
    binary_format_ =
        path.size() >= 9 &&
        path.substr(path.size() - 9) == ".bdfrtake";

    if (binary_format_) {
        binary_ = std::make_unique<BinaryTakeWriter>();
        if (!binary_->start(path)) {
            binary_.reset();
            binary_format_ = false;
            path_.clear();
            return false;
        }
        return true;
    }

    stream_.open(path, std::ios::out | std::ios::trunc);
    if (!stream_) {
        path_.clear();
        return false;
    }

    stream_ << "timestamp_ns,domain,source,confidence,channel,value\n";
    return static_cast<bool>(stream_);
}

bool TakeRecorder::append(const FusedPerformanceFrame& frame) {
    if (binary_) return binary_->append(frame);
    return append_csv(frame);
}

bool TakeRecorder::append_csv(const FusedPerformanceFrame& frame) {
    if (!stream_) return false;

    for (std::size_t sample_index = 0; sample_index < frame.samples.size(); ++sample_index) {
        const auto& sample = frame.samples[sample_index];
        const std::string source =
            sample_index < frame.selected_sources.size()
                ? frame.selected_sources[sample_index]
                : std::string{};

        for (std::size_t value_index = 0; value_index < sample.values.size(); ++value_index) {
            const std::string channel =
                value_index < sample.channels.size() && !sample.channels[value_index].empty()
                    ? sample.channels[value_index]
                    : ("value." + std::to_string(value_index));

            stream_
                << frame.timestamp_ns << ','
                << to_string(sample.domain) << ','
                << csv_field(source) << ','
                << std::setprecision(9) << sample.confidence << ','
                << csv_field(channel) << ','
                << std::setprecision(9) << sample.values[value_index] << '\n';
        }
    }

    if (!stream_) return false;
    ++frames_written_;
    return true;
}

bool TakeRecorder::flush() {
    if (binary_) return binary_->flush();
    if (!stream_) return false;
    stream_.flush();
    return static_cast<bool>(stream_);
}

void TakeRecorder::stop() {
    if (binary_) {
        binary_->stop();
        binary_.reset();
    }
    if (stream_.is_open()) {
        stream_.flush();
        stream_.close();
    }
    binary_format_ = false;
}

bool TakeRecorder::recording() const noexcept {
    return binary_
        ? binary_->recording()
        : stream_.is_open();
}

std::uint64_t TakeRecorder::frames_written() const noexcept {
    return binary_
        ? binary_->frames_written()
        : frames_written_;
}

} // namespace bdfrpc
