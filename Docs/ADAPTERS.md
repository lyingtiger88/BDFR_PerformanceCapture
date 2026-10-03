# Adapter Integration Notes

## BDFR FacialAnimation

The current FacialAnimation repository already provides a live runtime with:
- `FramePacketCodec`
- `UdpFrameSender` / `UdpFrameReceiver`
- `LiveSessionReceiver`
- jitter buffering and clock-offset estimation

PerformanceCapture should therefore integrate through an adapter boundary instead of
copying FacialAnimation internals. The first production adapter should consume its
versioned mocap packets and translate face/head data into `DomainSample` records.

This preserves independent operation: FacialAnimation remains runnable by itself.

## EasyMocap

EasyMocap is Python-first and has its own model/dependency stack. It should run as a
separate worker process. The BDFR adapter will translate synchronized camera frames or
recorded sessions into the worker's expected input and translate solved SMPL/SMPL-X /
MANO outputs back into BDFR domains.

Do not vendor EasyMocap into the C++ core.

## Transport policy

Control plane:
- versioned handshake
- capabilities
- health
- configuration
- start/stop/session commands

Data plane:
- local shared memory for high-bandwidth image frames (target)
- compact binary packets for solved animation data
- UDP is acceptable for low-latency lossy live curves where the producer already
  exposes it
- recorded/offline data should use reliable files or streams

## Operating modes

**Face Only**
- FacialAnimation adapter enabled
- body adapter disabled

**Body Only**
- body adapter enabled
- facial adapter disabled

**Hybrid**
- both enabled
- SourceRouter selects Face/Head/Body/Hands independently
- preferred sources can fail over on low confidence or stale data
