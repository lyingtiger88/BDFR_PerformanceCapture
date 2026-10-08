#include "bdfrpc/Retarget.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace bdfrpc {
namespace {

constexpr float kEpsilon = 1e-8F;
constexpr float kPi = 3.14159265358979323846F;

float length_squared(const Quaternion& q) noexcept {
    return q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z;
}

} // namespace

Quaternion normalize_quaternion(Quaternion q) noexcept {
    const float length2 = length_squared(q);
    if (!std::isfinite(length2) || length2 <= kEpsilon) {
        return {};
    }

    const float inv = 1.0F / std::sqrt(length2);
    q.w *= inv;
    q.x *= inv;
    q.y *= inv;
    q.z *= inv;

    // q and -q represent the same rotation; choose one hemisphere so
    // conversion back to axis-angle is deterministic.
    if (q.w < 0.0F) {
        q.w = -q.w;
        q.x = -q.x;
        q.y = -q.y;
        q.z = -q.z;
    }
    return q;
}

Quaternion multiply_quaternion(
    const Quaternion& a,
    const Quaternion& b) noexcept {

    return normalize_quaternion({
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w
    });
}

Quaternion inverse_quaternion(
    const Quaternion& q) noexcept {

    const auto n = normalize_quaternion(q);
    return {n.w, -n.x, -n.y, -n.z};
}

Quaternion axis_angle_to_quaternion(
    const AxisAngle& value) noexcept {

    const float angle = std::sqrt(
        value.x*value.x +
        value.y*value.y +
        value.z*value.z);

    if (!std::isfinite(angle) || angle <= kEpsilon) {
        return {};
    }

    const float half = angle * 0.5F;
    const float scale = std::sin(half) / angle;

    return normalize_quaternion({
        std::cos(half),
        value.x * scale,
        value.y * scale,
        value.z * scale
    });
}

AxisAngle quaternion_to_axis_angle(
    const Quaternion& value) noexcept {

    const auto q = normalize_quaternion(value);
    const float w = std::clamp(q.w, -1.0F, 1.0F);
    float angle = 2.0F * std::acos(w);

    if (angle > kPi) angle -= 2.0F * kPi;

    const float sin_half =
        std::sqrt(std::max(0.0F, 1.0F - w*w));

    if (sin_half <= kEpsilon || std::abs(angle) <= kEpsilon) {
        return {};
    }

    const float scale = angle / sin_half;
    return {
        q.x * scale,
        q.y * scale,
        q.z * scale
    };
}

AxisAngle transform_rotation_basis(
    const AxisAngle& source,
    const Quaternion& basis) noexcept {

    const auto b = normalize_quaternion(basis);
    const auto source_q =
        axis_angle_to_quaternion(source);

    const auto target_q =
        multiply_quaternion(
            multiply_quaternion(b, source_q),
            inverse_quaternion(b));

    return quaternion_to_axis_angle(target_q);
}

std::array<float,3> transform_vector_basis(
    const std::array<float,3>& source,
    const Quaternion& basis) noexcept {

    const auto b = normalize_quaternion(basis);
    const Quaternion vector{
        0.0F,
        source[0],
        source[1],
        source[2]
    };

    // Do not use multiply_quaternion for the vector term: that helper
    // normalizes rotations. Expand q*v*q^-1 directly.
    const auto inv = inverse_quaternion(b);

    const Quaternion t{
        b.w*vector.w - b.x*vector.x - b.y*vector.y - b.z*vector.z,
        b.w*vector.x + b.x*vector.w + b.y*vector.z - b.z*vector.y,
        b.w*vector.y - b.x*vector.z + b.y*vector.w + b.z*vector.x,
        b.w*vector.z + b.x*vector.y - b.y*vector.x + b.z*vector.w
    };

    const Quaternion out{
        t.w*inv.w - t.x*inv.x - t.y*inv.y - t.z*inv.z,
        t.w*inv.x + t.x*inv.w + t.y*inv.z - t.z*inv.y,
        t.w*inv.y - t.x*inv.z + t.y*inv.w + t.z*inv.x,
        t.w*inv.z + t.x*inv.y - t.y*inv.x + t.z*inv.w
    };

    return {out.x, out.y, out.z};
}

RetargetProfile::RetargetProfile(std::string name)
    : name_(std::move(name)) {}

void RetargetProfile::map_joint(
    std::string canonical_joint,
    std::string target_bone) {

    if (canonical_joint.empty() || target_bone.empty()) return;

    auto& rule = mappings_[std::move(canonical_joint)];
    rule.target_bone = std::move(target_bone);
}

void RetargetProfile::set_joint_basis(
    const std::string& canonical_joint,
    Quaternion basis) {

    if (canonical_joint.empty()) return;
    auto& rule = mappings_[canonical_joint];
    rule.basis = normalize_quaternion(basis);
}

const std::string* RetargetProfile::target_for(
    const std::string& canonical_joint) const noexcept {

    const auto* rule = rule_for(canonical_joint);
    if (!rule || rule->target_bone.empty()) return nullptr;
    return &rule->target_bone;
}

const RetargetRule* RetargetProfile::rule_for(
    const std::string& canonical_joint) const noexcept {

    const auto it = mappings_.find(canonical_joint);
    if (it == mappings_.end()) return nullptr;
    return &it->second;
}

void RetargetProfile::set_root_basis(Quaternion basis) {
    root_basis_ = normalize_quaternion(basis);
}

void RetargetProfile::set_translation_scale(float scale) noexcept {
    if (std::isfinite(scale) && scale > 0.0F) {
        translation_scale_ = scale;
    }
}

RetargetProfile RetargetProfile::unreal_mannequin() {
    RetargetProfile p("Unreal Mannequin");

    const std::pair<const char*, const char*> mappings[] = {
        {"pelvis", "pelvis"},
        {"left_hip", "thigh_l"},
        {"right_hip", "thigh_r"},
        {"left_knee", "calf_l"},
        {"right_knee", "calf_r"},
        {"left_ankle", "foot_l"},
        {"right_ankle", "foot_r"},
        {"left_foot", "ball_l"},
        {"right_foot", "ball_r"},
        {"spine1", "spine_01"},
        {"spine2", "spine_02"},
        {"spine3", "spine_03"},
        {"neck", "neck_01"},
        {"head", "head"},
        {"left_collar", "clavicle_l"},
        {"right_collar", "clavicle_r"},
        {"left_shoulder", "upperarm_l"},
        {"right_shoulder", "upperarm_r"},
        {"left_elbow", "lowerarm_l"},
        {"right_elbow", "lowerarm_r"},
        {"left_wrist", "hand_l"},
        {"right_wrist", "hand_r"},
    };

    for (const auto& [source, target] : mappings) {
        p.map_joint(source, target);
    }
    return p;
}

RetargetProfile RetargetProfile::metahuman_body() {
    auto p = unreal_mannequin();
    RetargetProfile out("MetaHuman Body");

    const char* joints[] = {
        "pelvis",
        "left_hip", "right_hip",
        "left_knee", "right_knee",
        "left_ankle", "right_ankle",
        "left_foot", "right_foot",
        "spine1", "spine2", "spine3",
        "neck", "head",
        "left_collar", "right_collar",
        "left_shoulder", "right_shoulder",
        "left_elbow", "right_elbow",
        "left_wrist", "right_wrist"
    };

    for (const char* joint : joints) {
        if (const auto* target = p.target_for(joint)) {
            out.map_joint(joint, *target);
        }
    }
    return out;
}

RetargetPose retarget_skeleton(
    const SkeletonPose& source,
    const RetargetProfile& profile) {

    RetargetPose out;
    out.profile_name = profile.name();
    out.timestamp_ns = source.timestamp_ns;

    auto translation =
        transform_vector_basis(
            source.translation,
            profile.root_basis());
    for (float& value : translation) {
        value *= profile.translation_scale();
    }
    out.root_translation = translation;

    out.root_orientation =
        [&]() {
            const AxisAngle root{
                source.global_orientation[0],
                source.global_orientation[1],
                source.global_orientation[2]
            };
            const auto transformed =
                transform_rotation_basis(
                    root,
                    profile.root_basis());
            return std::array<float,3>{
                transformed.x,
                transformed.y,
                transformed.z
            };
        }();

    for (const auto& joint : source.joints) {
        const auto* rule = profile.rule_for(joint.name);
        if (!rule || rule->target_bone.empty()) continue;

        out.bones.push_back({
            rule->target_bone,
            transform_rotation_basis(
                joint.rotation,
                rule->basis)
        });
    }
    return out;
}

} // namespace bdfrpc
