"""Deterministic reference model for the PulseForge virtual device."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Iterator

from .protocol import STATUS_FREEZE, STATUS_SPIKE, Sample


_FALLBACK_SEED = 0x6D2B79F5


def _trunc_div(numerator: int, denominator: int) -> int:
    """Integer division with C99 truncation toward zero."""

    quotient = abs(numerator) // abs(denominator)
    return -quotient if (numerator < 0) != (denominator < 0) else quotient


@dataclass(frozen=True, slots=True)
class Config:
    period_us: int = 10_000
    amplitude_milli: int = 1_000
    noise_milli: int = 25
    spike_every: int = 0
    drop_every: int = 0
    freeze_every: int = 0
    drift_ppm: int = 0
    seed: int = 0x00C0FFEE

    def validate(self) -> None:
        if not 100 <= self.period_us <= 10_000_000:
            raise ValueError("period_us must be between 100 and 10,000,000")
        if not 1 <= self.amplitude_milli <= 1_000_000:
            raise ValueError("amplitude_milli must be between 1 and 1,000,000")
        if not 0 <= self.noise_milli <= self.amplitude_milli:
            raise ValueError("noise_milli must be between 0 and amplitude_milli")
        for name in ("spike_every", "drop_every", "freeze_every"):
            if getattr(self, name) < 0:
                raise ValueError(f"{name} cannot be negative")
        if not -1_000_000 <= self.drift_ppm <= 1_000_000:
            raise ValueError("drift_ppm must be between -1,000,000 and 1,000,000")
        if not 0 <= self.seed <= 0xFFFFFFFF:
            raise ValueError("seed must fit in uint32")

    def to_dict(self) -> dict[str, int]:
        return asdict(self)


@dataclass(slots=True)
class Stats:
    generated: int = 0
    delivered: int = 0
    dropped: int = 0
    spikes: int = 0
    freezes: int = 0

    def to_dict(self) -> dict[str, int]:
        return asdict(self)


class Simulator:
    """A bit-exact, deterministic digital twin of the kernel device."""

    def __init__(self, config: Config | None = None) -> None:
        self.config = config or Config()
        self.config.validate()
        self.stats = Stats()
        self._rng = self.config.seed or _FALLBACK_SEED
        self._last_value = 0

    def _next_random(self) -> int:
        state = self._rng
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= state >> 17
        state ^= (state << 5) & 0xFFFFFFFF
        self._rng = state & 0xFFFFFFFF
        return self._rng

    def _triangle(self, sequence: int) -> int:
        amplitude = self.config.amplitude_milli
        phase = sequence % 200
        if phase < 100:
            return -amplitude + (2 * amplitude * phase) // 100
        return amplitude - (2 * amplitude * (phase - 100)) // 100

    def step(self) -> Sample | None:
        """Advance one device period, returning None for an injected drop."""

        cfg = self.config
        sequence = self.stats.generated
        self.stats.generated += 1

        random_word = self._next_random()
        noise_span = 2 * cfg.noise_milli + 1
        noise = random_word % noise_span - cfg.noise_milli
        drift = _trunc_div(cfg.amplitude_milli * cfg.drift_ppm * sequence, 1_000_000)
        drift = max(-(2**31), min(2**31 - 1, drift))
        value = self._triangle(sequence) + noise + drift

        if cfg.drop_every and (sequence + 1) % cfg.drop_every == 0:
            self.stats.dropped += 1
            return None

        status = 0
        if cfg.spike_every and (sequence + 1) % cfg.spike_every == 0:
            value += 3 * cfg.amplitude_milli
            status |= STATUS_SPIKE
            self.stats.spikes += 1

        if cfg.freeze_every and (sequence + 1) % cfg.freeze_every == 0:
            value = self._last_value
            status |= STATUS_FREEZE
            self.stats.freezes += 1

        value = max(-(2**31), min(2**31 - 1, value))
        self._last_value = value
        self.stats.delivered += 1
        return Sample(
            device_time_ns=sequence * cfg.period_us * 1_000,
            sequence=sequence,
            signal_milli=value,
            temperature_milli=25_000 + _trunc_div(value, 20),
            status=status,
        ).with_crc()

    def run(self, attempts: int) -> Iterator[Sample]:
        if attempts < 0:
            raise ValueError("attempts cannot be negative")
        for _ in range(attempts):
            sample = self.step()
            if sample is not None:
                yield sample
