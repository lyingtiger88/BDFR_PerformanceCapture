#pragma once

#include "bdfrpc/Types.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bdfrpc {

enum class SkeletonModel {
    Unknown,
    SMPL24,
    SMPLXCompact87,
    SMPLXFull55
};

struct AxisAngle {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

struct JointPose {
    std::string name;
    int parent{-1};
    AxisAngle rotation;
};

struct SkeletonPose {
    SkeletonModel model{SkeletonModel::Unknown};
    int subject_id{0};
    TimestampNs timestamp_ns{0};
    float confidence{0.0F};

    std::array<float, 3> global_orientation{0.0F, 0.0F, 0.0F};
    std::array<float, 3> translation{0.0F, 0.0F, 0.0F};

    std::vector<JointPose> joints;
    std::vector<float> shape;
    std::vector<float> expression;

    // EasyMocap SMPL-X compact streams carry 6 PCA coefficients per hand.
    // They are preserved here until a model-aware hand expander is available.
    std::vector<float> left_hand_pca;
    std::vector<float> right_hand_pca;

    const JointPose* find_joint(const std::string& name) const noexcept;
};

class EasyMocapSkeletonMapper {
public:
    static std::vector<int> subject_ids(const CaptureFrame& frame);

    static std::optional<SkeletonPose> map_subject(
        const CaptureFrame& frame,
        int subject_id);

    static const char* model_name(SkeletonModel model) noexcept;
};

} // namespace bdfrpc
