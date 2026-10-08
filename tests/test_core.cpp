#ifdef NDEBUG
#undef NDEBUG
#endif

#include "bdfrpc/AdapterProtocol.h"
#include "bdfrpc/BDFRFacialUdpSource.h"
#include "bdfrpc/Calibration.h"
#include "bdfrpc/ClockSync.h"
#include "bdfrpc/DeviceDiscovery.h"
#include "bdfrpc/EasyMocapTcpSource.h"
#include "bdfrpc/FusionCore.h"
#include "bdfrpc/FrameSynchronizer.h"
#include "bdfrpc/FusionRuntime.h"
#include "bdfrpc/SolverAdapters.h"
#include "bdfrpc/SourceHealth.h"
#include "bdfrpc/TakeRecorder.h"
#include "bdfrpc/TakeReader.h"
#include "bdfrpc/BinaryTake.h"
#include "bdfrpc/TakeSessionIndex.h"
#include "bdfrpc/Skeleton.h"
#include "bdfrpc/Retarget.h"
#include "bdfrpc/Kinematics.h"
#include "bdfrpc/BvhExport.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace bdfrpc;

static DomainSample sample(Domain domain, float confidence, float value) {
    return {domain, confidence, {value}};
}

static void add_group(
    DomainSample& sample,
    int subject,
    const std::string& group,
    std::size_t count,
    float start = 0.0f) {

    for (std::size_t i = 0; i < count; ++i) {
        sample.channels.push_back(
            "subject." + std::to_string(subject) + "." +
            group + "." + std::to_string(i));
        sample.values.push_back(start + static_cast<float>(i));
    }
}

static void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

static void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
    }
}

static void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
    }
}

static void append_float(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(out, bits);
}

static void append_double(std::vector<std::uint8_t>& out, double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_u64(out, bits);
}

static void append_string(std::vector<std::uint8_t>& out, const std::string& value) {
    append_u16(out, static_cast<std::uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

static std::vector<std::uint8_t> make_facial_packet() {
    std::vector<std::uint8_t> frame;
    append_u32(frame, 0x52464442U);
    append_u16(frame, 1);
    append_u32(frame, 1);
    append_double(frame, 1.25);
    append_float(frame, 0.90F);
    append_float(frame, 1.0F);
    append_float(frame, 2.0F);
    append_float(frame, 3.0F);
    append_float(frame, 0.1F);
    append_float(frame, 0.2F);
    append_float(frame, 0.8F);
    append_u32(frame, 2);
    append_string(frame, "eyeBlinkLeft");
    append_float(frame, 0.50F);
    append_string(frame, "jawOpen");
    append_float(frame, 0.40F);

    std::vector<std::uint8_t> packet;
    append_u32(packet, 0x50464442U);
    append_u16(packet, 1);
    append_u64(packet, 42);
    append_string(packet, "phone");
    append_u32(packet, static_cast<std::uint32_t>(frame.size()));
    packet.insert(packet.end(), frame.begin(), frame.end());
    return packet;
}

int main() {
    {
        FrameSynchronizer sync(5'000'000, 2);
        sync.set_required_streams({"cam0", "cam1", "cam2"});
        sync.push({"camera", "cam0", 1, 100'000'000, {}});
        sync.push({"camera", "cam1", 1, 102'000'000, {}});
        sync.push({"camera", "cam2", 1, 104'000'000, {}});

        auto set = sync.try_pop();
        assert(set.has_value());
        assert(set->frames.size() == 3);
        assert(set->max_skew_ns == 4'000'000);
        assert(sync.stats().emitted_sets == 1);
        assert(sync.stats().last_set_skew_ns == 4'000'000);
        assert(sync.stats().streams.at("cam0").last_offset_ns == -4'000'000);

        sync.push({"camera", "cam0", 2, 200'000'000, {}});
        sync.push({"camera", "cam0", 3, 201'000'000, {}});
        sync.push({"camera", "cam0", 4, 202'000'000, {}});
        assert(sync.stats().streams.at("cam0").dropped_overflow == 1);

        sync.set_tolerance_ns(7'000'000);
        assert(sync.tolerance_ns() == 7'000'000);
    }

    {
        ClockOffsetEstimator clock(0.5, 100'000'000);
        const auto mapped0 = clock.update(1'000'000'000, 6'000'000'000);
        assert(mapped0 == 6'000'000'000);
        assert(clock.stats().offset_ns == 5'000'000'000);

        const auto mapped1 = clock.update(2'000'000'000, 7'010'000'000);
        assert(mapped1 >= 7'000'000'000);
        assert(clock.stats().accepted_samples == 2);
        assert(clock.stats().jitter_ns > 0);

        const auto accepted_before = clock.stats().accepted_samples;
        clock.update(3'000'000'000, 9'000'000'000);
        assert(clock.stats().accepted_samples == accepted_before);
        assert(clock.stats().rejected_samples == 1);
    }

    {
        SourceRouter router;
        router.set_route(Domain::Face, {
            {"bdfr_face", 0.70f, 20'000'000},
            {"easymocap", 0.40f, 20'000'000}
        });
        router.set_route(Domain::Body, {
            {"easymocap", 0.50f, 20'000'000}
        });

        SyncedFrameSet set;
        set.timestamp_ns = 1'000'000'000;
        set.frames = {
            {"bdfr_face", "face0", 10, 999'000'000,
                {sample(Domain::Face, 0.91f, 11.0f)}},
            {"easymocap", "body0", 10, 1'001'000'000,
                {sample(Domain::Face, 0.80f, 22.0f),
                 sample(Domain::Body, 0.95f, 33.0f)}}
        };

        FusionCore fusion(router);
        const auto out = fusion.fuse(set);
        assert(out.samples.size() == 2);
        assert(out.selected_sources.size() == 2);
        assert(out.selected_sources[0] == "bdfr_face");
        assert(out.selected_sources[1] == "easymocap");
        assert(out.samples[0].values[0] == 11.0f);
        assert(out.samples[1].values[0] == 33.0f);
    }

    {
        const AdapterHello hello{1, "BDFR_FacialAnimation", "0.1", "face,head"};
        const auto wire = encode_hello(hello);
        const auto decoded = decode_hello(wire);
        assert(decoded.has_value());
        assert(decoded->adapter_name == "BDFR_FacialAnimation");
        assert(decoded->protocol_version == 1);
    }

    {
        DeviceMonitor monitor;
        auto changes = monitor.update({
            {"camera:0", "Camera 0", "test", DeviceKind::Video, 0, 1280, 720, 60.0, true}
        });
        assert(changes.added.size() == 1);

        changes = monitor.update({
            {"camera:0", "Camera 0", "test", DeviceKind::Video, 0, 1920, 1080, 60.0, true},
            {"camera:1", "Camera 1", "test", DeviceKind::Video, 1, 1280, 720, 30.0, true}
        });
        assert(changes.added.size() == 1);
        assert(changes.changed.size() == 1);

        changes = monitor.update({
            {"camera:1", "Camera 1", "test", DeviceKind::Video, 1, 1280, 720, 30.0, true}
        });
        assert(changes.removed.size() == 1);
    }

    {
        CameraCalibration camera;
        camera.camera_id = "cam0";
        camera.intrinsics.width = 1920;
        camera.intrinsics.height = 1080;
        camera.intrinsics.fx = 1000.0;
        camera.intrinsics.fy = 1000.0;
        camera.intrinsics.cx = 960.0;
        camera.intrinsics.cy = 540.0;
        camera.rms_reprojection_error = 0.25;

        const auto projected = project_point(camera, {0.0, 0.0, 2.0});
        assert(projected.has_value());
        assert(std::abs(projected->x - 960.0) < 1e-9);
        assert(std::abs(projected->y - 540.0) < 1e-9);

        CalibrationProfile profile;
        assert(profile.upsert(camera));
        const auto serialized = profile.serialize();

        std::string error;
        const auto restored = CalibrationProfile::deserialize(serialized, &error);
        assert(restored.has_value());
        assert(error.empty());
        assert(restored->find("cam0") != nullptr);

        const auto rmse = reprojection_rmse(
            camera,
            {{0.0, 0.0, 2.0}, {0.1, 0.0, 2.0}},
            {{960.0, 540.0}, {1010.0, 540.0}});
        assert(rmse < 1e-9);
    }

    {
        BDFRFacialAdapter face;
        EasyMocapAdapter body;
        assert(face.start());
        assert(body.start());

        CaptureFrame face_frame;
        face_frame.timestamp_ns = 1'000'000'000;
        face_frame.samples = {
            sample(Domain::Face, 0.95f, 1.0f),
            sample(Domain::Body, 0.95f, 99.0f)
        };
        assert(face.submit(face_frame));
        auto accepted = face.poll();
        assert(accepted.has_value());
        assert(accepted->source_id == "bdfr_facial");
        assert(accepted->samples.size() == 1);
        assert(accepted->samples[0].domain == Domain::Face);

        SourceRouter router;
        configure_default_routes(OperatingMode::Hybrid, router);

        SyncedFrameSet set;
        set.timestamp_ns = 2'000'000'000;
        set.frames = {
            {"bdfr_facial", "face", 1, 2'000'000'000,
                {sample(Domain::Face, 0.90f, 7.0f),
                 sample(Domain::Head, 0.90f, 8.0f)}},
            {"easymocap", "body", 1, 2'000'000'000,
                {sample(Domain::Face, 0.80f, 70.0f),
                 sample(Domain::Body, 0.95f, 9.0f),
                 sample(Domain::LeftHand, 0.90f, 10.0f),
                 sample(Domain::RightHand, 0.90f, 11.0f)}}
        };

        FusionCore fusion(router);
        const auto out = fusion.fuse(set);
        assert(out.samples.size() == 5);
        assert(out.selected_sources[0] == "bdfr_facial");
        assert(out.selected_sources[2] == "easymocap");
    }

    {
        CaptureFrame decoded;
        std::uint64_t sequence = 0;
        std::string remote;
        assert(BDFRFacialPacketCodec::decode(
            make_facial_packet(), "bdfr_facial", decoded, &sequence, &remote));
        assert(sequence == 42);
        assert(remote == "phone");
        assert(decoded.source_id == "bdfr_facial");
        assert(decoded.stream_id == "phone");
        assert(decoded.timestamp_ns == 1'250'000'000);
        assert(decoded.samples.size() == 2);
        assert(decoded.samples[0].domain == Domain::Face);
        assert(decoded.samples[0].channels.size() == 5);
        assert(decoded.samples[0].channels[0] == "eyeBlinkLeft");
        assert(decoded.samples[0].channels[1] == "jawOpen");
        assert(decoded.samples[1].channels[0] == "pitch");
        assert(decoded.samples[0].channels_valid());
    }

    {
        LatestFrameStore store(1'000'000'000);
        CaptureFrame newer{
            "solver", "stream", 2, 2'000'000'000,
            {sample(Domain::Body, 0.9f, 2.0f)}
        };
        assert(store.push(newer));

        CaptureFrame older{
            "solver", "stream", 1, 1'900'000'000,
            {sample(Domain::Body, 0.9f, 1.0f)}
        };
        assert(!store.push(older));
        assert(store.stats().rejected_out_of_order == 1);

        const auto snap = store.snapshot(2'100'000'000);
        assert(snap.frames.size() == 1);
        assert(snap.frames[0].sequence == 2);
    }

    {
        FusionRuntime runtime(OperatingMode::Hybrid);

        assert(runtime.submit({
            "bdfr_facial", "face", 1, 3'000'000'000,
            {sample(Domain::Face, 0.95f, 10.0f),
             sample(Domain::Head, 0.95f, 11.0f)}
        }));
        assert(runtime.submit({
            "easymocap", "body", 1, 3'000'000'000,
            {sample(Domain::Face, 0.80f, 20.0f),
             sample(Domain::Body, 0.95f, 21.0f)}
        }));

        auto fused = runtime.evaluate(3'050'000'000);
        assert(fused.samples.size() == 3);
        assert(fused.selected_sources[0] == "bdfr_facial");

        assert(runtime.submit({
            "easymocap", "body", 2, 3'200'000'000,
            {sample(Domain::Face, 0.80f, 30.0f),
             sample(Domain::Body, 0.95f, 31.0f)}
        }));

        fused = runtime.evaluate(3'200'000'000);
        assert(fused.samples.size() >= 2);
        assert(fused.selected_sources[0] == "easymocap");

        runtime.set_mode(OperatingMode::BodyOnly);
        fused = runtime.evaluate(3'200'000'000);
        assert(!fused.samples.empty());
        assert(fused.selected_sources[0] == "easymocap");
    }

    {
        SourceHealthMonitor health;
        health.configure("cam0", 100'000'000);
        auto initial = health.snapshot(1'000'000'000);
        assert(initial.size() == 1);
        assert(initial[0].state == SourceHealthState::Unknown);

        health.observe_frame("cam0", 1'000'000'000);
        auto live = health.snapshot(1'050'000'000);
        assert(live[0].state == SourceHealthState::Healthy);
        assert(live[0].frames_observed == 1);

        auto stale = health.snapshot(1'200'000'001);
        assert(stale[0].state == SourceHealthState::Stale);

        health.observe_failure("cam0", 1'210'000'000, "camera disconnected");
        auto failed = health.snapshot(1'220'000'000);
        assert(failed[0].state == SourceHealthState::Failed);
        assert(failed[0].failures == 1);

        health.observe_frame("cam0", 1'230'000'000);
        auto recovered = health.snapshot(1'240'000'000);
        assert(recovered[0].state == SourceHealthState::Healthy);
        assert(recovered[0].message.empty());
    }

    {
        const std::string payload =
            "[{\"id\":0,\"poses\":[[1,2,3,4]],\"shapes\":[[0.1,0.2]],"
            "\"expression\":[[0.3,0.4]],\"Rh\":[[0.5,0.6,0.7]],"
            "\"Th\":[[1,2,3]]}]";

        CaptureFrame decoded;
        assert(EasyMocapPayloadCodec::decode(
            payload, "easymocap", 7, 3'000'000'000, decoded));
        assert(decoded.source_id == "easymocap");
        assert(decoded.sequence == 7);
        assert(decoded.samples.size() == 2);
        assert(decoded.samples[0].domain == Domain::Body);
        assert(decoded.samples[0].channels_valid());
        assert(decoded.samples[0].channels[0] == "subject.0.Rh.0");
        assert(decoded.samples[1].domain == Domain::Face);
        assert(decoded.samples[1].channels[0] == "subject.0.expression.0");
    }

    {
        const std::string path = "bdfrpc_take_recorder_test.csv";
        TakeRecorder recorder;
        assert(recorder.start(path));

        FusedPerformanceFrame frame;
        frame.timestamp_ns = 4'000'000'000;
        DomainSample face = sample(Domain::Face, 0.95f, 0.5f);
        face.channels = {"jawOpen"};
        frame.samples = {face};
        frame.selected_sources = {"bdfr_facial"};

        assert(recorder.append(frame));
        assert(recorder.frames_written() == 1);
        recorder.stop();

        std::ifstream input(path);
        std::string content(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        assert(content.find("timestamp_ns,domain,source,confidence,channel,value") != std::string::npos);
        assert(content.find("jawOpen") != std::string::npos);
        std::remove(path.c_str());
    }

    {
        CaptureFrame smplx;
        smplx.source_id = "easymocap";
        smplx.stream_id = "easymocap_tcp";
        smplx.sequence = 100;
        smplx.timestamp_ns = 5'000'000'000;

        DomainSample body;
        body.domain = Domain::Body;
        body.confidence = 0.93f;
        add_group(body, 7, "Rh", 3, 0.1f);
        add_group(body, 7, "Th", 3, 1.0f);
        add_group(body, 7, "poses", 87, 0.01f);
        add_group(body, 7, "shapes", 10, 0.2f);

        DomainSample face;
        face.domain = Domain::Face;
        face.confidence = 0.88f;
        add_group(face, 7, "expression", 10, 0.3f);

        smplx.samples = {body, face};

        const auto ids = EasyMocapSkeletonMapper::subject_ids(smplx);
        assert(ids.size() == 1 && ids[0] == 7);

        const auto mapped = EasyMocapSkeletonMapper::map_subject(smplx, 7);
        assert(mapped.has_value());
        assert(mapped->model == SkeletonModel::SMPLXCompact87);
        assert(mapped->joints.size() == 25);
        assert(mapped->left_hand_pca.size() == 6);
        assert(mapped->right_hand_pca.size() == 6);
        assert(mapped->shape.size() == 10);
        assert(mapped->expression.size() == 10);
        assert(mapped->find_joint("pelvis") != nullptr);
        assert(mapped->find_joint("jaw") != nullptr);
        assert(std::abs(mapped->translation[0] - 1.0f) < 1e-6f);

        auto meta = RetargetProfile::metahuman_body();
        meta.set_translation_scale(100.0f);

        // 90-degree basis around Z maps canonical X rotation into Y.
        const float half = 0.78539816339f;
        meta.set_joint_basis(
            "left_shoulder",
            {std::cos(half), 0.0f, 0.0f, std::sin(half)});

        const auto retargeted = retarget_skeleton(*mapped, meta);
        assert(retargeted.profile_name == "MetaHuman Body");
        assert(!retargeted.bones.empty());
        assert(retargeted.bones.front().bone == "pelvis");
        assert(
            std::abs(
                retargeted.root_translation[0] -
                mapped->translation[0] * 100.0f) < 1e-4f);

        const auto preview = SkeletonKinematics::solve(*mapped);
        assert(preview.joints.size() == mapped->joints.size());
        const auto* left_knee = preview.find_joint("left_knee");
        const auto* left_ankle = preview.find_joint("left_ankle");
        assert(left_knee != nullptr);
        assert(left_ankle != nullptr);
        const float dx = left_ankle->position[0] - left_knee->position[0];
        const float dy = left_ankle->position[1] - left_knee->position[1];
        const float dz = left_ankle->position[2] - left_knee->position[2];
        assert(std::sqrt(dx*dx + dy*dy + dz*dz) > 0.30f);
    }

    {
        const float half = 0.78539816339f;
        const Quaternion z90{
            std::cos(half),
            0.0f,
            0.0f,
            std::sin(half)
        };

        const AxisAngle x90{1.57079632679f, 0.0f, 0.0f};
        const auto transformed =
            transform_rotation_basis(x90, z90);

        assert(std::abs(transformed.x) < 1e-4f);
        assert(
            std::abs(
                transformed.y -
                1.57079632679f) < 1e-4f);

        const auto v =
            transform_vector_basis(
                {1.0f, 0.0f, 0.0f},
                z90);
        assert(std::abs(v[0]) < 1e-4f);
        assert(std::abs(v[1] - 1.0f) < 1e-4f);
    }

    {
        CaptureFrame full;
        full.source_id = "easymocap";
        full.stream_id = "easymocap_tcp";
        full.sequence = 101;
        full.timestamp_ns = 5'100'000'000;

        DomainSample body;
        body.domain = Domain::Body;
        body.confidence = 0.95f;
        add_group(body, 8, "Rh", 3);
        add_group(body, 8, "Th", 3);
        add_group(body, 8, "poses", 165);
        full.samples = {body};

        const auto mapped = EasyMocapSkeletonMapper::map_subject(full, 8);
        assert(mapped.has_value());
        assert(mapped->model == SkeletonModel::SMPLXFull55);
        assert(mapped->joints.size() == 55);
        assert(mapped->find_joint("left_index1") != nullptr);
        assert(mapped->find_joint("right_thumb3") != nullptr);
    }

    {
        const std::string path = "bdfrpc_take_reader_test.csv";
        TakeRecorder recorder;
        assert(recorder.start(path));

        FusedPerformanceFrame f0;
        f0.timestamp_ns = 10'000'000'000;
        DomainSample face0;
        face0.domain = Domain::Face;
        face0.confidence = 0.97f;
        face0.channels = {"jawOpen", "brow,Inner"};
        face0.values = {0.5f, 0.25f};
        f0.samples = {face0};
        f0.selected_sources = {"bdfr_facial"};
        assert(recorder.append(f0));

        FusedPerformanceFrame f1;
        f1.timestamp_ns = 10'033'333'333;
        DomainSample body1;
        body1.domain = Domain::Body;
        body1.confidence = 0.91f;
        body1.channels = {"pelvis.yaw"};
        body1.values = {0.12f};
        f1.samples = {body1};
        f1.selected_sources = {"easymocap"};
        assert(recorder.append(f1));
        assert(recorder.flush());
        recorder.stop();

        TakeReader reader;
        assert(reader.load(path));
        assert(reader.frames().size() == 2);
        assert(reader.duration_ns() == 33'333'333);
        assert(reader.frames()[0].samples[0].channels[1] == "brow,Inner");
        assert(reader.frames()[1].selected_sources[0] == "easymocap");
        assert(reader.lower_bound_index(10'020'000'000) == 1);
        std::remove(path.c_str());
    }

    {
        const std::string path_a = "bdfrpc_binary_take_a.bdfrtake";
        const std::string path_b = "bdfrpc_binary_take_b.bdfrtake";

        FusedPerformanceFrame frame;
        frame.timestamp_ns = 20'000'000'000;
        DomainSample body;
        body.domain = Domain::Body;
        body.confidence = 0.92f;
        body.channels = {"subject.0.poses.0", "subject.0.poses.1"};
        body.values = {0.1f, -0.2f};
        frame.samples = {body};
        frame.selected_sources = {"easymocap"};

        for (const auto& path : {path_a, path_b}) {
            TakeRecorder writer;
            assert(writer.start(path));
            assert(writer.binary_format());
            assert(writer.append(frame));
            assert(writer.flush());
            writer.stop();
        }

        std::ifstream a(path_a, std::ios::binary);
        std::ifstream b(path_b, std::ios::binary);
        const std::string bytes_a(
            (std::istreambuf_iterator<char>(a)),
            std::istreambuf_iterator<char>());
        const std::string bytes_b(
            (std::istreambuf_iterator<char>(b)),
            std::istreambuf_iterator<char>());
        assert(bytes_a == bytes_b);

        TakeReader reader;
        assert(reader.load(path_a));
        assert(reader.frames().size() == 1);
        assert(reader.frames()[0].selected_sources[0] == "easymocap");
        assert(reader.frames()[0].samples[0].channels[1] == "subject.0.poses.1");
        assert(std::abs(reader.frames()[0].samples[0].values[1] + 0.2f) < 1e-6f);

        std::remove(path_a.c_str());
        std::remove(path_b.c_str());
    }

    {
        CaptureFrame source;
        source.source_id = "easymocap";
        source.stream_id = "easymocap_tcp";
        source.timestamp_ns = 30'000'000'000;

        DomainSample body;
        body.domain = Domain::Body;
        body.confidence = 1.0f;
        add_group(body, 0, "Rh", 3, 0.0f);
        add_group(body, 0, "Th", 3, 0.0f);
        add_group(body, 0, "poses", 72, 0.0f);
        source.samples = {body};

        auto pose0 = EasyMocapSkeletonMapper::map_subject(source, 0);
        assert(pose0.has_value());
        auto pose1 = *pose0;
        pose1.timestamp_ns = 30'033'333'333;
        pose1.translation[0] = 0.05f;

        const std::string path = "bdfrpc_export_test.bvh";
        std::string error;
        assert(export_bvh(path, {*pose0, pose1}, {}, &error));
        assert(error.empty());

        std::ifstream input(path);
        const std::string content(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        assert(content.find("HIERARCHY") != std::string::npos);
        assert(content.find("MOTION") != std::string::npos);
        assert(content.find("Frames: 2") != std::string::npos);
        assert(content.find("JOINT left_wrist") != std::string::npos);
        std::remove(path.c_str());
    }

    {
        const std::filesystem::path dir =
            "bdfrpc_session_index_test";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);

        FusedPerformanceFrame frame;
        frame.timestamp_ns = 40'000'000'000;
        DomainSample face;
        face.domain = Domain::Face;
        face.confidence = 0.9f;
        face.channels = {"jawOpen"};
        face.values = {0.4f};
        frame.samples = {face};
        frame.selected_sources = {"bdfr_facial"};

        {
            TakeRecorder binary;
            const auto path = (dir / "take01.bdfrtake").string();
            assert(binary.start(path));
            assert(binary.append(frame));
            binary.stop();
        }

        {
            TakeRecorder csv;
            const auto path = (dir / "take02.bdfrtake.csv").string();
            assert(csv.start(path));
            assert(csv.append(frame));
            csv.stop();
        }

        const auto sessions =
            TakeSessionIndex::scan(dir.string());
        assert(sessions.size() == 2);
        assert(sessions[0].valid);
        assert(sessions[1].valid);
        assert(sessions[0].frame_count == 1);
        assert(sessions[1].frame_count == 1);
        assert(
            sessions[0].format != TakeFileFormat::Unknown);
        assert(
            sessions[1].format != TakeFileFormat::Unknown);

        std::filesystem::remove_all(dir);
    }

    {
        const std::string path = "bdfrpc_recovery_test.bdfrtake";

        TakeRecorder writer;
        assert(writer.start(path));

        FusedPerformanceFrame first;
        first.timestamp_ns = 50'000'000'000;
        DomainSample face;
        face.domain = Domain::Face;
        face.confidence = 0.98f;
        face.channels = {"jawOpen"};
        face.values = {0.42f};
        first.samples = {face};
        first.selected_sources = {"bdfr_facial"};
        assert(writer.append(first));

        FusedPerformanceFrame second = first;
        second.timestamp_ns = 50'033'333'333;
        second.samples[0].values[0] = 0.73f;
        assert(writer.append(second));
        assert(writer.flush());
        writer.stop();

        const auto original_size =
            std::filesystem::file_size(path);
        assert(original_size > 12);
        std::filesystem::resize_file(
            path,
            original_size - 7);

        TakeReader recovered;
        assert(recovered.load(path));
        assert(recovered.recovered_truncated_tail());
        assert(recovered.frames().size() == 1);
        assert(
            recovered.frames()[0].timestamp_ns ==
            50'000'000'000);

        std::remove(path.c_str());
    }

    std::cout << "bdfrpc_core_tests: OK\n";
    return 0;
}
