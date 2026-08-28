# Architecture and design decisions

## Goal

PulseForge is a hardware-independent environment for exercising the difficult parts of an embedded telemetry stack: kernel timing, concurrent buffering, ABI design, non-blocking I/O, fault handling, integrity verification and reproducible debugging.

The Linux implementation is the system under test. The Python implementation is an executable specification. They intentionally share algorithms instead of sharing implementation code, so agreement between them is meaningful.

## Data path

1. A high-resolution kernel timer represents the sampling clock.
2. The fixed-point model advances its xorshift32 PRNG once per attempted sample.
3. Noise and drift are applied to a 200-step triangle waveform.
4. The configured drop, spike and freeze rules are evaluated in that order.
5. A delivered frame receives its status bits and IEEE CRC-32.
6. The frame enters a lock-protected ring. On saturation, the oldest frame is evicted and the new frame is marked as an overrun.
7. A wait queue wakes blocking readers and makes the descriptor readable to `poll`/`epoll`.
8. `pulseforge-cat` reads batches, rejects corrupted frames and emits NDJSON or CSV.

## Invariants

- The PRNG advances exactly once per attempted sample, including an injected drop.
- `sequence` and `device_time_ns` advance on attempts, not deliveries.
- Frames are either delivered whole or not delivered; reads never return a partial frame.
- The frame ABI remains exactly 32 bytes.
- CRC covers the first 28 bytes and excludes the CRC field itself.
- Configuration changes reset the deterministic timeline and atomically start a new incident.
- A reader can identify injected drops from sequence gaps without privileged access to driver counters.

## Concurrency model

The timer callback, readers and ioctl handlers share a single spin lock for configuration, generator state, statistics and ring indices. The protected section in the timer is bounded and performs no allocation or sleeping operation. Reads copy a bounded batch from the ring into temporary kernel memory while locked, then release the lock before `copy_to_user`.

Changing configuration cancels the timer, replaces the configuration under the lock, resets the deterministic state, and restarts the timer with the new period. This prevents frames from two configurations from being mixed in one incident timeline.

## Why fixed point

Floating-point code is inappropriate in the Linux kernel and can introduce cross-platform rounding differences. PulseForge uses milli-units and C99 truncation toward zero. The Python model implements the same division semantics explicitly.

## Why a deterministic device time

Wall-clock timestamps make byte-for-byte replay impossible. `device_time_ns = sequence × period_us × 1000` is stable across executions. A real deployment can add host receipt time in the collector without changing the device ABI.

## Known boundaries

- Version 0.1 targets little-endian Linux systems.
- The misc device is global rather than per-open; multiple readers compete for frames.
- Ring transport uses `read`, not `mmap`.
- The driver is compiled in CI but hardware-free runtime tests currently require a Linux VM or host that permits loading custom modules.

The kernel module is dual-licensed under `GPL-2.0-only OR MIT` because its high-resolution timer path uses GPL-only exported kernel symbols. Userspace components remain MIT licensed.

Each boundary maps directly to a roadmap item instead of being hidden as an implementation accident.
