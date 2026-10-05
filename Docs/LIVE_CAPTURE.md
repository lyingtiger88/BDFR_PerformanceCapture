# Live capture milestone

## Device discovery

When OpenCV support is enabled, `OpenCVDeviceEnumerator` probes local video
indices and reports an ID, backend, negotiated width/height and FPS. The generic
`DeviceMonitor` compares snapshots so a Studio front end can surface added,
removed and changed devices without coupling UI code to OpenCV.

The first implementation uses portable OpenCV probing. Native Windows
Media Foundation / DirectShow enumeration can later enrich stable friendly names,
VID/PID, serials and capabilities without changing the public device model.

## Synchronization telemetry

`FrameSynchronizer` tracks:
- received frames per stream
- stale frames dropped during alignment
- queue-overflow drops
- emitted frames per stream
- emitted frame-set count
- current and maximum observed set skew
- last per-stream offset from the emitted frame-set timestamp

These counters are intended for the Studio camera grid and for automated quality
gates during long capture sessions.

## Calibration

`CalibrationProfile` now stores:
- camera intrinsics
- 8 distortion coefficients
- world-to-camera 3x3 rotation
- translation
- RMS reprojection error

Profiles use a small versioned text representation for deterministic tests and
easy diagnostics. A projection/RMSE implementation is included so imported or
future Charuco/AprilTag calibrations can be validated in the core.

## Solver adapters

Two independent buffered adapters exist:
- `BDFRFacialAdapter`: Face + Head domains
- `EasyMocapAdapter`: Face + Head + Body + Hands domains

`configure_default_routes` implements the three product modes:
Face Only, Body Only and Hybrid. In Hybrid mode, BDFR FacialAnimation is the
preferred Face/Head source and EasyMocap is the fallback; body and hands route to
EasyMocap.

The Python EasyMocap worker bootstrap lives in `adapters/easymocap_worker.py`.
It already exposes a versioned JSON-lines control handshake and dependency probe,
while actual model execution remains isolated behind that process boundary.


## Live intrinsic calibration

With `BDFRPC_ENABLE_OPENCV=ON`, `bdfrpc_calibrate_camera` performs a real
live calibration session from a local camera. It detects chessboard inner
corners, refines them to subpixel accuracy, calls OpenCV `calibrateCamera`,
then saves the result through `CalibrationProfile`.

Example:

```bash
bdfrpc_calibrate_camera 0 9 6 0.025 20 camera0.bdfrcal
```

The current tool solves per-camera intrinsics and distortion. Multi-camera
extrinsics and ChArUco/AprilTag workflows remain separate upcoming steps.
