#include "bdfrpc/Skeleton.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bdfrpc {
namespace {

struct JointDef {
    const char* name;
    int parent;
};

constexpr JointDef kBody22[] = {
    {"pelvis", -1},
    {"left_hip", 0},
    {"right_hip", 0},
    {"spine1", 0},
    {"left_knee", 1},
    {"right_knee", 2},
    {"spine2", 3},
    {"left_ankle", 4},
    {"right_ankle", 5},
    {"spine3", 6},
    {"left_foot", 7},
    {"right_foot", 8},
    {"neck", 9},
    {"left_collar", 9},
    {"right_collar", 9},
    {"head", 12},
    {"left_shoulder", 13},
    {"right_shoulder", 14},
    {"left_elbow", 16},
    {"right_elbow", 17},
    {"left_wrist", 18},
    {"right_wrist", 19},
};

constexpr JointDef kSmplExtra[] = {
    {"left_hand", 20},
    {"right_hand", 21},
};

constexpr JointDef kFaceJoints[] = {
    {"jaw", 15},
    {"left_eye", 15},
    {"right_eye", 15},
};

constexpr const char* kHandDigits[] = {
    "index", "middle", "pinky", "ring", "thumb"
};

std::string prefix(int subject_id, const char* group) {
    return "subject." + std::to_string(subject_id) + "." + group + ".";
}

bool parse_channel_index(
    const std::string& channel,
    const std::string& wanted_prefix,
    std::size_t& index) {

    if (channel.rfind(wanted_prefix, 0) != 0) return false;
    const auto suffix = channel.substr(wanted_prefix.size());
    if (suffix.empty()) return false;

    char* end = nullptr;
    const auto value = std::strtoul(suffix.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    index = static_cast<std::size_t>(value);
    return true;
}

std::vector<float> extract_group(
    const DomainSample& sample,
    int subject_id,
    const char* group) {

    const auto wanted = prefix(subject_id, group);
    std::map<std::size_t, float> ordered;

    const auto count = std::min(sample.channels.size(), sample.values.size());
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t index = 0;
        if (parse_channel_index(sample.channels[i], wanted, index)) {
            ordered[index] = sample.values[i];
        }
    }

    if (ordered.empty()) return {};

    std::vector<float> values(ordered.rbegin()->first + 1, 0.0F);
    for (const auto& [index, value] : ordered) {
        values[index] = value;
    }
    return values;
}

void append_joint(
    SkeletonPose& pose,
    const JointDef& def,
    const std::vector<float>& values,
    std::size_t offset) {

    if (offset + 2 >= values.size()) return;
    JointPose joint;
    joint.name = def.name;
    joint.parent = def.parent;
    joint.rotation = {
        values[offset],
        values[offset + 1],
        values[offset + 2]
    };
    pose.joints.push_back(std::move(joint));
}

void append_body22(SkeletonPose& pose, const std::vector<float>& values) {
    for (std::size_t i = 0; i < 22; ++i) {
        append_joint(pose, kBody22[i], values, i * 3);
    }
}

void append_full_hand(
    SkeletonPose& pose,
    const std::vector<float>& values,
    std::size_t offset,
    bool left) {

    const int wrist_parent = left ? 20 : 21;
    const std::string side = left ? "left_" : "right_";

    std::size_t joint_index = 0;
    for (const char* digit : kHandDigits) {
        int parent = wrist_parent;
        for (int segment = 1; segment <= 3; ++segment) {
            const auto component_offset = offset + joint_index * 3;
            if (component_offset + 2 >= values.size()) return;

            JointPose joint;
            joint.name =
                side + digit + std::to_string(segment);
            joint.parent = parent;
            joint.rotation = {
                values[component_offset],
                values[component_offset + 1],
                values[component_offset + 2]
            };

            pose.joints.push_back(std::move(joint));
            parent = static_cast<int>(pose.joints.size() - 1);
            ++joint_index;
        }
    }
}

const DomainSample* sample_for(
    const CaptureFrame& frame,
    Domain domain) {

    for (const auto& sample : frame.samples) {
        if (sample.domain == domain) return &sample;
    }
    return nullptr;
}

std::vector<int> ids_from_sample(const DomainSample& sample) {
    std::vector<int> ids;
    for (const auto& channel : sample.channels) {
        constexpr const char* begin = "subject.";
        if (channel.rfind(begin, 0) != 0) continue;

        const auto dot = channel.find('.', 8);
        if (dot == std::string::npos) continue;

        const auto id_text = channel.substr(8, dot - 8);
        if (id_text.empty()) continue;

        bool numeric = true;
        for (char ch : id_text) {
            if (!std::isdigit(static_cast<unsigned char>(ch)) && ch != '-') {
                numeric = false;
                break;
            }
        }
        if (!numeric) continue;

        try {
            const int id = std::stoi(id_text);
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
                ids.push_back(id);
            }
        } catch (...) {
        }
    }
    return ids;
}

} // namespace

const JointPose* SkeletonPose::find_joint(
    const std::string& name) const noexcept {

    for (const auto& joint : joints) {
        if (joint.name == name) return &joint;
    }
    return nullptr;
}

std::vector<int> EasyMocapSkeletonMapper::subject_ids(
    const CaptureFrame& frame) {

    std::vector<int> ids;
    for (const auto& sample : frame.samples) {
        for (const int id : ids_from_sample(sample)) {
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
                ids.push_back(id);
            }
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::optional<SkeletonPose> EasyMocapSkeletonMapper::map_subject(
    const CaptureFrame& frame,
    int subject_id) {

    const auto* body = sample_for(frame, Domain::Body);
    if (!body) return std::nullopt;

    const auto poses = extract_group(*body, subject_id, "poses");
    const auto rh = extract_group(*body, subject_id, "Rh");
    const auto th = extract_group(*body, subject_id, "Th");
    const auto shapes = extract_group(*body, subject_id, "shapes");

    if (poses.empty()) return std::nullopt;

    SkeletonPose out;
    out.subject_id = subject_id;
    out.timestamp_ns = frame.timestamp_ns;
    out.confidence = body->confidence;
    out.shape = shapes;

    if (rh.size() >= 3) {
        std::copy_n(rh.begin(), 3, out.global_orientation.begin());
    }
    if (th.size() >= 3) {
        std::copy_n(th.begin(), 3, out.translation.begin());
    }

    const auto* face = sample_for(frame, Domain::Face);
    if (face) {
        out.expression = extract_group(*face, subject_id, "expression");
    }

    if (poses.size() == 72) {
        out.model = SkeletonModel::SMPL24;
        append_body22(out, poses);
        append_joint(out, kSmplExtra[0], poses, 66);
        append_joint(out, kSmplExtra[1], poses, 69);
        return out;
    }

    if (poses.size() == 87) {
        // EasyMocap SMPL-X compact layout:
        // 66 body + 6 left-hand PCA + 6 right-hand PCA + 9 jaw/eyes.
        out.model = SkeletonModel::SMPLXCompact87;
        append_body22(out, poses);

        out.left_hand_pca.assign(poses.begin() + 66, poses.begin() + 72);
        out.right_hand_pca.assign(poses.begin() + 72, poses.begin() + 78);

        for (std::size_t i = 0; i < 3; ++i) {
            append_joint(out, kFaceJoints[i], poses, 78 + i * 3);
        }
        return out;
    }

    if (poses.size() == 165) {
        // EasyMocap SMPL-X full layout after extend_pose:
        // 66 body + 9 jaw/eyes + 45 left hand + 45 right hand.
        out.model = SkeletonModel::SMPLXFull55;
        append_body22(out, poses);

        for (std::size_t i = 0; i < 3; ++i) {
            append_joint(out, kFaceJoints[i], poses, 66 + i * 3);
        }
        append_full_hand(out, poses, 75, true);
        append_full_hand(out, poses, 120, false);
        return out;
    }

    return std::nullopt;
}

const char* EasyMocapSkeletonMapper::model_name(
    SkeletonModel model) noexcept {

    switch (model) {
        case SkeletonModel::SMPL24: return "SMPL24";
        case SkeletonModel::SMPLXCompact87: return "SMPLXCompact87";
        case SkeletonModel::SMPLXFull55: return "SMPLXFull55";
        case SkeletonModel::Unknown: break;
    }
    return "Unknown";
}

} // namespace bdfrpc
