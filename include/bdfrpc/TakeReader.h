#pragma once

#include "bdfrpc/Types.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace bdfrpc {

class TakeReader {
public:
    bool load(const std::string& path);
    void clear();

    bool loaded() const noexcept { return !frames_.empty(); }
    const std::string& path() const noexcept { return path_; }
    const std::vector<FusedPerformanceFrame>& frames() const noexcept { return frames_; }

    TimestampNs start_time_ns() const noexcept;
    TimestampNs end_time_ns() const noexcept;
    TimestampNs duration_ns() const noexcept;

    const FusedPerformanceFrame* frame(std::size_t index) const noexcept;
    std::size_t lower_bound_index(TimestampNs timestamp_ns) const noexcept;

    const std::string& last_error() const noexcept { return last_error_; }

private:
    std::string path_;
    std::vector<FusedPerformanceFrame> frames_;
    std::string last_error_;
};

} // namespace bdfrpc
