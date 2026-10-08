#include "bdfrpc/Retarget.h"

#include <utility>

namespace bdfrpc {

RetargetProfile::RetargetProfile(std::string name)
    : name_(std::move(name)) {}

void RetargetProfile::map_joint(
    std::string canonical_joint,
    std::string target_bone) {

    if (canonical_joint.empty() || target_bone.empty()) return;
    mappings_[std::move(canonical_joint)] = std::move(target_bone);
}

const std::string* RetargetProfile::target_for(
    const std::string& canonical_joint) const noexcept {

    const auto it = mappings_.find(canonical_joint);
    if (it == mappings_.end()) return nullptr;
    return &it->second;
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
    // MetaHuman body skeleton retains the standard UE body bone names for
    // the core limbs/spine. Facial RigLogic remains a separate curve path.
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
    out.root_translation = source.translation;
    out.root_orientation = source.global_orientation;

    for (const auto& joint : source.joints) {
        const auto* target = profile.target_for(joint.name);
        if (!target) continue;
        out.bones.push_back({*target, joint.rotation});
    }
    return out;
}

} // namespace bdfrpc
