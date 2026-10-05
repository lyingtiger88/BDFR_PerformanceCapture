#include "bdfrpc/Calibration.h"
#include "bdfrpc/OpenCVCameraSource.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    using namespace bdfrpc;

    const int device_index = argc > 1 ? std::atoi(argv[1]) : 0;
    const int columns = argc > 2 ? std::max(2, std::atoi(argv[2])) : 9;
    const int rows = argc > 3 ? std::max(2, std::atoi(argv[3])) : 6;
    const double square_size =
        argc > 4 ? std::max(1e-6, std::atof(argv[4])) : 0.025;
    const int target_samples =
        argc > 5 ? std::max(5, std::atoi(argv[5])) : 20;
    const std::string output =
        argc > 6 ? argv[6] : "bdfr_camera_calibration.txt";

    CameraConfig config;
    config.device_index = device_index;
    config.source_id = "calibration_camera";
    config.stream_id = "camera:" + std::to_string(device_index);
    config.width = 1920;
    config.height = 1080;
    config.fps = 30.0;
    config.buffer_frames = 2;

    OpenCVCameraSource camera(config);
    if (!camera.start()) {
        std::cerr << "Failed to open camera " << device_index << "\n";
        return 2;
    }

    const cv::Size pattern_size(columns, rows);
    std::vector<std::vector<cv::Point2f>> image_points;
    cv::Size image_size;
    auto last_accepted = std::chrono::steady_clock::now() -
                         std::chrono::seconds(5);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::minutes(3);

    std::cout << "BDFR camera calibration\n"
              << "Device: " << device_index << "\n"
              << "Pattern: " << columns << "x" << rows
              << " inner corners\n"
              << "Square size: " << square_size << "\n"
              << "Target samples: " << target_samples << "\n";

    while (static_cast<int>(image_points.size()) < target_samples &&
           std::chrono::steady_clock::now() < deadline) {
        auto frame = camera.poll();
        if (!frame || !frame->image.valid()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        auto& image = frame->image;
        cv::Mat bgr(
            image.height,
            image.width,
            CV_8UC3,
            image.bytes->data(),
            static_cast<std::size_t>(image.stride_bytes));

        cv::Mat gray;
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
        image_size = gray.size();

        std::vector<cv::Point2f> corners;
        const bool found = cv::findChessboardCorners(
            gray,
            pattern_size,
            corners,
            cv::CALIB_CB_ADAPTIVE_THRESH |
                cv::CALIB_CB_NORMALIZE_IMAGE |
                cv::CALIB_CB_FAST_CHECK);

        if (!found) continue;

        cv::cornerSubPix(
            gray,
            corners,
            cv::Size(11, 11),
            cv::Size(-1, -1),
            cv::TermCriteria(
                cv::TermCriteria::EPS + cv::TermCriteria::COUNT,
                30,
                0.001));

        const auto now = std::chrono::steady_clock::now();
        if (now - last_accepted < std::chrono::milliseconds(400)) {
            continue;
        }

        image_points.push_back(std::move(corners));
        last_accepted = now;
        std::cout << "Accepted sample "
                  << image_points.size() << "/" << target_samples
                  << "\n";
    }

    camera.stop();

    if (static_cast<int>(image_points.size()) < target_samples) {
        std::cerr << "Calibration timed out with only "
                  << image_points.size() << " valid samples.\n";
        return 3;
    }

    std::vector<cv::Point3f> pattern;
    pattern.reserve(static_cast<std::size_t>(columns * rows));
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            pattern.emplace_back(
                static_cast<float>(x * square_size),
                static_cast<float>(y * square_size),
                0.0F);
        }
    }

    std::vector<std::vector<cv::Point3f>> object_points(
        image_points.size(), pattern);

    cv::Mat camera_matrix = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat distortion;
    std::vector<cv::Mat> rotations;
    std::vector<cv::Mat> translations;

    const double rms = cv::calibrateCamera(
        object_points,
        image_points,
        image_size,
        camera_matrix,
        distortion,
        rotations,
        translations);

    CameraCalibration calibration;
    calibration.camera_id = "opencv:" + std::to_string(device_index);
    calibration.intrinsics.width = image_size.width;
    calibration.intrinsics.height = image_size.height;
    calibration.intrinsics.fx = camera_matrix.at<double>(0, 0);
    calibration.intrinsics.fy = camera_matrix.at<double>(1, 1);
    calibration.intrinsics.cx = camera_matrix.at<double>(0, 2);
    calibration.intrinsics.cy = camera_matrix.at<double>(1, 2);
    calibration.rms_reprojection_error = rms;

    cv::Mat distortion_flat = distortion.reshape(1, 1);
    const int coefficient_count =
        std::min<int>(8, static_cast<int>(distortion_flat.total()));
    for (int i = 0; i < coefficient_count; ++i) {
        calibration.intrinsics.distortion[static_cast<std::size_t>(i)] =
            distortion_flat.at<double>(0, i);
    }

    CalibrationProfile profile;
    if (!profile.upsert(calibration)) {
        std::cerr << "Calibration result failed BDFR validation.\n";
        return 4;
    }

    std::ofstream file(output, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to create " << output << "\n";
        return 5;
    }
    file << profile.serialize();
    file.close();

    std::cout << "Calibration complete\n"
              << "Resolution: " << image_size.width << "x"
              << image_size.height << "\n"
              << "fx/fy: " << calibration.intrinsics.fx << " / "
              << calibration.intrinsics.fy << "\n"
              << "cx/cy: " << calibration.intrinsics.cx << " / "
              << calibration.intrinsics.cy << "\n"
              << "RMS reprojection error: " << rms << "\n"
              << "Saved: " << output << "\n";

    return 0;
}
