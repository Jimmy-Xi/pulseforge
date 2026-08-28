# PulseForge protocol

## Frame ABI

The authoritative definition is `include/pulseforge_uapi.h`.

```c
struct pulseforge_sample {
    uint64_t device_time_ns;
    uint64_t sequence;
    int32_t  signal_milli;
    int32_t  temperature_milli;
    uint32_t status;
    uint32_t crc32;
} __attribute__((packed));
```

Version 0.1 uses native little-endian integer encoding and an IEEE CRC-32 compatible with Python `binascii.crc32`. The frame size is 32 bytes with no padding.

## Status bits

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `PF_STATUS_SPIKE` | The fault engine added a three-amplitude impulse. |
| 1 | `PF_STATUS_FREEZE` | The previous delivered signal value was repeated. |
| 2 | `PF_STATUS_OVERRUN` | An unread frame was evicted before this frame entered the full ring. |

Unknown bits must be preserved by forward-compatible consumers.

## Control ABI

The device accepts four ioctls:

- `PF_IOC_SET_CONFIG`: atomically replace configuration and reset deterministic state.
- `PF_IOC_GET_CONFIG`: retrieve the active configuration.
- `PF_IOC_GET_STATS`: retrieve generated, delivered, dropped, overrun, spike and freeze counters.
- `PF_IOC_RESET`: clear the ring, counters, sequence and PRNG state.

Invalid periods, amplitudes, noise levels and drift rates return `EINVAL`. Unknown commands return `ENOTTY`.

## Incident identity

An incident fingerprint is SHA-256 over the concatenated packed frames in delivery order. It identifies the observable trace, not just its configuration. Changing one value, status bit, CRC or frame order changes the fingerprint.

