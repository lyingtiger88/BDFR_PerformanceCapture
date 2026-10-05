#pragma once

#include "bdfrpc/DeviceDiscovery.h"

#include <vector>

namespace bdfrpc {

class OpenCVDeviceEnumerator {
public:
    static std::vector<CaptureDeviceDescriptor> probe(
        int first_index = 0,
        int last_index = 9);
};

} // namespace bdfrpc
