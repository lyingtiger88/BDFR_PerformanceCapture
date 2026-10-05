#include "bdfrpc/AdapterProtocol.h"
#include "bdfrpc/BDFRFacialUdpSource.h"
#include "bdfrpc/Calibration.h"
#include "bdfrpc/ClockSync.h"
#include "bdfrpc/DeviceDiscovery.h"
#include "bdfrpc/EasyMocapTcpSource.h"
#include "bdfrpc/FusionCore.h"
#include "bdfrpc/FrameSynchronizer.h"
#include "bdfrpc/SolverAdapters.h"
#include "bdfrpc/SourceHealth.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace bdfrpc;

static DomainSample sample(Domain domain, float confidence, float value) {
    return {domain, confidence, {value}};
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

    std::cout << "bdfrpc_core_tests: OK\n";
    return 0;
}
