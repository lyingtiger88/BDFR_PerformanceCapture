#pragma once

#include "bdfrpc/Skeleton.h"

#include <array>
#include <string>
#include <vector>

namespace bdfrpc {

struct KinematicJoint {
    std::string name;
    int parent{-1};
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
};

struct KinematicPose {
    TimestampNs timestamp_ns{0};
    std::vector<KinematicJoint> joints;

    const KinematicJoint* find_joint(const std::string& name) const noexcept;
};

std::array<float, 3> canonical_rest_offset(
    const std::string& joint_name);

class SkeletonKinematics {
public:
    // Produces a visualization/preview pose from canonical local rotations.
    // Bone lengths are canonical preview proportions rather than subject-specific
    // SMPL shape-derived lengths, so this is deterministic and model-independent.
    static KinematicPose solve(const SkeletonPose& pose);
};

} // namespace bdfrpc
