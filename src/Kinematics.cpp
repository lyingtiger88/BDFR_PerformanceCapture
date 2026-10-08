#include "bdfrpc/Kinematics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace bdfrpc {
namespace {

struct Mat3 {
    std::array<float, 9> m{
        1,0,0,
        0,1,0,
        0,0,1
    };
};

Mat3 multiply(const Mat3& a, const Mat3& b) {
    Mat3 out;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            float value = 0.0F;
            for (int k = 0; k < 3; ++k) {
                value += a.m[r * 3 + k] * b.m[k * 3 + c];
            }
            out.m[r * 3 + c] = value;
        }
    }
    return out;
}

std::array<float, 3> transform(
    const Mat3& r,
    const std::array<float, 3>& v) {

    return {
        r.m[0] * v[0] + r.m[1] * v[1] + r.m[2] * v[2],
        r.m[3] * v[0] + r.m[4] * v[1] + r.m[5] * v[2],
        r.m[6] * v[0] + r.m[7] * v[1] + r.m[8] * v[2]
    };
}

std::array<float, 3> add(
    const std::array<float, 3>& a,
    const std::array<float, 3>& b) {
    return {a[0]+b[0], a[1]+b[1], a[2]+b[2]};
}

Mat3 axis_angle(const AxisAngle& a) {
    const float theta =
        std::sqrt(a.x*a.x + a.y*a.y + a.z*a.z);

    if (theta < 1e-8F) return {};

    const float x = a.x / theta;
    const float y = a.y / theta;
    const float z = a.z / theta;
    const float c = std::cos(theta);
    const float s = std::sin(theta);
    const float t = 1.0F - c;

    Mat3 r;
    r.m = {
        t*x*x + c,     t*x*y - s*z,   t*x*z + s*y,
        t*x*y + s*z,   t*y*y + c,     t*y*z - s*x,
        t*x*z - s*y,   t*y*z + s*x,   t*z*z + c
    };
    return r;
}

std::array<float, 3> rest_offset_impl(const std::string& name) {
    static const std::unordered_map<std::string, std::array<float,3>> offsets = {
        {"pelvis", {0.0F, 0.0F, 0.0F}},
        {"left_hip", {-0.09F, -0.09F, 0.0F}},
        {"right_hip", {0.09F, -0.09F, 0.0F}},
        {"spine1", {0.0F, 0.13F, 0.0F}},
        {"left_knee", {0.0F, -0.42F, 0.0F}},
        {"right_knee", {0.0F, -0.42F, 0.0F}},
        {"spine2", {0.0F, 0.14F, 0.0F}},
        {"left_ankle", {0.0F, -0.41F, 0.0F}},
        {"right_ankle", {0.0F, -0.41F, 0.0F}},
        {"spine3", {0.0F, 0.14F, 0.0F}},
        {"left_foot", {0.0F, -0.05F, 0.13F}},
        {"right_foot", {0.0F, -0.05F, 0.13F}},
        {"neck", {0.0F, 0.16F, 0.0F}},
        {"left_collar", {-0.08F, 0.07F, 0.0F}},
        {"right_collar", {0.08F, 0.07F, 0.0F}},
        {"head", {0.0F, 0.17F, 0.0F}},
        {"left_shoulder", {-0.15F, 0.0F, 0.0F}},
        {"right_shoulder", {0.15F, 0.0F, 0.0F}},
        {"left_elbow", {-0.27F, 0.0F, 0.0F}},
        {"right_elbow", {0.27F, 0.0F, 0.0F}},
        {"left_wrist", {-0.25F, 0.0F, 0.0F}},
        {"right_wrist", {0.25F, 0.0F, 0.0F}},
        {"left_hand", {-0.10F, 0.0F, 0.0F}},
        {"right_hand", {0.10F, 0.0F, 0.0F}},
        {"jaw", {0.0F, -0.055F, 0.045F}},
        {"left_eye", {-0.032F, 0.035F, 0.075F}},
        {"right_eye", {0.032F, 0.035F, 0.075F}},
    };

    const auto it = offsets.find(name);
    if (it != offsets.end()) return it->second;

    const bool left = name.rfind("left_", 0) == 0;
    const bool right = name.rfind("right_", 0) == 0;
    if (left || right) {
        if (name.find("thumb") != std::string::npos) {
            return {left ? -0.035F : 0.035F, -0.015F, 0.02F};
        }
        return {left ? -0.035F : 0.035F, 0.0F, 0.0F};
    }

    return {0.0F, 0.0F, 0.0F};
}

} // namespace

std::array<float, 3> canonical_rest_offset(
    const std::string& joint_name) {
    return rest_offset_impl(joint_name);
}

const KinematicJoint* KinematicPose::find_joint(
    const std::string& name) const noexcept {

    for (const auto& joint : joints) {
        if (joint.name == name) return &joint;
    }
    return nullptr;
}

KinematicPose SkeletonKinematics::solve(const SkeletonPose& pose) {
    KinematicPose out;
    out.timestamp_ns = pose.timestamp_ns;
    out.joints.reserve(pose.joints.size());

    std::vector<Mat3> global_rotations;
    global_rotations.reserve(pose.joints.size());

    const AxisAngle global_axis{
        pose.global_orientation[0],
        pose.global_orientation[1],
        pose.global_orientation[2]
    };
    const Mat3 root_rotation = axis_angle(global_axis);

    for (std::size_t i = 0; i < pose.joints.size(); ++i) {
        const auto& source = pose.joints[i];
        const Mat3 local_rotation = axis_angle(source.rotation);

        KinematicJoint joint;
        joint.name = source.name;
        joint.parent = source.parent;

        if (source.parent < 0 ||
            static_cast<std::size_t>(source.parent) >= out.joints.size()) {
            joint.position = pose.translation;
            global_rotations.push_back(
                multiply(root_rotation, local_rotation));
        } else {
            const auto parent = static_cast<std::size_t>(source.parent);
            const auto offset = canonical_rest_offset(source.name);
            const auto rotated_offset =
                transform(global_rotations[parent], offset);
            joint.position =
                add(out.joints[parent].position, rotated_offset);
            global_rotations.push_back(
                multiply(global_rotations[parent], local_rotation));
        }

        out.joints.push_back(std::move(joint));
    }

    return out;
}

} // namespace bdfrpc
