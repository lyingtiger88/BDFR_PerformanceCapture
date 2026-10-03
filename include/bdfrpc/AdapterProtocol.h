#pragma once

#include <optional>
#include <string>

namespace bdfrpc {

struct AdapterHello {
    int protocol_version{1};
    std::string adapter_name;
    std::string adapter_version;
    std::string capabilities_csv;
};

std::string encode_hello(const AdapterHello& hello);
std::optional<AdapterHello> decode_hello(const std::string& line);

} // namespace bdfrpc
