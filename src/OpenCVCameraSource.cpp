#include "bdfrpc/OpenCVCameraSource.h"

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <chrono>
#include <cstring>
#include <utility>

namespace bdfrpc {

namespace {
TimestampNs monotonic_now_ns() {
    return static_cast<TimestampNs>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

class OpenCVCameraSource::Impl {
public:
    explicit Impl(CameraConfig cfg) : config(std::move(cfg)) {}

    CameraConfig config;
    cv::VideoCapture capture;
    std::uint64_t sequence{0};
    TimestampNs first_frame_ns{0};
    TimestampNs last_frame_ns{0};
};

OpenCVCameraSource::OpenCVCameraSource(CameraConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

OpenCVCameraSource::~OpenCVCameraSource() = default;

const std::string& OpenCVCameraSource::id() const noexcept {
    return impl_->config.source_id;
}

bool OpenCVCameraSource::start() {
    if (impl_->capture.isOpened()) return true;

    const int backend = impl_->config.backend;
    const bool opened = backend == 0
        ? impl_->capture.open(impl_->config.device_index)
        : impl_->capture.open(impl_->config.device_index, backend);

    if (!opened) return false;

    if (impl_->config.width > 0)
        impl_->capture.set(cv::CAP_PROP_FRAME_WIDTH, impl_->config.width);
    if (impl_->config.height > 0)
        impl_->capture.set(cv::CAP_PROP_FRAME_HEIGHT, impl_->config.height);
    if (impl_->config.fps > 0.0)
        impl_->capture.set(cv::CAP_PROP_FPS, impl_->config.fps);

    impl_->sequence = 0;
    impl_->first_frame_ns = 0;
    impl_->last_frame_ns = 0;
    return true;
}

void OpenCVCameraSource::stop() noexcept {
    if (impl_->capture.isOpened()) impl_->capture.release();
}

bool OpenCVCameraSource::running() const noexcept {
    return impl_->capture.isOpened();
}

std::optional<CaptureFrame> OpenCVCameraSource::poll() {
    if (!impl_->capture.isOpened()) return std::nullopt;

    cv::Mat image;
    if (!impl_->capture.read(image) || image.empty()) return std::nullopt;
    if (image.type() != CV_8UC3) return std::nullopt;

    const auto ts = monotonic_now_ns();
    if (impl_->first_frame_ns == 0) impl_->first_frame_ns = ts;
    impl_->last_frame_ns = ts;

    CaptureFrame frame;
    frame.source_id = impl_->config.source_id;
    frame.stream_id = impl_->config.stream_id;
    frame.sequence = ++impl_->sequence;
    frame.timestamp_ns = ts;
    frame.image.width = image.cols;
    frame.image.height = image.rows;
    frame.image.stride_bytes = image.cols * 3;
    frame.image.format = PixelFormat::BGR8;

    const std::size_t row_bytes = static_cast<std::size_t>(image.cols) * 3u;
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(
        row_bytes * static_cast<std::size_t>(image.rows));

    if (image.isContinuous() && image.step == row_bytes) {
        std::memcpy(bytes->data(), image.data, bytes->size());
    } else {
        for (int y = 0; y < image.rows; ++y) {
            std::memcpy(bytes->data() + static_cast<std::size_t>(y) * row_bytes,
                        image.ptr(y), row_bytes);
        }
    }
    frame.image.bytes = std::move(bytes);
    return frame;
}

double OpenCVCameraSource::measured_fps() const noexcept {
    if (impl_->sequence < 2 || impl_->last_frame_ns <= impl_->first_frame_ns) return 0.0;
    const double seconds =
        static_cast<double>(impl_->last_frame_ns - impl_->first_frame_ns) / 1'000'000'000.0;
    return static_cast<double>(impl_->sequence - 1) / seconds;
}

} // namespace bdfrpc
