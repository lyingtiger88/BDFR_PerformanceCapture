#include "bdfrpc/TakeReader.h"
#include "bdfrpc/BinaryTake.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bdfrpc {
namespace {

std::vector<std::string> parse_csv_row(const std::string& row) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;

    for (std::size_t i = 0; i < row.size(); ++i) {
        const char ch = row[i];

        if (quoted) {
            if (ch == '"') {
                if (i + 1 < row.size() && row[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field.push_back(ch);
            }
            continue;
        }

        if (ch == '"') {
            quoted = true;
        } else if (ch == ',') {
            fields.push_back(std::move(field));
            field.clear();
        } else {
            field.push_back(ch);
        }
    }

    if (quoted) return {};
    fields.push_back(std::move(field));
    return fields;
}

std::optional<Domain> parse_domain(const std::string& value) {
    if (value == "face") return Domain::Face;
    if (value == "head") return Domain::Head;
    if (value == "body") return Domain::Body;
    if (value == "left_hand") return Domain::LeftHand;
    if (value == "right_hand") return Domain::RightHand;
    return std::nullopt;
}

struct SampleKey {
    TimestampNs timestamp{0};
    Domain domain{Domain::Body};
    std::string source;
    float confidence{0.0F};
};

} // namespace

bool TakeReader::load(const std::string& path) {
    clear();
    if (path.empty()) {
        last_error_ = "empty take path";
        return false;
    }

    {
        std::ifstream probe(path, std::ios::binary);
        if (!probe) {
            last_error_ = "unable to open take";
            return false;
        }

        char magic[8]{};
        probe.read(magic, 8);
        if (probe.gcount() == 8 &&
            std::string(magic, magic + 8) == "BDFRTAKE") {
            BinaryTakeReader binary;
            if (!binary.load(path)) {
                last_error_ = binary.last_error();
                return false;
            }
            frames_ = binary.frames();
            path_ = path;
            last_error_.clear();
            return true;
        }
    }

    std::ifstream input(path);
    if (!input) {
        last_error_ = "unable to open take";
        return false;
    }

    std::string header;
    if (!std::getline(input, header) ||
        header != "timestamp_ns,domain,source,confidence,channel,value") {
        last_error_ = "unsupported take header";
        return false;
    }

    std::map<TimestampNs, FusedPerformanceFrame> by_time;
    std::map<std::string, std::size_t> sample_lookup;

    std::string line;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty()) continue;

        const auto fields = parse_csv_row(line);
        if (fields.size() != 6) {
            last_error_ = "invalid CSV row at line " + std::to_string(line_number);
            clear();
            return false;
        }

        TimestampNs timestamp = 0;
        float confidence = 0.0F;
        float value = 0.0F;

        try {
            std::size_t used = 0;
            timestamp = std::stoll(fields[0], &used);
            if (used != fields[0].size()) throw std::invalid_argument("timestamp");

            confidence = std::stof(fields[3], &used);
            if (used != fields[3].size()) throw std::invalid_argument("confidence");

            value = std::stof(fields[5], &used);
            if (used != fields[5].size()) throw std::invalid_argument("value");
        } catch (...) {
            last_error_ = "invalid numeric field at line " + std::to_string(line_number);
            clear();
            return false;
        }

        const auto domain = parse_domain(fields[1]);
        if (!domain) {
            last_error_ = "invalid domain at line " + std::to_string(line_number);
            clear();
            return false;
        }

        auto& frame = by_time[timestamp];
        frame.timestamp_ns = timestamp;

        const std::string key =
            std::to_string(timestamp) + "|" +
            fields[1] + "|" + fields[2] + "|" + fields[3];

        std::size_t sample_index = 0;
        const auto existing = sample_lookup.find(key);
        if (existing == sample_lookup.end()) {
            sample_index = frame.samples.size();
            DomainSample sample;
            sample.domain = *domain;
            sample.confidence = confidence;
            frame.samples.push_back(std::move(sample));
            frame.selected_sources.push_back(fields[2]);
            sample_lookup[key] = sample_index;
        } else {
            sample_index = existing->second;
        }

        if (sample_index >= frame.samples.size()) {
            last_error_ = "internal take grouping error";
            clear();
            return false;
        }

        frame.samples[sample_index].channels.push_back(fields[4]);
        frame.samples[sample_index].values.push_back(value);
    }

    frames_.reserve(by_time.size());
    for (auto& [timestamp, frame] : by_time) {
        (void)timestamp;
        frames_.push_back(std::move(frame));
    }

    if (frames_.empty()) {
        last_error_ = "take contains no frames";
        return false;
    }

    path_ = path;
    last_error_.clear();
    return true;
}

void TakeReader::clear() {
    path_.clear();
    frames_.clear();
    last_error_.clear();
}

TimestampNs TakeReader::start_time_ns() const noexcept {
    return frames_.empty() ? 0 : frames_.front().timestamp_ns;
}

TimestampNs TakeReader::end_time_ns() const noexcept {
    return frames_.empty() ? 0 : frames_.back().timestamp_ns;
}

TimestampNs TakeReader::duration_ns() const noexcept {
    if (frames_.size() < 2) return 0;
    return end_time_ns() - start_time_ns();
}

const FusedPerformanceFrame* TakeReader::frame(
    std::size_t index) const noexcept {

    if (index >= frames_.size()) return nullptr;
    return &frames_[index];
}

std::size_t TakeReader::lower_bound_index(
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
