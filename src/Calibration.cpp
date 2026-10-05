#include "bdfrpc/Calibration.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace bdfrpc {

bool CameraIntrinsics::valid() const noexcept {
    return width > 0 && height > 0 &&
           std::isfinite(fx) && std::isfinite(fy) &&
           std::isfinite(cx) && std::isfinite(cy) &&
           fx > 0.0 && fy > 0.0;
}

bool CameraCalibration::valid() const noexcept {
    return !camera_id.empty() && intrinsics.valid() &&
           std::isfinite(rms_reprojection_error) &&
           rms_reprojection_error >= 0.0;
}

bool CalibrationProfile::upsert(CameraCalibration camera) {
    if (!camera.valid()) return false;
    for (auto& existing : cameras_) {
        if (existing.camera_id == camera.camera_id) {
            existing = std::move(camera);
            return true;
        }
    }
    cameras_.push_back(std::move(camera));
    std::sort(cameras_.begin(), cameras_.end(),
        [](const CameraCalibration& a, const CameraCalibration& b) {
            return a.camera_id < b.camera_id;
        });
    return true;
}

bool CalibrationProfile::erase(const std::string& camera_id) {
    const auto old = cameras_.size();
    cameras_.erase(
        std::remove_if(cameras_.begin(), cameras_.end(),
            [&](const CameraCalibration& item) {
                return item.camera_id == camera_id;
            }),
        cameras_.end());
    return cameras_.size() != old;
}

const CameraCalibration* CalibrationProfile::find(
    const std::string& camera_id) const noexcept {
    for (const auto& item : cameras_) {
        if (item.camera_id == camera_id) return &item;
    }
    return nullptr;
}

const std::vector<CameraCalibration>& CalibrationProfile::cameras() const noexcept {
    return cameras_;
}

std::string CalibrationProfile::serialize() const {
    std::ostringstream out;
    out << std::setprecision(17);
    out << "BDFRPC_CALIBRATION_V1\n";
    for (const auto& c : cameras_) {
        out << "camera|" << c.camera_id << "|"
            << c.intrinsics.width << "|" << c.intrinsics.height << "|"
            << c.intrinsics.fx << "|" << c.intrinsics.fy << "|"
            << c.intrinsics.cx << "|" << c.intrinsics.cy << "|"
            << c.rms_reprojection_error << "\n";

        out << "dist|" << c.camera_id;
        for (const double v : c.intrinsics.distortion) out << "|" << v;
        out << "\n";

        out << "pose|" << c.camera_id;
        for (const double v : c.extrinsics.rotation) out << "|" << v;
        for (const double v : c.extrinsics.translation) out << "|" << v;
        out << "\n";
    }
    return out.str();
}

namespace {

std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> fields;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, delimiter)) fields.push_back(item);
    return fields;
}

bool parse_double(const std::string& text, double& value) {
    try {
        std::size_t used = 0;
        value = std::stod(text, &used);
        return used == text.size() && std::isfinite(value);
    } catch (...) {
        return false;
    }
}

bool parse_int(const std::string& text, int& value) {
    try {
        std::size_t used = 0;
        value = std::stoi(text, &used);
        return used == text.size();
    } catch (...) {
        return false;
    }
}

} // namespace

std::optional<CalibrationProfile> CalibrationProfile::deserialize(
    const std::string& text,
    std::string* error) {

    auto fail = [&](const std::string& message)
        -> std::optional<CalibrationProfile> {
        if (error) *error = message;
        return std::nullopt;
    };

    std::stringstream input(text);
    std::string line;
    if (!std::getline(input, line) || line != "BDFRPC_CALIBRATION_V1") {
        return fail("missing or unsupported calibration header");
    }

    CalibrationProfile profile;
    std::vector<CameraCalibration> pending;

    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const auto fields = split(line, '|');
        if (fields.empty()) continue;

        if (fields[0] == "camera") {
            if (fields.size() != 9 || fields[1].empty()) {
                return fail("invalid camera calibration record");
            }
            CameraCalibration c;
            c.camera_id = fields[1];
            if (!parse_int(fields[2], c.intrinsics.width) ||
                !parse_int(fields[3], c.intrinsics.height) ||
                !parse_double(fields[4], c.intrinsics.fx) ||
                !parse_double(fields[5], c.intrinsics.fy) ||
                !parse_double(fields[6], c.intrinsics.cx) ||
                !parse_double(fields[7], c.intrinsics.cy) ||
                !parse_double(fields[8], c.rms_reprojection_error)) {
                return fail("invalid numeric value in camera record");
            }
            pending.push_back(std::move(c));
        } else if (fields[0] == "dist") {
            if (fields.size() != 10) return fail("invalid distortion record");
            auto it = std::find_if(pending.begin(), pending.end(),
                [&](const CameraCalibration& c) { return c.camera_id == fields[1]; });
            if (it == pending.end()) return fail("distortion references unknown camera");
            for (std::size_t i = 0; i < 8; ++i) {
                if (!parse_double(fields[i + 2], it->intrinsics.distortion[i])) {
                    return fail("invalid distortion coefficient");
                }
            }
        } else if (fields[0] == "pose") {
            if (fields.size() != 14) return fail("invalid pose record");
            auto it = std::find_if(pending.begin(), pending.end(),
                [&](const CameraCalibration& c) { return c.camera_id == fields[1]; });
            if (it == pending.end()) return fail("pose references unknown camera");
            for (std::size_t i = 0; i < 9; ++i) {
                if (!parse_double(fields[i + 2], it->extrinsics.rotation[i])) {
                    return fail("invalid rotation value");
                }
            }
            for (std::size_t i = 0; i < 3; ++i) {
                if (!parse_double(fields[i + 11], it->extrinsics.translation[i])) {
                    return fail("invalid translation value");
                }
            }
        } else {
            return fail("unknown calibration record type");
        }
    }

    for (auto& c : pending) {
        if (!profile.upsert(std::move(c))) {
            return fail("calibration profile contains an invalid camera");
        }
    }

    if (error) error->clear();
    return profile;
}

std::optional<Point2d> project_point(
    const CameraCalibration& camera,
    const Point3d& p) {

    if (!camera.valid()) return std::nullopt;

    const auto& r = camera.extrinsics.rotation;
    const auto& t = camera.extrinsics.translation;
    const double x =
        r[0] * p.x + r[1] * p.y + r[2] * p.z + t[0];
    const double y =
        r[3] * p.x + r[4] * p.y + r[5] * p.z + t[1];
    const double z =
        r[6] * p.x + r[7] * p.y + r[8] * p.z + t[2];

    if (!std::isfinite(z) || z <= 1e-9) return std::nullopt;

    double xn = x / z;
    double yn = y / z;

    const auto& d = camera.intrinsics.distortion;
    const double r2 = xn * xn + yn * yn;
    const double r4 = r2 * r2;
    const double r6 = r4 * r2;

    const double radial_num = 1.0 + d[0] * r2 + d[1] * r4 + d[4] * r6;
    const double radial_den = 1.0 + d[5] * r2 + d[6] * r4 + d[7] * r6;
    const double radial =
        std::abs(radial_den) > 1e-12 ? radial_num / radial_den : radial_num;

    const double x_dist =
        xn * radial + 2.0 * d[2] * xn * yn + d[3] * (r2 + 2.0 * xn * xn);
    const double y_dist =
        yn * radial + d[2] * (r2 + 2.0 * yn * yn) + 2.0 * d[3] * xn * yn;

    return Point2d{
        camera.intrinsics.fx * x_dist + camera.intrinsics.cx,
        camera.intrinsics.fy * y_dist + camera.intrinsics.cy
    };
}

double reprojection_rmse(
    const CameraCalibration& camera,
    const std::vector<Point3d>& world_points,
    const std::vector<Point2d>& image_points) {

    if (world_points.empty() || world_points.size() != image_points.size()) {
        return std::numeric_limits<double>::infinity();
    }

    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < world_points.size(); ++i) {
        const auto projected = project_point(camera, world_points[i]);
        if (!projected) continue;
        const double dx = projected->x - image_points[i].x;
        const double dy = projected->y - image_points[i].y;
        sum += dx * dx + dy * dy;
        ++count;
    }

    if (count == 0) return std::numeric_limits<double>::infinity();
    return std::sqrt(sum / static_cast<double>(count));
}

} // namespace bdfrpc
