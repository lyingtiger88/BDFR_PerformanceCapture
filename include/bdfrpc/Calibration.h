#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace bdfrpc {

struct Point2d {
    double x{0.0};
    double y{0.0};
};

struct Point3d {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct CameraIntrinsics {
    int width{0};
    int height{0};
    double fx{0.0};
    double fy{0.0};
    double cx{0.0};
    double cy{0.0};
    // k1, k2, p1, p2, k3, k4, k5, k6
    std::array<double, 8> distortion{};

    bool valid() const noexcept;
};

struct CameraExtrinsics {
    // World-to-camera rotation, row-major 3x3.
    std::array<double, 9> rotation{
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0
    };
    std::array<double, 3> translation{0.0, 0.0, 0.0};
};

struct CameraCalibration {
    std::string camera_id;
    CameraIntrinsics intrinsics;
    CameraExtrinsics extrinsics;
    double rms_reprojection_error{0.0};

    bool valid() const noexcept;
};

class CalibrationProfile {
public:
    bool upsert(CameraCalibration camera);
    bool erase(const std::string& camera_id);
    const CameraCalibration* find(const std::string& camera_id) const noexcept;
    const std::vector<CameraCalibration>& cameras() const noexcept;

    std::string serialize() const;
    static std::optional<CalibrationProfile> deserialize(
        const std::string& text,
        std::string* error = nullptr);

private:
    std::vector<CameraCalibration> cameras_;
};

std::optional<Point2d> project_point(
    const CameraCalibration& camera,
    const Point3d& world_point);

double reprojection_rmse(
    const CameraCalibration& camera,
    const std::vector<Point3d>& world_points,
    const std::vector<Point2d>& image_points);

} // namespace bdfrpc
