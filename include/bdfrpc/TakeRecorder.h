#pragma once

#include "bdfrpc/Types.h"

#include <cstdint>
#include <fstream>
#include <string>

namespace bdfrpc {

class TakeRecorder {
public:
    TakeRecorder() = default;
    ~TakeRecorder();

    bool start(const std::string& path);
    bool append(const FusedPerformanceFrame& frame);
    void stop();

    bool recording() const noexcept { return stream_.is_open(); }
    std::uint64_t frames_written() const noexcept { return frames_written_; }
    const std::string& path() const noexcept { return path_; }

private:
    std::ofstream stream_;
    std::string path_;
    std::uint64_t frames_written_{0};
};

} // namespace bdfrpc
