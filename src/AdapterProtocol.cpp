#include "bdfrpc/AdapterProtocol.h"
#include <sstream>
#include <vector>

namespace bdfrpc {

namespace {
std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> out;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, delimiter)) out.push_back(item);
    return out;
}
}

std::string encode_hello(const AdapterHello& hello) {
    // Deliberately tiny line protocol for local process adapters.
    // Fields must not contain '|'; a future binary transport can replace this without
    // coupling the core to any solver.
    return "BDFRPC|" + std::to_string(hello.protocol_version) + "|" +
           hello.adapter_name + "|" + hello.adapter_version + "|" +
           hello.capabilities_csv;
}

std::optional<AdapterHello> decode_hello(const std::string& line) {
    const auto fields = split(line, '|');
    if (fields.size() != 5 || fields[0] != "BDFRPC") return std::nullopt;

    AdapterHello out;
    try {
        out.protocol_version = std::stoi(fields[1]);
    } catch (...) {
        return std::nullopt;
    }
    if (out.protocol_version <= 0 || fields[2].empty()) return std::nullopt;

    out.adapter_name = fields[2];
    out.adapter_version = fields[3];
    out.capabilities_csv = fields[4];
    return out;
}

} // namespace bdfrpc
