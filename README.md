# BDFR PerformanceCapture

BDFR PerformanceCapture is the orchestration layer for modular performance capture: multi-camera acquisition, timestamp synchronization, source routing, full-performance fusion, adapters, retargeting and live/offline outputs.

The central design rule is **independent or hybrid operation**:

- **Face Only** — BDFR FacialAnimation can run by itself.
- **Body Only** — an external body/hand solver such as EasyMocap can run by itself.
- **Hybrid** — face, head, body and hands are selected and fused per domain with timestamped failover.

No third-party mocap implementation is vendored into this repository. Adapters remain isolated so each upstream project retains its own dependency and license boundary.

## Current implemented core

- C++17 / CMake portable core
- `ICaptureSource` abstraction for arbitrary live/offline inputs
- `CaptureManager` for N independent sources
- `FrameSynchronizer` for timestamp-based multi-camera grouping
- `SourceRouter` with priority, confidence threshold, timeout and automatic fallback
- `FusionCore` for deterministic per-domain Face / Head / Body / Left Hand / Right Hand selection
- versioned adapter hello protocol for out-of-process integrations
- core tests and Windows/Linux CI

## Build

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## License status

BDFR PerformanceCapture's own project license has not yet been finalized. Third-party software, models and datasets remain subject to their own licenses.
