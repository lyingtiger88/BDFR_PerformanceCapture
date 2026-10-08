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
- [x] bounded frame fan-out core for independent preview/solver/recorder consumers
- [ ] hardware timestamp adapters where supported

## M2 — Calibration
- [x] intrinsics data model
- [x] distortion model
- [x] extrinsics data model
- [x] versioned calibration profile persistence
- [x] projection and reprojection-RMSE diagnostics
- [x] live chessboard intrinsic-calibration tool
- [x] synchronized chessboard multi-camera extrinsic rig calibration
- [ ] ChArUco/AprilTag calibration workflow
- [x] calibration wizard UI for intrinsics and rig extrinsics

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
- [x] source health state / timeout / recovery monitor
- [x] EasyMocap worker process supervisor with bounded watchdog restart
- [x] Studio UI for EasyMocap command/config launch through worker supervisor

## M4 — Fusion and retarget
- [x] remote-to-local clock offset estimation and outlier rejection
- [x] confidence-aware preferred/fallback source arbitration
- [x] named semantic channels preserved through fusion
- [x] asynchronous latest-frame solver cache
- [x] runtime Face/Body/Hybrid failover without lockstep solver timestamps
- [x] canonical skeletal schema
- [x] EasyMocap SMPL24 / SMPL-X 87 / SMPL-X 165 canonical mapping
- [x] UE Mannequin / MetaHuman canonical bone-name mapping foundation
- [x] configurable target-space joint/root basis correction engine
- [ ] validated asset-specific MetaHuman rest-pose correction profile
- [x] Blender/Maya BVH animation export bridge

## M5 — Studio UI
- [x] live camera grid with previews
- [x] source health model and Studio status binding
- [x] live sync/skew/drop status
- [ ] calibration wizard
- [x] Face Only / Body Only / Hybrid mode control and live bridge status
- [x] canonical forward-kinematics preview core
- [x] interactive 3D skeleton viewer UI with live/playback orbit and zoom
- [x] fused take recording and CSV take reader core
- [x] Studio take playback controls
- [x] take session directory index core
- [x] Studio session browser dialog

## M6 — Production hardening
- [ ] shared-memory frame transport
- [ ] zero-copy/GPU upload paths
- [x] synthetic high-frame-count soak/throughput harness
- [ ] real multi-hour camera/solver soak test
- [x] synthetic Sync/Fusion processing benchmark
- [ ] hardware end-to-end capture-to-output latency benchmark
- [x] deterministic binary BDFR Take v1 + legacy CSV compatibility
- [x] truncated-tail recovery for binary takes after interrupted writes
- [ ] full application/session-state crash recovery
- [ ] signed Windows test builds
