# Architecture

BDFR PerformanceCapture is an orchestration layer, not a replacement for capture solvers.

## Operating modes

1. **Face Only**: BDFR FacialAnimation can be the only solver.
2. **Body Only**: EasyMocap or another body/hand solver can be the only solver.
3. **Hybrid**: each performance domain is routed independently and may fail over.

## Core data flow

```text
Capture Sources (N)
        |
        v
CaptureManager
        |
        v
FrameSynchronizer ---- Calibration/Clock Model
        |
        +---------------------+
        |                     |
        v                     v
BDFR Facial Adapter      Body Solver Adapter
        |                     |
        +----------+----------+
                   |
                   v
              SourceRouter
                   |
                   v
               FusionCore
                   |
        +----------+----------+
        |          |          |
      Viewer    Streaming    Export
```

## Important boundaries

- Third-party projects are not vendored into the core.
- Adapter processes may use their own Python/C++/CUDA dependency stacks.
- Every frame is timestamped.
- Routing is per-domain (face/head/body/left hand/right hand).
- A preferred source can fail over when confidence is low or data is stale.
- Multi-camera synchronization is a core service, not a solver feature.

## Synchronization strategy

Software-synchronized cameras are collected into bounded per-stream queues.
The synchronizer emits a frame set only when the selected stream-front timestamps
fall inside the configured tolerance. Frames that are provably too old are dropped.

Future clock layers:
- monotonic host timestamps
- network clock offset estimation
- device PTP/NTP metadata where available
- hardware trigger/timecode adapters

## Adapter protocol

The initial adapter handshake is intentionally minimal and versioned. Adapters remain
out-of-process so EasyMocap, BDFR FacialAnimation, ARKit bridges, or future trackers can
be upgraded independently.

The production transport is planned as a length-prefixed binary frame protocol or
shared-memory transport for local high-rate payloads, while control remains versioned.
