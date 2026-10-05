#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bdfrpc {

enum class DeviceKind {
    Video,
    Network,
    Mobile,
    File,
    Solver
};

struct CaptureDeviceDescriptor {
    std::string id;
    std::string name;
    std::string backend;
    DeviceKind kind{DeviceKind::Video};
    int device_index{-1};
    int width{0};
    int height{0};
    double fps{0.0};
    bool available{false};

    bool operator==(const CaptureDeviceDescriptor& other) const noexcept {
        return id == other.id &&
               name == other.name &&
               backend == other.backend &&
               kind == other.kind &&
               device_index == other.device_index &&
               width == other.width &&
               height == other.height &&
               fps == other.fps &&
               available == other.available;
    }
};

struct DeviceChangeSet {
    std::vector<CaptureDeviceDescriptor> added;
    std::vector<CaptureDeviceDescriptor> removed;
    std::vector<CaptureDeviceDescriptor> changed;

    bool empty() const noexcept {
        return added.empty() && removed.empty() && changed.empty();
    }
};

class DeviceMonitor {
public:
    DeviceChangeSet update(std::vector<CaptureDeviceDescriptor> snapshot);
    const std::vector<CaptureDeviceDescriptor>& snapshot() const noexcept;

private:
    std::vector<CaptureDeviceDescriptor> snapshot_;
};

} // namespace bdfrpc
