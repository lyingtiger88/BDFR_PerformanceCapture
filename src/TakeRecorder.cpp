#include "bdfrpc/TakeRecorder.h"

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

    stream_.open(path, std::ios::out | std::ios::trunc);
    if (!stream_) return false;

    path_ = path;
    frames_written_ = 0;
    stream_ << "timestamp_ns,domain,source,confidence,channel,value\n";
    return static_cast<bool>(stream_);
}

bool TakeRecorder::append(const FusedPerformanceFrame& frame) {
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

void TakeRecorder::stop() {
    if (stream_.is_open()) {
        stream_.flush();
        stream_.close();
    }
}

} // namespace bdfrpc
