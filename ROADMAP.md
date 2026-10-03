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
- [ ] device enumeration and hot-plug detection
- [ ] camera capability negotiation (resolution/FPS/pixel format)
- [ ] configurable software-sync tolerance
- [ ] per-camera latency/skew telemetry
- [ ] dropped-frame counters
- [ ] preview fan-out without blocking capture
- [ ] hardware timestamp adapters where supported

## M2 — Calibration
- [ ] intrinsics
- [ ] distortion
- [ ] extrinsics
- [ ] calibration profile persistence
- [ ] Charuco/AprilTag calibration workflow
- [ ] reprojection-error diagnostics

## M3 — Solver adapters
- [ ] BDFR FacialAnimation adapter
- [ ] EasyMocap adapter
- [ ] independent Face Only mode
- [ ] independent Body Only mode
- [ ] Hybrid mode
- [ ] adapter health/watchdog/reconnect

## M4 — Fusion and retarget
- [ ] clock-offset estimation
- [ ] confidence-aware head-source arbitration
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
