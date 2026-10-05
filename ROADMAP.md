# Roadmap

## M0 — Foundation
- [x] C++17/CMake core
- [x] N-source capture abstraction
- [x] timestamped image frames
- [x] bounded multi-stream synchronization
- [x] per-domain routing and fallback
- [x] deterministic fusion core
- [x] versioned adapter handshake
- [x] Windows/Linux CI
- [x] optional OpenCV live camera backend

## M1 — Live multi-camera
- [x] portable OpenCV device probing
- [x] generic hot-plug/change snapshot detection
- [x] configurable software-sync tolerance
- [x] per-camera skew telemetry
- [x] stale/overflow drop counters
- [x] independent asynchronous capture thread per local camera
- [x] live multicamera hardware probe with FPS/skew/drop table
- [ ] native Windows friendly-name/VID/PID enumeration
- [ ] full camera capability negotiation
- [ ] preview fan-out / UI subscribers without blocking capture
- [ ] hardware timestamp adapters where supported

## M2 — Calibration
- [x] intrinsics data model
- [x] distortion model
- [x] extrinsics data model
- [x] versioned calibration profile persistence
- [x] projection and reprojection-RMSE diagnostics
- [ ] Charuco/AprilTag calibration workflow
- [ ] calibration wizard UI

## M3 — Solver adapters
- [x] buffered BDFR FacialAnimation adapter foundation
- [x] buffered EasyMocap adapter foundation
- [x] Face Only routing policy
- [x] Body Only routing policy
- [x] Hybrid routing policy with Face/Head fallback
- [x] versioned EasyMocap worker control bootstrap
- [x] FacialAnimation live UDP packet bridge
- [x] native receiver for EasyMocap BaseSocketClient.send_smpl()
- [ ] automatic EasyMocap solver launch / live solve execution
- [ ] adapter health/watchdog/reconnect

## M4 — Fusion and retarget
- [x] remote-to-local clock offset estimation and outlier rejection
- [x] confidence-aware preferred/fallback source arbitration
- [x] named semantic channels preserved through fusion
- [ ] skeletal schema
- [ ] SMPL-X mapping
- [ ] UE/MetaHuman mapping
- [ ] Blender/Maya export bridge

## M5 — Studio UI
- [ ] camera grid
- [ ] source health
- [ ] sync status
- [ ] calibration wizard
- [ ] source routing panel
- [ ] 3D viewer
- [ ] record/playback/session browser

## M6 — Production hardening
- [ ] shared-memory frame transport
- [ ] zero-copy/GPU upload paths
- [ ] long-session soak tests
- [ ] latency benchmarks
- [ ] deterministic recording format
- [ ] crash recovery
- [ ] signed Windows test builds
