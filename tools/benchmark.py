#!/usr/bin/env python3
"""Small dependency-free throughput benchmark for the digital twin."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
from time import perf_counter

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from pulseforge_sim import Config, Simulator  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--attempts", type=int, default=250_000)
    args = parser.parse_args()

    simulator = Simulator(Config(spike_every=97, drop_every=101, freeze_every=211))
    digest = hashlib.sha256()
    started = perf_counter()
    for sample in simulator.run(args.attempts):
        digest.update(sample.pack())
    elapsed = perf_counter() - started

    print(json.dumps({
        "attempts": args.attempts,
        "delivered": simulator.stats.delivered,
        "elapsed_seconds": round(elapsed, 6),
        "attempts_per_second": round(args.attempts / elapsed),
        "incident_fingerprint": digest.hexdigest(),
    }, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

