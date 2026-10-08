#pragma once

#include "bdfrpc/Types.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace bdfrpc {

class BinaryTakeWriter {
public:
    BinaryTakeWriter() = default;
    ~BinaryTakeWriter();

    bool start(const std::string& path);
    bool append(const FusedPerformanceFrame& frame);
    bool flush();
    void stop();

    bool recording() const noexcept { return stream_.is_open(); }
    std::uint64_t frames_written() const noexcept { return frames_written_; }
    const std::string& path() const noexcept { return path_; }
    const std::string& last_error() const noexcept { return last_error_; }

private:
    std::ofstream stream_;
    std::string path_;
    std::string last_error_;
    std::uint64_t frames_written_{0};
};

class BinaryTakeReader {
public:
    bool load(const std::string& path);
    void clear();

    bool loaded() const noexcept { return !frames_.empty(); }
    const std::vector<FusedPerformanceFrame>& frames() const noexcept {
        return frames_;
    }

    TimestampNs start_time_ns() const noexcept;
    TimestampNs end_time_ns() const noexcept;
    TimestampNs duration_ns() const noexcept;
    const FusedPerformanceFrame* frame(std::size_t index) const noexcept;
    std::size_t lower_bound_index(TimestampNs timestamp_ns) const noexcept;

    const std::string& path() const noexcept { return path_; }
    const std::string& last_error() const noexcept { return last_error_; }

private:
    std::string path_;
    std::vector<FusedPerformanceFrame> frames_;
    std::string last_error_;
};

} // namespace bdfrpc
