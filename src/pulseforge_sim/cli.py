"""Command-line interface for generation, replay and incident analysis."""

from __future__ import annotations

import argparse
from contextlib import nullcontext
import json
from pathlib import Path
import sys
from typing import TextIO

from .model import Config, Simulator
from .protocol import Sample, fingerprint


def _add_config_arguments(parser: argparse.ArgumentParser) -> None:
    defaults = Config()
    parser.add_argument("--period-us", type=int, default=defaults.period_us)
    parser.add_argument("--amplitude", type=int, default=defaults.amplitude_milli)
    parser.add_argument("--noise", type=int, default=defaults.noise_milli)
    parser.add_argument("--spike-every", type=int, default=defaults.spike_every)
    parser.add_argument("--drop-every", type=int, default=defaults.drop_every)
    parser.add_argument("--freeze-every", type=int, default=defaults.freeze_every)
    parser.add_argument("--drift-ppm", type=int, default=defaults.drift_ppm)
    parser.add_argument("--seed", type=lambda value: int(value, 0), default=defaults.seed)


def _config_from_args(args: argparse.Namespace) -> Config:
    return Config(
        period_us=args.period_us,
        amplitude_milli=args.amplitude,
        noise_milli=args.noise,
        spike_every=args.spike_every,
        drop_every=args.drop_every,
        freeze_every=args.freeze_every,
        drift_ppm=args.drift_ppm,
        seed=args.seed,
    )


def _output_context(path: str | None):
    if path is None or path == "-":
        return nullcontext(sys.stdout)
    return Path(path).open("w", encoding="utf-8", newline="\n")


def generate(args: argparse.Namespace) -> int:
    simulator = Simulator(_config_from_args(args))
    samples = list(simulator.run(args.attempts))
    with _output_context(args.output) as stream:
        output: TextIO = stream
        for sample in samples:
            output.write(json.dumps(sample.to_dict(), separators=(",", ":")) + "\n")

    summary = {
        "config": simulator.config.to_dict(),
        "stats": simulator.stats.to_dict(),
        "incident_fingerprint": fingerprint(samples),
    }
    print(json.dumps(summary, sort_keys=True), file=sys.stderr)
    return 0


def analyze(args: argparse.Namespace) -> int:
    samples: list[Sample] = []
    with Path(args.input).open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, start=1):
            if not line.strip():
                continue
            record = json.loads(line)
            try:
                sample = Sample(
                    device_time_ns=record["device_time_ns"],
                    sequence=record["sequence"],
                    signal_milli=record["signal_milli"],
                    temperature_milli=record["temperature_milli"],
                    status=record["status"],
                    crc32=record["crc32"],
                )
                sample.verify_crc()
            except (KeyError, TypeError, ValueError) as exc:
                raise SystemExit(f"invalid record on line {line_number}: {exc}") from exc
            samples.append(sample)

    sequence_gaps = sum(
        max(0, current.sequence - previous.sequence - 1)
        for previous, current in zip(samples, samples[1:])
    )
    report = {
        "frames": len(samples),
        "first_sequence": samples[0].sequence if samples else None,
        "last_sequence": samples[-1].sequence if samples else None,
        "sequence_gaps": sequence_gaps,
        "flagged_frames": sum(bool(sample.status) for sample in samples),
        "incident_fingerprint": fingerprint(samples),
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="pulseforge",
        description="Fault-injectable virtual telemetry device digital twin",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    generate_parser = subparsers.add_parser("generate", help="generate a deterministic trace")
    generate_parser.add_argument("--attempts", type=int, default=100)
    generate_parser.add_argument("--output", help="NDJSON path; defaults to stdout")
    _add_config_arguments(generate_parser)
    generate_parser.set_defaults(handler=generate)

    analyze_parser = subparsers.add_parser("analyze", help="validate and summarize an NDJSON trace")
    analyze_parser.add_argument("input")
    analyze_parser.set_defaults(handler=analyze)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.handler(args)
    except ValueError as exc:
        parser.error(str(exc))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
