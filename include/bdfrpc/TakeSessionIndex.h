#pragma once

#include "bdfrpc/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bdfrpc {

enum class TakeFileFormat {
    BinaryV1,
    CsvLegacy,
    Unknown
};

struct TakeSessionInfo {
    std::string path;
    std::string filename;
    TakeFileFormat format{TakeFileFormat::Unknown};
    std::uintmax_t size_bytes{0};
    std::size_t frame_count{0};
    TimestampNs duration_ns{0};
    bool valid{false};
    std::string error;
};

class TakeSessionIndex {
public:
    static std::vector<TakeSessionInfo> scan(
        const std::string& directory,
        bool include_invalid = true);

    static const char* format_name(TakeFileFormat format) noexcept;
};

} // namespace bdfrpc
