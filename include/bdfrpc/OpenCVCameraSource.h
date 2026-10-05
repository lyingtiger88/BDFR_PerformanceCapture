#pragma once

#include "bdfrpc/ICaptureSource.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace bdfrpc {

struct CameraConfig {
    int device_index{0};
    int width{1280};
    int height{720};
    double fps{60.0};
    int backend{0}; // 0 = OpenCV default backend
    std::size_t buffer_frames{4};
    std::string source_id{"opencv_camera"};
    std::string stream_id{"camera0"};
};

class OpenCVCameraSource final : public ICaptureSource {
public:
    explicit OpenCVCameraSource(CameraConfig config);
    ~OpenCVCameraSource() override;

    const std::string& id() const noexcept override;
    bool start() override;
    void stop() noexcept override;
    bool running() const noexcept override;
    std::optional<CaptureFrame> poll() override;

    double measured_fps() const noexcept;
    std::uint64_t dropped_frames() const noexcept;
    std::size_t buffered_frames() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bdfrpc
