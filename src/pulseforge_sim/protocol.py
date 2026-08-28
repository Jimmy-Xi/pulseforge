"""Bit-exact PulseForge wire protocol shared with the Linux driver."""

from __future__ import annotations

from dataclasses import dataclass
import binascii
import hashlib
import struct
from typing import Iterable


STATUS_SPIKE = 1 << 0
STATUS_FREEZE = 1 << 1
STATUS_OVERRUN = 1 << 2

_PAYLOAD = struct.Struct("<QQiiI")
_FRAME = struct.Struct("<QQiiII")


class StreamCorruptionError(ValueError):
    """Raised when a binary frame fails its CRC check."""


@dataclass(frozen=True, slots=True)
class Sample:
    """One 32-byte telemetry frame."""

    device_time_ns: int
    sequence: int
    signal_milli: int
    temperature_milli: int
    status: int
    crc32: int = 0

    @property
    def status_labels(self) -> list[str]:
        labels: list[str] = []
        if self.status & STATUS_SPIKE:
            labels.append("spike")
        if self.status & STATUS_FREEZE:
            labels.append("freeze")
        if self.status & STATUS_OVERRUN:
            labels.append("overrun")
        return labels

    def payload(self) -> bytes:
        return _PAYLOAD.pack(
            self.device_time_ns,
            self.sequence,
            self.signal_milli,
            self.temperature_milli,
            self.status,
        )

    def with_crc(self) -> "Sample":
        return Sample(
            self.device_time_ns,
            self.sequence,
            self.signal_milli,
            self.temperature_milli,
            self.status,
            binascii.crc32(self.payload()) & 0xFFFFFFFF,
        )

    def pack(self) -> bytes:
        sample = self if self.crc32 else self.with_crc()
        return _FRAME.pack(
            sample.device_time_ns,
            sample.sequence,
            sample.signal_milli,
            sample.temperature_milli,
            sample.status,
            sample.crc32,
        )

    def to_dict(self) -> dict[str, int | list[str]]:
        sample = self if self.crc32 else self.with_crc()
        return {
            "device_time_ns": sample.device_time_ns,
            "sequence": sample.sequence,
            "signal_milli": sample.signal_milli,
            "temperature_milli": sample.temperature_milli,
            "status": sample.status,
            "status_labels": sample.status_labels,
            "crc32": sample.crc32,
        }

    @classmethod
    def unpack(cls, frame: bytes, *, verify: bool = True) -> "Sample":
        if len(frame) != _FRAME.size:
            raise ValueError(f"expected {_FRAME.size} bytes, got {len(frame)}")
        sample = cls(*_FRAME.unpack(frame))
        expected = binascii.crc32(sample.payload()) & 0xFFFFFFFF
        if verify and sample.crc32 != expected:
            raise StreamCorruptionError(
                f"CRC mismatch at sequence {sample.sequence}: "
                f"got 0x{sample.crc32:08x}, expected 0x{expected:08x}"
            )
        return sample


def frame_size() -> int:
    return _FRAME.size


def fingerprint(samples: Iterable[Sample]) -> str:
    """Return a stable incident fingerprint for a sequence of frames."""

    digest = hashlib.sha256()
    for sample in samples:
        digest.update(sample.pack())
    return digest.hexdigest()

