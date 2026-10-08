# BDFR PerformanceCapture

BDFR PerformanceCapture is the orchestration layer for modular performance capture:
live multi-camera acquisition, timestamp synchronization, calibration, source routing,
full-performance fusion, solver adapters, retargeting and live/offline outputs.

The central design rule is **independent or hybrid operation**:

- **Face Only** — BDFR FacialAnimation can run by itself.
- **Body Only** — EasyMocap or another body/hand solver can run by itself.
- **Hybrid** — face, head, body and hands are selected and fused per domain with confidence-aware fallback.

No third-party mocap implementation is vendored into this repository. Adapters remain
isolated so each upstream project retains its own dependency and license boundary.

## Implemented foundation

- C++17 / CMake portable core
- `ICaptureSource` abstraction for arbitrary live/offline inputs
- `CaptureManager` for N independent sources
- timestamped image buffers
- `FrameSynchronizer` with configurable tolerance and bounded queues
- per-stream received/emitted/drop/skew telemetry
- remote-to-local clock rebasing with jitter/outlier tracking
- thread-safe source health monitor with Healthy/Stale/Failed recovery state
- `DeviceMonitor` for added/removed/changed capture devices
- optional OpenCV camera source and device probing
- independent asynchronous capture thread and bounded frame queue per camera
- live multi-camera probe with FPS/skew/drop diagnostics
- camera intrinsics, distortion, extrinsics and calibration-profile persistence
- projection / reprojection-RMSE diagnostics
- live chessboard camera intrinsic calibration and profile export
- `SourceRouter` with priority, confidence threshold, timeout and automatic fallback
- `FusionCore` for deterministic Face / Head / Body / Left Hand / Right Hand selection
- asynchronous `LatestFrameStore` + `FusionRuntime` so solver streams do not need lockstep timestamps
- BDFR FacialAnimation and EasyMocap buffered adapter foundations
- protocol-compatible live UDP receiver for BDFR FacialAnimation packets
- named semantic channels so FACS/ARKit/skeletal labels survive fusion
- Face Only / Body Only / Hybrid routing policies
- versioned EasyMocap JSON-lines worker bootstrap
- native TCP receiver compatible with EasyMocap `BaseSocketClient.send_smpl()`
- canonical SMPL24 / compact SMPL-X 87 / expanded SMPL-X 165 skeleton parsing
- UE Mannequin and MetaHuman body bone-name retarget profiles
- fused take recording plus CSV take reader with random timestamp lookup
- Windows/Linux CI plus OpenCV backend compile-check

## BDFR Performance Studio

An optional Qt6 desktop Studio is now included. It uses the real capture and
fusion core rather than mock controls:

- camera discovery and rescanning
- live multi-camera preview grid
- start/stop of asynchronous camera capture
- live FPS, queue-drop and sync-skew telemetry
- Face Only / Body Only / Hybrid mode switching
- BDFR FacialAnimation UDP bridge controls
- EasyMocap TCP bridge controls
- live solver health and fused-domain/source status

Build it with:

```bash
cmake -S . -B build-studio -DBDFRPC_ENABLE_OPENCV=ON -DBDFRPC_BUILD_STUDIO=ON
cmake --build build-studio --config Release
```

## Build

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For local OpenCV camera support:

```bash
cmake -S . -B build -DBDFRPC_ENABLE_OPENCV=ON
cmake --build build --config Release
```

Then `bdfrpc_devices` can probe local camera indices,
`bdfrpc_camera_probe <index>` performs a short single-camera test, and
`bdfrpc_multicam_probe [seconds] [last-index] [tolerance-ms]` runs a real
multi-camera capture session and prints per-camera FPS, skew and drop telemetry.
`bdfrpc_calibrate_camera [device] [cols] [rows] [square-size] [samples] [output]`
collects live chessboard observations, solves intrinsics/distortion and upserts the
camera into a versioned BDFR calibration profile. Run it for each camera using the
same output file to build a multi-camera profile.

`bdfrpc_calibrate_rig [profile] [reference-index] [cols] [rows] [square-size] [samples] [output]`
uses synchronized views of the same chessboard and OpenCV stereo calibration to
solve each camera's extrinsics relative to a selected reference camera.

For solver transport tests:
- `bdfrpc_facial_udp_probe <port>` listens for BDFR FacialAnimation live packets.
- `bdfrpc_easymocap_probe <port>` accepts EasyMocap `send_smpl()` TCP streams.

See `ROADMAP.md`, `Docs/ARCHITECTURE.md`, `Docs/ADAPTERS.md` and
`Docs/LIVE_CAPTURE.md`.

## License status

BDFR PerformanceCapture's own project license has not yet been finalized.
Third-party software, models and datasets remain subject to their own licenses.
