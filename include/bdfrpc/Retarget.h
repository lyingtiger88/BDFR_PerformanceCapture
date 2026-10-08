#pragma once

#include "bdfrpc/Skeleton.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace bdfrpc {

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

class RetargetProfile {
public:
    explicit RetargetProfile(std::string name = {});

    void map_joint(std::string canonical_joint, std::string target_bone);
    const std::string* target_for(const std::string& canonical_joint) const noexcept;
    const std::string& name() const noexcept { return name_; }

    static RetargetProfile unreal_mannequin();
    static RetargetProfile metahuman_body();

private:
    std::string name_;
    std::unordered_map<std::string, std::string> mappings_;
};

RetargetPose retarget_skeleton(
    const SkeletonPose& source,
    const RetargetProfile& profile);

} // namespace bdfrpc
