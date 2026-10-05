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
- `SourceRouter` with priority, confidence threshold, timeout and automatic fallback
- `FusionCore` for deterministic Face / Head / Body / Left Hand / Right Hand selection
- BDFR FacialAnimation and EasyMocap buffered adapter foundations
- protocol-compatible live UDP receiver for BDFR FacialAnimation packets
- named semantic channels so FACS/ARKit/skeletal labels survive fusion
- Face Only / Body Only / Hybrid routing policies
- versioned EasyMocap JSON-lines worker bootstrap
- native TCP receiver compatible with EasyMocap `BaseSocketClient.send_smpl()`
- Windows/Linux CI plus OpenCV backend compile-check

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

For solver transport tests:
- `bdfrpc_facial_udp_probe <port>` listens for BDFR FacialAnimation live packets.
- `bdfrpc_easymocap_probe <port>` accepts EasyMocap `send_smpl()` TCP streams.

See `ROADMAP.md`, `Docs/ARCHITECTURE.md`, `Docs/ADAPTERS.md` and
`Docs/LIVE_CAPTURE.md`.

## License status

BDFR PerformanceCapture's own project license has not yet been finalized.
Third-party software, models and datasets remain subject to their own licenses.
