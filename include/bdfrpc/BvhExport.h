#pragma once

#include "bdfrpc/Skeleton.h"
#include "bdfrpc/Types.h"

#include <string>
#include <vector>

namespace bdfrpc {

struct BvhExportOptions {
    // If <= 0, frame time is inferred from take timestamps.
    double frame_rate{0.0};
    // EasyMocap translations are normally meters. BVH importers commonly work
    // more naturally with centimeter-scale values.
    float position_scale{100.0F};
};

std::vector<SkeletonPose> extract_skeleton_sequence(
    const std::vector<FusedPerformanceFrame>& frames,
    int subject_id);

bool export_bvh(
    const std::string& path,
    const std::vector<SkeletonPose>& poses,
    const BvhExportOptions& options = {},
    std::string* error = nullptr);

} // namespace bdfrpc
