#pragma once

#include "bdfrpc/Skeleton.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace bdfrpc {

struct Quaternion {
    float w{1.0F};
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

struct RetargetBonePose {
    std::string bone;
    AxisAngle rotation;
};

struct RetargetPose {
    std::string profile_name;
    TimestampNs timestamp_ns{0};
    std::array<float, 3> root_translation{0.0F, 0.0F, 0.0F};
    std::array<float, 3> root_orientation{0.0F, 0.0F, 0.0F};
    std::vector<RetargetBonePose> bones;
};

struct RetargetRule {
    std::string target_bone;
    // Coordinate-basis transform from canonical joint local axes into the
    // target rig's local axes. Animation delta is conjugated by this basis:
    // target_delta = basis * source_delta * inverse(basis).
    Quaternion basis;
};

class RetargetProfile {
public:
    explicit RetargetProfile(std::string name = {});

    void map_joint(std::string canonical_joint, std::string target_bone);
    void set_joint_basis(
        const std::string& canonical_joint,
        Quaternion basis);

    const std::string* target_for(
        const std::string& canonical_joint) const noexcept;
    const RetargetRule* rule_for(
        const std::string& canonical_joint) const noexcept;

    void set_root_basis(Quaternion basis);
    Quaternion root_basis() const noexcept { return root_basis_; }

    void set_translation_scale(float scale) noexcept;
    float translation_scale() const noexcept { return translation_scale_; }

    const std::string& name() const noexcept { return name_; }

    static RetargetProfile unreal_mannequin();
    static RetargetProfile metahuman_body();

private:
    std::string name_;
    std::unordered_map<std::string, RetargetRule> mappings_;
    Quaternion root_basis_;
    float translation_scale_{1.0F};
};

Quaternion normalize_quaternion(Quaternion q) noexcept;
Quaternion multiply_quaternion(
    const Quaternion& a,
    const Quaternion& b) noexcept;
Quaternion inverse_quaternion(
    const Quaternion& q) noexcept;
Quaternion axis_angle_to_quaternion(
    const AxisAngle& value) noexcept;
AxisAngle quaternion_to_axis_angle(
    const Quaternion& value) noexcept;

AxisAngle transform_rotation_basis(
    const AxisAngle& source,
    const Quaternion& basis) noexcept;

std::array<float,3> transform_vector_basis(
    const std::array<float,3>& source,
    const Quaternion& basis) noexcept;

RetargetPose retarget_skeleton(
    const SkeletonPose& source,
    const RetargetProfile& profile);

} // namespace bdfrpc
