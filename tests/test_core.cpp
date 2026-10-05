#include "bdfrpc/AdapterProtocol.h"
#include "bdfrpc/Calibration.h"
#include "bdfrpc/DeviceDiscovery.h"
#include "bdfrpc/FusionCore.h"
#include "bdfrpc/FrameSynchronizer.h"
#include "bdfrpc/SolverAdapters.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace bdfrpc;

static DomainSample sample(Domain domain, float confidence, float value) {
    return {domain, confidence, {value}};
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

    std::cout << "bdfrpc_core_tests: OK\n";
    return 0;
}
