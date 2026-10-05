#include "bdfrpc/CaptureManager.h"
#include "bdfrpc/FrameSynchronizer.h"
#include "bdfrpc/OpenCVCameraSource.h"
#include "bdfrpc/OpenCVDeviceEnumerator.h"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    using namespace bdfrpc;

    const int seconds = argc > 1 ? std::max(1, std::atoi(argv[1])) : 5;
    const int last_index = argc > 2 ? std::max(0, std::atoi(argv[2])) : 9;
    const int tolerance_ms = argc > 3 ? std::max(1, std::atoi(argv[3])) : 20;

    const auto devices = OpenCVDeviceEnumerator::probe(0, last_index);
    if (devices.empty()) {
        std::cerr << "No OpenCV cameras detected.\n";
        return 3;
    }

    CaptureManager manager;
    std::vector<OpenCVCameraSource*> cameras;
    std::vector<std::string> streams;

    for (const auto& device : devices) {
        CameraConfig cfg;
        cfg.device_index = device.device_index;
        cfg.width = device.width > 0 ? device.width : 1280;
        cfg.height = device.height > 0 ? device.height : 720;
        cfg.fps = device.fps > 0.0 ? device.fps : 30.0;
        cfg.buffer_frames = 4;
        cfg.source_id = device.id;
        cfg.stream_id = device.id;

        auto camera = std::make_unique<OpenCVCameraSource>(cfg);
        cameras.push_back(camera.get());
        streams.push_back(cfg.stream_id);
        if (!manager.add(std::move(camera))) {
            std::cerr << "Failed to register " << device.id << "\n";
            return 4;
        }
    }

    FrameSynchronizer synchronizer(
        static_cast<TimestampNs>(tolerance_ms) * 1'000'000,
        8);
    synchronizer.set_required_streams(streams);

    if (!manager.start_all()) {
        std::cerr << "Failed to start all detected cameras.\n";
        manager.stop_all();
        return 5;
    }

    std::cout << "Capturing " << cameras.size()
              << " camera(s) for " << seconds
              << "s, sync tolerance=" << tolerance_ms << "ms\n";

    std::uint64_t synced_sets = 0;
    const auto until =
        std::chrono::steady_clock::now() + std::chrono::seconds(seconds);

    while (std::chrono::steady_clock::now() < until) {
        for (auto& frame : manager.poll_all()) {
            synchronizer.push(std::move(frame));
        }

        while (auto set = synchronizer.try_pop()) {
            (void)set;
            ++synced_sets;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    manager.stop_all();

    const auto& stats = synchronizer.stats();
    std::cout << "\nSynchronized sets: " << synced_sets << "\n";
    std::cout << std::left
              << std::setw(14) << "STREAM"
              << std::setw(10) << "FPS"
              << std::setw(10) << "RECV"
              << std::setw(10) << "EMIT"
              << std::setw(10) << "STALE"
              << std::setw(10) << "QDROP"
              << std::setw(12) << "OFFSET_MS"
              << std::setw(12) << "CAM_DROP"
              << "\n";

    for (std::size_t i = 0; i < streams.size(); ++i) {
        const auto it = stats.streams.find(streams[i]);
        StreamSyncStats s;
        if (it != stats.streams.end()) s = it->second;

        std::cout << std::left
                  << std::setw(14) << streams[i]
                  << std::setw(10) << std::fixed << std::setprecision(1)
                  << cameras[i]->measured_fps()
                  << std::setw(10) << s.received_frames
                  << std::setw(10) << s.emitted_frames
                  << std::setw(10) << s.dropped_stale
                  << std::setw(10) << s.dropped_overflow
                  << std::setw(12) << std::setprecision(2)
                  << (static_cast<double>(s.last_offset_ns) / 1.0e6)
                  << std::setw(12) << cameras[i]->dropped_frames()
                  << "\n";
    }

    std::cout << "Last set skew: "
              << static_cast<double>(stats.last_set_skew_ns) / 1.0e6
              << " ms\n";
    std::cout << "Max set skew: "
              << static_cast<double>(stats.max_set_skew_ns) / 1.0e6
              << " ms\n";

    return 0;
}
