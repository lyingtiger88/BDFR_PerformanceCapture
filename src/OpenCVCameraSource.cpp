#include "bdfrpc/OpenCVCameraSource.h"

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace bdfrpc {

namespace {

TimestampNs monotonic_now_ns() {
    return static_cast<TimestampNs>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

ImageBuffer copy_bgr_image(const cv::Mat& image) {
    ImageBuffer out;
    if (image.empty() || image.type() != CV_8UC3) return out;

    out.width = image.cols;
    out.height = image.rows;
    out.stride_bytes = image.cols * 3;
    out.format = PixelFormat::BGR8;

    const std::size_t row_bytes =
        static_cast<std::size_t>(image.cols) * 3u;
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(
        row_bytes * static_cast<std::size_t>(image.rows));

    if (image.isContinuous() && image.step == row_bytes) {
        std::memcpy(bytes->data(), image.data, bytes->size());
    } else {
        for (int y = 0; y < image.rows; ++y) {
            std::memcpy(
                bytes->data() + static_cast<std::size_t>(y) * row_bytes,
                image.ptr(y),
                row_bytes);
        }
    }

    out.bytes = std::move(bytes);
    return out;
}

} // namespace

class OpenCVCameraSource::Impl {
public:
    explicit Impl(CameraConfig cfg)
        : config(std::move(cfg)) {
        config.buffer_frames =
            std::max<std::size_t>(1, config.buffer_frames);
    }

    CameraConfig config;
    cv::VideoCapture capture;

    std::atomic<bool> running{false};
    std::thread worker;

    mutable std::mutex queue_mutex;
    std::deque<CaptureFrame> queue;

    std::atomic<std::uint64_t> sequence{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<TimestampNs> first_frame_ns{0};
    std::atomic<TimestampNs> last_frame_ns{0};

    void capture_loop() {
        while (running.load(std::memory_order_acquire)) {
            cv::Mat image;
            if (!capture.read(image) || image.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }

            auto buffer = copy_bgr_image(image);
            if (!buffer.valid()) continue;

            const TimestampNs ts = monotonic_now_ns();
            TimestampNs expected = 0;
            first_frame_ns.compare_exchange_strong(expected, ts);
            last_frame_ns.store(ts, std::memory_order_release);

            CaptureFrame frame;
            frame.source_id = config.source_id;
            frame.stream_id = config.stream_id;
            frame.sequence =
                sequence.fetch_add(1, std::memory_order_acq_rel) + 1;
            frame.timestamp_ns = ts;
            frame.image = std::move(buffer);

            std::lock_guard<std::mutex> lock(queue_mutex);
            queue.push_back(std::move(frame));
            while (queue.size() > config.buffer_frames) {
                queue.pop_front();
                dropped.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
};

OpenCVCameraSource::OpenCVCameraSource(CameraConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

OpenCVCameraSource::~OpenCVCameraSource() {
    stop();
}

const std::string& OpenCVCameraSource::id() const noexcept {
    return impl_->config.source_id;
}

bool OpenCVCameraSource::start() {
    if (impl_->running.load(std::memory_order_acquire)) return true;

    const int backend = impl_->config.backend;
    const bool opened = backend == 0
        ? impl_->capture.open(impl_->config.device_index)
        : impl_->capture.open(impl_->config.device_index, backend);

    if (!opened) return false;

    if (impl_->config.width > 0) {
        impl_->capture.set(cv::CAP_PROP_FRAME_WIDTH, impl_->config.width);
    }
    if (impl_->config.height > 0) {
        impl_->capture.set(cv::CAP_PROP_FRAME_HEIGHT, impl_->config.height);
    }
    if (impl_->config.fps > 0.0) {
        impl_->capture.set(cv::CAP_PROP_FPS, impl_->config.fps);
    }

    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        impl_->queue.clear();
    }
    impl_->sequence.store(0);
    impl_->dropped.store(0);
    impl_->first_frame_ns.store(0);
    impl_->last_frame_ns.store(0);
    impl_->running.store(true, std::memory_order_release);

    try {
        impl_->worker = std::thread([this] { impl_->capture_loop(); });
    } catch (...) {
        impl_->running.store(false, std::memory_order_release);
        impl_->capture.release();
        return false;
    }

    return true;
}

void OpenCVCameraSource::stop() noexcept {
    impl_->running.store(false, std::memory_order_release);
    if (impl_->worker.joinable()) impl_->worker.join();
    if (impl_->capture.isOpened()) impl_->capture.release();

    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    impl_->queue.clear();
}

bool OpenCVCameraSource::running() const noexcept {
    return impl_->running.load(std::memory_order_acquire);
}

std::optional<CaptureFrame> OpenCVCameraSource::poll() {
    if (!running()) return std::nullopt;

    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    if (impl_->queue.empty()) return std::nullopt;

    auto frame = std::move(impl_->queue.front());
    impl_->queue.pop_front();
    return frame;
}

double OpenCVCameraSource::measured_fps() const noexcept {
    const auto count = impl_->sequence.load(std::memory_order_acquire);
    const auto first = impl_->first_frame_ns.load(std::memory_order_acquire);
    const auto last = impl_->last_frame_ns.load(std::memory_order_acquire);

    if (count < 2 || first <= 0 || last <= first) return 0.0;

    const double seconds =
        static_cast<double>(last - first) / 1'000'000'000.0;
    return static_cast<double>(count - 1) / seconds;
}

std::uint64_t OpenCVCameraSource::dropped_frames() const noexcept {
    return impl_->dropped.load(std::memory_order_acquire);
}

std::size_t OpenCVCameraSource::buffered_frames() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    return impl_->queue.size();
}

} // namespace bdfrpc
