#include "bdfrpc/Calibration.h"
#include "bdfrpc/FrameSynchronizer.h"
#include "bdfrpc/OpenCVCameraSource.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct RigCamera {
    bdfrpc::CameraCalibration calibration;
    int device_index{-1};
    std::unique_ptr<bdfrpc::OpenCVCameraSource> source;
    std::vector<std::vector<cv::Point2f>> observations;
};

bool parse_opencv_index(const std::string& id, int& index) {
    constexpr const char* prefix = "opencv:";
    if (id.rfind(prefix, 0) != 0) return false;
    try {
        std::size_t used = 0;
        index = std::stoi(id.substr(7), &used);
        return used == id.size() - 7 && index >= 0;
    } catch (...) {
        return false;
    }
}

cv::Mat camera_matrix(const bdfrpc::CameraIntrinsics& in) {
    return (cv::Mat_<double>(3, 3) <<
        in.fx, 0.0, in.cx,
        0.0, in.fy, in.cy,
        0.0, 0.0, 1.0);
}

cv::Mat distortion(const bdfrpc::CameraIntrinsics& in) {
    cv::Mat d(8, 1, CV_64F);
    for (int i = 0; i < 8; ++i) {
        d.at<double>(i, 0) =
            in.distortion[static_cast<std::size_t>(i)];
    }
    return d;
}

bool find_corners(
    const bdfrpc::CaptureFrame& frame,
    const cv::Size& pattern_size,
    std::vector<cv::Point2f>& corners) {

    if (!frame.image.valid() ||
        frame.image.format != bdfrpc::PixelFormat::BGR8) {
        return false;
    }

    const auto& image = frame.image;
    cv::Mat bgr(
        image.height,
        image.width,
        CV_8UC3,
        image.bytes->data(),
        static_cast<std::size_t>(image.stride_bytes));

    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

    if (!cv::findChessboardCorners(
            gray,
            pattern_size,
            corners,
            cv::CALIB_CB_ADAPTIVE_THRESH |
                cv::CALIB_CB_NORMALIZE_IMAGE |
                cv::CALIB_CB_FAST_CHECK)) {
        return false;
    }

    cv::cornerSubPix(
        gray,
        corners,
        cv::Size(11, 11),
        cv::Size(-1, -1),
        cv::TermCriteria(
            cv::TermCriteria::EPS + cv::TermCriteria::COUNT,
            30,
            0.001));
    return true;
}

} // namespace

int main(int argc, char** argv) {
    using namespace bdfrpc;

    const std::string input =
        argc > 1 ? argv[1] : "bdfr_camera_calibration.txt";
    const int reference_index =
        argc > 2 ? std::max(0, std::atoi(argv[2])) : 0;
    const int columns =
        argc > 3 ? std::max(2, std::atoi(argv[3])) : 9;
    const int rows =
        argc > 4 ? std::max(2, std::atoi(argv[4])) : 6;
    const double square_size =
        argc > 5 ? std::max(1e-6, std::atof(argv[5])) : 0.025;
    const int target_samples =
        argc > 6 ? std::max(5, std::atoi(argv[6])) : 15;
    const std::string output =
        argc > 7 ? argv[7] : "bdfr_rig_calibration.txt";

    std::ifstream file(input, std::ios::binary);
    if (!file) {
        std::cerr << "Unable to read calibration profile: "
                  << input << "\n";
        return 2;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string parse_error;
    auto profile =
        CalibrationProfile::deserialize(buffer.str(), &parse_error);
    if (!profile) {
        std::cerr << "Invalid calibration profile: "
                  << parse_error << "\n";
        return 3;
    }

    std::vector<RigCamera> cameras;
    for (const auto& calibration : profile->cameras()) {
        int index = -1;
        if (!parse_opencv_index(calibration.camera_id, index)) {
            continue;
        }
        RigCamera camera;
        camera.calibration = calibration;
        camera.device_index = index;
        cameras.push_back(std::move(camera));
    }

    std::sort(
        cameras.begin(), cameras.end(),
        [](const RigCamera& a, const RigCamera& b) {
            return a.device_index < b.device_index;
        });

    if (cameras.size() < 2) {
        std::cerr
            << "Rig calibration requires at least two opencv:N cameras "
               "with valid intrinsics in the profile.\n";
        return 4;
    }

    auto ref_it = std::find_if(
        cameras.begin(), cameras.end(),
        [&](const RigCamera& c) {
            return c.device_index == reference_index;
        });
    if (ref_it == cameras.end()) {
        std::cerr << "Reference camera opencv:"
                  << reference_index << " is not in the profile.\n";
        return 5;
    }
    const std::size_t ref =
        static_cast<std::size_t>(
            std::distance(cameras.begin(), ref_it));

    std::vector<std::string> streams;
    for (auto& camera : cameras) {
        CameraConfig cfg;
        cfg.device_index = camera.device_index;
        cfg.width = camera.calibration.intrinsics.width;
        cfg.height = camera.calibration.intrinsics.height;
        cfg.fps = 30.0;
        cfg.buffer_frames = 4;
        cfg.source_id = camera.calibration.camera_id;
        cfg.stream_id = camera.calibration.camera_id;

        camera.source =
            std::make_unique<OpenCVCameraSource>(cfg);
        if (!camera.source->start()) {
            std::cerr << "Failed to open "
                      << camera.calibration.camera_id << "\n";
            for (auto& opened : cameras) {
                if (opened.source) opened.source->stop();
            }
            return 6;
        }
        streams.push_back(cfg.stream_id);
    }

    FrameSynchronizer sync(20'000'000, 8);
    sync.set_required_streams(streams);

    const cv::Size pattern_size(columns, rows);
    auto last_accepted =
        std::chrono::steady_clock::now() -
        std::chrono::seconds(5);
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::minutes(5);

    std::cout
        << "BDFR multi-camera rig calibration\n"
        << "Cameras: " << cameras.size() << "\n"
        << "Reference: opencv:" << reference_index << "\n"
        << "Need all cameras to see the same chessboard simultaneously.\n";

    while (
        static_cast<int>(cameras[ref].observations.size()) <
            target_samples &&
        std::chrono::steady_clock::now() < deadline) {

        for (auto& camera : cameras) {
            while (auto frame = camera.source->poll()) {
                sync.push(std::move(*frame));
            }
        }

        auto set = sync.try_pop();
        if (!set) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(2));
            continue;
        }

        std::map<std::string, const CaptureFrame*> by_stream;
        for (const auto& frame : set->frames) {
            by_stream[frame.stream_id] = &frame;
        }

        std::vector<std::vector<cv::Point2f>> found(
            cameras.size());
        bool all_found = true;

        for (std::size_t i = 0; i < cameras.size(); ++i) {
            const auto it =
                by_stream.find(cameras[i].calibration.camera_id);
            if (it == by_stream.end() ||
                !find_corners(
                    *it->second,
                    pattern_size,
                    found[i])) {
                all_found = false;
                break;
            }
        }

        if (!all_found) continue;

        const auto now = std::chrono::steady_clock::now();
        if (now - last_accepted <
            std::chrono::milliseconds(500)) {
            continue;
        }

        for (std::size_t i = 0; i < cameras.size(); ++i) {
            cameras[i].observations.push_back(
                std::move(found[i]));
        }
        last_accepted = now;

        std::cout << "Accepted synchronized rig sample "
                  << cameras[ref].observations.size()
                  << "/" << target_samples
                  << " · skew "
                  << static_cast<double>(set->max_skew_ns) / 1.0e6
                  << " ms\n";
    }

    for (auto& camera : cameras) {
        if (camera.source) camera.source->stop();
    }

    if (static_cast<int>(
            cameras[ref].observations.size()) <
        target_samples) {
        std::cerr << "Rig calibration timed out after "
                  << cameras[ref].observations.size()
                  << " synchronized observations.\n";
        return 7;
    }

    std::vector<cv::Point3f> board;
    board.reserve(
        static_cast<std::size_t>(columns * rows));
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            board.emplace_back(
                static_cast<float>(x * square_size),
                static_cast<float>(y * square_size),
                0.0F);
        }
    }

    std::vector<std::vector<cv::Point3f>> object_points(
        cameras[ref].observations.size(),
        board);

    auto reference_calibration =
        cameras[ref].calibration;
    reference_calibration.extrinsics = {};
    profile->upsert(reference_calibration);

    const cv::Size image_size(
        reference_calibration.intrinsics.width,
        reference_calibration.intrinsics.height);

    for (std::size_t i = 0; i < cameras.size(); ++i) {
        if (i == ref) continue;

        cv::Mat k1 = camera_matrix(
            reference_calibration.intrinsics);
        cv::Mat d1 = distortion(
            reference_calibration.intrinsics);
        cv::Mat k2 = camera_matrix(
            cameras[i].calibration.intrinsics);
        cv::Mat d2 = distortion(
            cameras[i].calibration.intrinsics);

        cv::Mat r, t, e, f;
        const double rms = cv::stereoCalibrate(
            object_points,
            cameras[ref].observations,
            cameras[i].observations,
            k1,
            d1,
            k2,
            d2,
            image_size,
            r,
            t,
            e,
            f,
            cv::CALIB_FIX_INTRINSIC,
            cv::TermCriteria(
                cv::TermCriteria::EPS +
                    cv::TermCriteria::COUNT,
                100,
                1e-6));

        auto solved = cameras[i].calibration;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                solved.extrinsics.rotation[
                    static_cast<std::size_t>(row * 3 + col)] =
                    r.at<double>(row, col);
            }
            solved.extrinsics.translation[
                static_cast<std::size_t>(row)] =
                t.at<double>(row, 0);
        }

        if (!profile->upsert(solved)) {
            std::cerr << "Failed to save solved extrinsics for "
                      << solved.camera_id << "\n";
            return 8;
        }

        std::cout << solved.camera_id
                  << " stereo RMS: " << rms << "\n";
    }

    std::ofstream output_file(output, std::ios::binary);
    if (!output_file) {
        std::cerr << "Unable to create " << output << "\n";
        return 9;
    }
    output_file << profile->serialize();

    std::cout << "Rig calibration saved: "
              << output << "\n";
    return 0;
}
