#pragma once

#include "bdfrpc/Types.h"
#include "bdfrpc/BinaryTake.h"

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

namespace bdfrpc {


class TakeRecorder {
public:
    TakeRecorder() = default;
    ~TakeRecorder();

    // .bdfrtake => deterministic binary v1
    // other extensions => legacy CSV
    bool start(const std::string& path);
    bool append(const FusedPerformanceFrame& frame);
    bool flush();
    void stop();

    bool recording() const noexcept;
    std::uint64_t frames_written() const noexcept;
    const std::string& path() const noexcept { return path_; }
    bool binary_format() const noexcept { return binary_format_; }

private:
    bool append_csv(const FusedPerformanceFrame& frame);

    std::ofstream stream_;
    std::unique_ptr<BinaryTakeWriter> binary_;
    std::string path_;
    std::uint64_t frames_written_{0};
    bool binary_format_{false};
};

} // namespace bdfrpc
