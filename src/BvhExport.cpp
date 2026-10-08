#include "bdfrpc/BvhExport.h"

#include "bdfrpc/Kinematics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace bdfrpc {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct Mat3 {
    std::array<double,9> m{
        1,0,0,
        0,1,0,
        0,0,1
    };
};

Mat3 axis_angle_matrix(float x, float y, float z) {
    const double dx = static_cast<double>(x);
    const double dy = static_cast<double>(y);
    const double dz = static_cast<double>(z);
    const double theta = std::sqrt(dx*dx + dy*dy + dz*dz);

    if (theta < 1e-12) return {};

    const double ax = dx / theta;
    const double ay = dy / theta;
    const double az = dz / theta;
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    const double t = 1.0 - c;

    Mat3 r;
    r.m = {
        t*ax*ax + c,      t*ax*ay - s*az,  t*ax*az + s*ay,
        t*ax*ay + s*az,   t*ay*ay + c,     t*ay*az - s*ax,
        t*ax*az - s*ay,   t*ay*az + s*ax,  t*az*az + c
    };
    return r;
}

Mat3 multiply(const Mat3& a, const Mat3& b) {
    Mat3 out;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            double value = 0.0;
            for (int k = 0; k < 3; ++k) {
                value +=
                    a.m[row*3+k] * b.m[k*3+col];
            }
            out.m[row*3+col] = value;
        }
    }
    return out;
}

std::array<double,3> matrix_to_xyz_degrees(const Mat3& r) {
    // Decomposition for R = Rx * Ry * Rz, matching BVH channel order
    // Xrotation Yrotation Zrotation.
    const double sy = std::clamp(r.m[2], -1.0, 1.0);
    const double y = std::asin(sy);
    const double cy = std::cos(y);

    double x = 0.0;
    double z = 0.0;

    if (std::abs(cy) > 1e-8) {
        x = std::atan2(-r.m[5], r.m[8]);
        z = std::atan2(-r.m[1], r.m[0]);
    } else {
        // Gimbal-lock fallback: preserve X and collapse Z.
        x = std::atan2(r.m[7], r.m[4]);
        z = 0.0;
    }

    constexpr double to_deg = 180.0 / kPi;
    return {x * to_deg, y * to_deg, z * to_deg};
}

std::array<double,3> local_euler(const JointPose& joint) {
    return matrix_to_xyz_degrees(
        axis_angle_matrix(
            joint.rotation.x,
            joint.rotation.y,
            joint.rotation.z));
}

std::array<double,3> root_euler(const SkeletonPose& pose) {
    Mat3 global = axis_angle_matrix(
        pose.global_orientation[0],
        pose.global_orientation[1],
        pose.global_orientation[2]);

    if (!pose.joints.empty()) {
        const auto& local = pose.joints.front().rotation;
        global = multiply(
            global,
            axis_angle_matrix(local.x, local.y, local.z));
    }

    return matrix_to_xyz_degrees(global);
}

std::vector<std::vector<int>> children_for(
    const SkeletonPose& pose) {

    std::vector<std::vector<int>> children(pose.joints.size());
    for (std::size_t i = 0; i < pose.joints.size(); ++i) {
        const int parent = pose.joints[i].parent;
        if (parent >= 0 &&
            static_cast<std::size_t>(parent) < pose.joints.size()) {
            children[static_cast<std::size_t>(parent)]
                .push_back(static_cast<int>(i));
        }
    }
    return children;
}

void indent(std::ofstream& out, int depth) {
    for (int i = 0; i < depth; ++i) out << "  ";
}

void write_joint_hierarchy(
    std::ofstream& out,
    const SkeletonPose& pose,
    const std::vector<std::vector<int>>& children,
    int index,
    int depth,
    float scale,
    std::vector<int>& traversal) {

    const auto& joint = pose.joints[static_cast<std::size_t>(index)];
    traversal.push_back(index);

    indent(out, depth);
    out << (joint.parent < 0 ? "ROOT " : "JOINT ")
        << joint.name << "\n";
    indent(out, depth);
    out << "{\n";

    const auto offset =
        joint.parent < 0
            ? std::array<float,3>{0.0F,0.0F,0.0F}
            : canonical_rest_offset(joint.name);

    indent(out, depth + 1);
    out << std::fixed << std::setprecision(6)
        << "OFFSET "
        << offset[0] * scale << ' '
        << offset[1] * scale << ' '
        << offset[2] * scale << "\n";

    indent(out, depth + 1);
    if (joint.parent < 0) {
        out << "CHANNELS 6 "
            << "Xposition Yposition Zposition "
            << "Xrotation Yrotation Zrotation\n";
    } else {
        out << "CHANNELS 3 "
            << "Xrotation Yrotation Zrotation\n";
    }

    const auto& child_list =
        children[static_cast<std::size_t>(index)];

    for (const int child : child_list) {
        write_joint_hierarchy(
            out,
            pose,
            children,
            child,
            depth + 1,
            scale,
            traversal);
    }

    if (child_list.empty()) {
        indent(out, depth + 1);
        out << "End Site\n";
        indent(out, depth + 1);
        out << "{\n";
        indent(out, depth + 2);
        out << "OFFSET 0.000000 2.000000 0.000000\n";
        indent(out, depth + 1);
        out << "}\n";
    }

    indent(out, depth);
    out << "}\n";
}

bool same_topology(
    const SkeletonPose& a,
    const SkeletonPose& b) {

    if (a.joints.size() != b.joints.size()) return false;
    for (std::size_t i = 0; i < a.joints.size(); ++i) {
        if (a.joints[i].name != b.joints[i].name ||
            a.joints[i].parent != b.joints[i].parent) {
            return false;
        }
    }
    return true;
}

double inferred_frame_rate(
    const std::vector<SkeletonPose>& poses) {

    if (poses.size() < 2) return 30.0;

    std::vector<TimestampNs> deltas;
    deltas.reserve(poses.size() - 1);

    for (std::size_t i = 1; i < poses.size(); ++i) {
        const auto delta =
            poses[i].timestamp_ns - poses[i - 1].timestamp_ns;
        if (delta > 0) deltas.push_back(delta);
    }

    if (deltas.empty()) return 30.0;

    const auto middle =
        deltas.begin() + static_cast<std::ptrdiff_t>(deltas.size() / 2);
    std::nth_element(deltas.begin(), middle, deltas.end());
    const double seconds =
        static_cast<double>(*middle) / 1.0e9;

    if (seconds <= 0.0) return 30.0;
    return std::clamp(1.0 / seconds, 1.0, 240.0);
}

} // namespace

std::vector<SkeletonPose> extract_skeleton_sequence(
    const std::vector<FusedPerformanceFrame>& frames,
    int subject_id) {

    std::vector<SkeletonPose> out;
    out.reserve(frames.size());

    for (const auto& fused : frames) {
        CaptureFrame frame;
        frame.source_id = "take";
        frame.stream_id = "take";
        frame.timestamp_ns = fused.timestamp_ns;

        for (const auto& sample : fused.samples) {
            if (sample.domain == Domain::Body ||
                sample.domain == Domain::Face) {
                frame.samples.push_back(sample);
            }
        }

        auto skeleton =
            EasyMocapSkeletonMapper::map_subject(
                frame,
                subject_id);
        if (skeleton) out.push_back(std::move(*skeleton));
    }

    return out;
}

bool export_bvh(
    const std::string& path,
    const std::vector<SkeletonPose>& poses,
    const BvhExportOptions& options,
    std::string* error) {

    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        return false;
    };

    if (poses.empty()) {
        return fail("no skeleton frames to export");
    }
    if (poses.front().joints.empty()) {
        return fail("first skeleton frame has no joints");
    }

    for (const auto& pose : poses) {
        if (!same_topology(poses.front(), pose)) {
            return fail("skeleton topology changes between frames");
        }
    }

    int root_index = -1;
    for (std::size_t i = 0; i < poses.front().joints.size(); ++i) {
        if (poses.front().joints[i].parent < 0) {
            root_index = static_cast<int>(i);
            break;
        }
    }
    if (root_index < 0) {
        return fail("skeleton has no root joint");
    }

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) return fail("unable to create BVH file");

    out << "HIERARCHY\n";

    const auto children = children_for(poses.front());
    std::vector<int> traversal;
    write_joint_hierarchy(
        out,
        poses.front(),
        children,
        root_index,
        0,
        options.position_scale,
        traversal);

    const double frame_rate =
        options.frame_rate > 0.0
            ? options.frame_rate
            : inferred_frame_rate(poses);

    out << "MOTION\n";
    out << "Frames: " << poses.size() << "\n";
    out << std::fixed << std::setprecision(9)
        << "Frame Time: " << (1.0 / frame_rate) << "\n";

    for (const auto& pose : poses) {
        bool first = true;
        for (const int index : traversal) {
            const auto& joint =
                pose.joints[static_cast<std::size_t>(index)];

            auto emit = [&](double value) {
                if (!first) out << ' ';
                first = false;
                out << std::setprecision(7) << value;
            };

            if (joint.parent < 0) {
                emit(
                    static_cast<double>(pose.translation[0]) *
                    options.position_scale);
                emit(
                    static_cast<double>(pose.translation[1]) *
                    options.position_scale);
                emit(
                    static_cast<double>(pose.translation[2]) *
                    options.position_scale);

                const auto euler = root_euler(pose);
                emit(euler[0]);
                emit(euler[1]);
                emit(euler[2]);
            } else {
                const auto euler = local_euler(joint);
                emit(euler[0]);
                emit(euler[1]);
                emit(euler[2]);
            }
        }
        out << "\n";
    }

    if (!out) return fail("BVH write failed");
    if (error) error->clear();
    return true;
}

} // namespace bdfrpc
