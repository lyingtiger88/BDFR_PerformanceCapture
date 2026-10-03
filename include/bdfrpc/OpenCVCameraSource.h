#pragma once

#include "bdfrpc/ICaptureSource.h"
#include <memory>
#include <string>

namespace bdfrpc {

struct CameraConfig {
    int device_index{0};
    int width{1280};
    int height{720};
    double fps{60.0};
    int backend{0}; // 0 = OpenCV default backend
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

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bdfrpc
