#include "bdfrpc/OpenCVDeviceEnumerator.h"

#include <opencv2/videoio.hpp>

#include <algorithm>
#include <string>

namespace bdfrpc {

std::vector<CaptureDeviceDescriptor> OpenCVDeviceEnumerator::probe(
    int first_index,
    int last_index) {

    if (last_index < first_index) std::swap(first_index, last_index);

    std::vector<CaptureDeviceDescriptor> devices;
    for (int index = first_index; index <= last_index; ++index) {
        cv::VideoCapture capture;
        if (!capture.open(index)) continue;

        CaptureDeviceDescriptor device;
        device.id = "opencv:" + std::to_string(index);
        device.name = "Camera " + std::to_string(index);
        device.backend = capture.getBackendName();
        device.kind = DeviceKind::Video;
        device.device_index = index;
        device.width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
        device.height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
        device.fps = capture.get(cv::CAP_PROP_FPS);
        device.available = true;
        devices.push_back(std::move(device));
        capture.release();
    }
    return devices;
}

} // namespace bdfrpc
