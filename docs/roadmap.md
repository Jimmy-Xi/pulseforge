# Roadmap

PulseForge is intentionally structured as a platform rather than a finished one-off demo.

## v0.2 — Cross-backend conformance

- Capture a kernel trace and compare it frame-by-frame with the Python digital twin.
- Add Hypothesis-generated configuration cases.
- Version the ABI and add explicit little-endian conversion helpers.
- Add AddressSanitizer/UndefinedBehaviorSanitizer tests for userspace tools.

## v0.3 — QEMU hardware boundary

- Implement a QEMU PCI or MMIO telemetry function with the same generator.
- Split the current synthetic driver core from the bus-specific frontend.
- Exercise probe/remove, interrupt handling and register access.
- Add a Buildroot image for a one-command headless demonstration.

## v0.4 — Performance and observability

- Add an mmap shared-ring transport with a versioned control page.
- Measure interrupt-to-userspace latency with eBPF tracepoints.
- Export Prometheus counters and a small live dashboard.
- Compare read, io_uring and mmap transport overhead.

## v1.0 — Hardware bridge

- Port the ABI to an FPGA or microcontroller telemetry source.
- Keep the Python model as the golden reference.
- Add recorded real-sensor scenarios and regression fingerprints.
- Publish reproducible latency, throughput and CPU utilization results.

Issues suitable for a first contribution should be labeled `good first issue`; architecture-changing work should start with a short design note.

