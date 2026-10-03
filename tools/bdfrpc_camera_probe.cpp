#include "bdfrpc/OpenCVCameraSource.h"
#include <chrono>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    using namespace bdfrpc;

    const int index = argc > 1 ? std::atoi(argv[1]) : 0;
    CameraConfig config;
    config.device_index = index;
    config.stream_id = "camera" + std::to_string(index);
    config.source_id = "opencv_" + std::to_string(index);

    OpenCVCameraSource camera(config);
    if (!camera.start()) {
        std::cerr << "Failed to open camera index " << index << "\n";
        return 2;
    }

    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::uint64_t frames = 0;
    while (std::chrono::steady_clock::now() < end) {
        if (auto frame = camera.poll()) {
            ++frames;
            if (frames == 1) {
                std::cout << "First frame: " << frame->image.width << "x"
                          << frame->image.height << "\n";
            }
        }
    }

    std::cout << "Captured " << frames << " frames, measured "
              << camera.measured_fps() << " FPS\n";
    camera.stop();
    return frames > 0 ? 0 : 3;
}
