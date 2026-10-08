#include "bdfrpc/TakeSessionIndex.h"

#include "bdfrpc/TakeReader.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace bdfrpc {
namespace {

bool has_suffix(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(
               value.size() - suffix.size(),
               suffix.size(),
               suffix) == 0;
}

TakeFileFormat detect_format(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    char magic[8]{};
    input.read(magic, 8);

    if (input.gcount() == 8 &&
        std::string(magic, magic + 8) == "BDFRTAKE") {
        return TakeFileFormat::BinaryV1;
    }

    const auto name = path.filename().string();
    if (has_suffix(name, ".csv")) {
        return TakeFileFormat::CsvLegacy;
    }

    return TakeFileFormat::Unknown;
}

bool is_candidate(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    return has_suffix(name, ".bdfrtake") ||
           has_suffix(name, ".bdfrtake.csv") ||
           has_suffix(name, ".csv");
}

} // namespace

std::vector<TakeSessionInfo> TakeSessionIndex::scan(
    const std::string& directory,
    bool include_invalid) {

    std::vector<TakeSessionInfo> out;
    std::error_code ec;

    const std::filesystem::path root(directory);
    if (!std::filesystem::is_directory(root, ec) || ec) {
        return out;
    }

    for (const auto& entry :
         std::filesystem::directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;
        if (!is_candidate(entry.path())) continue;

        TakeSessionInfo info;
        info.path = entry.path().string();
        info.filename = entry.path().filename().string();
        info.format = detect_format(entry.path());
        info.size_bytes = entry.file_size(ec);
        if (ec) {
            info.size_bytes = 0;
            ec.clear();
        }

        TakeReader reader;
        if (reader.load(info.path)) {
            info.valid = true;
            info.frame_count = reader.frames().size();
            info.duration_ns = reader.duration_ns();
        } else {
            info.valid = false;
            info.error = reader.last_error();
        }

        if (info.valid || include_invalid) {
            out.push_back(std::move(info));
        }
    }

    std::sort(
        out.begin(),
        out.end(),
        [](const TakeSessionInfo& a, const TakeSessionInfo& b) {
            return a.filename < b.filename;
        });

    return out;
}

const char* TakeSessionIndex::format_name(
    TakeFileFormat format) noexcept {

    switch (format) {
        case TakeFileFormat::BinaryV1: return "BDFR Take v1";
        case TakeFileFormat::CsvLegacy: return "CSV";
        case TakeFileFormat::Unknown: break;
    }
    return "Unknown";
}

} // namespace bdfrpc
