#include "bdfrpc/FrameSynchronizer.h"
#include "bdfrpc/FusionRuntime.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

bdfrpc::DomainSample one(
    bdfrpc::Domain domain,
    float confidence,
    float value) {

    bdfrpc::DomainSample sample;
    sample.domain = domain;
    sample.confidence = confidence;
    sample.channels = {"value"};
    sample.values = {value};
    return sample;
}

} // namespace

int main(int argc, char** argv) {
    using namespace bdfrpc;

    const std::uint64_t frame_count =
        argc > 1
            ? std::max<std::uint64_t>(
                  1,
                  static_cast<std::uint64_t>(
                      std::strtoull(argv[1], nullptr, 10)))
            : 100'000;

    const int stream_count =
        argc > 2
            ? std::clamp(std::atoi(argv[2]), 2, 16)
            : 4;

    std::vector<std::string> streams;
    streams.reserve(static_cast<std::size_t>(stream_count));
    for (int i = 0; i < stream_count; ++i) {
        streams.push_back("cam" + std::to_string(i));
    }

    FrameSynchronizer synchronizer(2'000'000, 8);
    synchronizer.set_required_streams(streams);

    FusionRuntime fusion(OperatingMode::Hybrid);

    constexpr TimestampNs frame_step = 16'666'667;
    constexpr TimestampNs stream_skew = 100'000;
    const TimestampNs base = 1'000'000'000;

    std::uint64_t synced_sets = 0;
    std::uint64_t fused_frames = 0;

    const auto begin = std::chrono::steady_clock::now();

    for (std::uint64_t frame_index = 0;
         frame_index < frame_count;
         ++frame_index) {

        const TimestampNs timestamp =
            base +
            static_cast<TimestampNs>(frame_index) *
                frame_step;

        for (int stream = 0;
             stream < stream_count;
             ++stream) {

            CaptureFrame camera;
            camera.source_id = "camera";
            camera.stream_id = streams[
                static_cast<std::size_t>(stream)];
            camera.sequence = frame_index + 1;
            camera.timestamp_ns =
                timestamp +
                static_cast<TimestampNs>(stream) *
                    stream_skew;
            synchronizer.push(std::move(camera));
        }

        if (auto set = synchronizer.try_pop()) {
            ++synced_sets;
        }

        CaptureFrame face;
        face.source_id = "bdfr_facial";
        face.stream_id = "face";
        face.sequence = frame_index + 1;
        face.timestamp_ns = timestamp;
        face.samples = {
            one(Domain::Face, 0.95f, 0.5f),
            one(Domain::Head, 0.95f, 0.1f)
        };
        if (!fusion.submit(std::move(face))) {
            std::cerr
                << "Fusion rejected facial frame "
                << frame_index << "\n";
            return 3;
        }

        CaptureFrame body;
        body.source_id = "easymocap";
        body.stream_id = "body";
        body.sequence = frame_index + 1;
        body.timestamp_ns = timestamp;
        body.samples = {
            one(Domain::Body, 0.95f, 0.2f),
            one(Domain::LeftHand, 0.90f, 0.3f),
            one(Domain::RightHand, 0.90f, 0.4f)
        };
        if (!fusion.submit(std::move(body))) {
            std::cerr
                << "Fusion rejected body frame "
                << frame_index << "\n";
            return 4;
        }

        const auto fused =
            fusion.evaluate(timestamp);
        if (fused.samples.size() != 5) {
            std::cerr
                << "Unexpected fused domain count at frame "
                << frame_index << ": "
                << fused.samples.size() << "\n";
            return 5;
        }
        ++fused_frames;
    }

    const auto end = std::chrono::steady_clock::now();
    const auto elapsed_ns =
        std::chrono::duration_cast<
            std::chrono::nanoseconds>(
            end - begin).count();

    if (synced_sets != frame_count ||
        fused_frames != frame_count) {
        std::cerr
            << "Correctness failure: sync="
            << synced_sets
            << " fusion=" << fused_frames
            << " expected=" << frame_count
            << "\n";
        return 6;
    }

    const double seconds =
        static_cast<double>(elapsed_ns) / 1.0e9;
    const double fps =
        seconds > 0.0
            ? static_cast<double>(frame_count) / seconds
            : 0.0;
    const double us_per_frame =
        frame_count > 0
            ? static_cast<double>(elapsed_ns) /
                  static_cast<double>(frame_count) /
                  1000.0
            : 0.0;

    std::cout
        << "BDFR PerformanceCapture synthetic benchmark\n"
        << "frames: " << frame_count << "\n"
        << "camera streams: " << stream_count << "\n"
        << "synchronized sets: " << synced_sets << "\n"
        << "fused frames: " << fused_frames << "\n"
        << std::fixed << std::setprecision(2)
        << "elapsed: " << seconds << " s\n"
        << "throughput: " << fps << " frames/s\n"
        << "processing: " << us_per_frame
        << " us/frame\n"
        << "max sync skew: "
        << static_cast<double>(
               synchronizer.stats().max_set_skew_ns) /
               1.0e6
        << " ms\n";

    return 0;
}
