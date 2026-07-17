#!/usr/bin/env python3
"""Compara temporização relativa (sleep) e absoluta (deadline monotônico)."""
from __future__ import annotations

import argparse
import json
import statistics
import time


def run_naive(cycles: int, period_s: float, work_s: float) -> list[float]:
    start = time.monotonic_ns()
    errors_ms = []
    for i in range(cycles):
        actual = time.monotonic_ns()
        expected = start + round(i * period_s * 1e9)
        errors_ms.append((actual - expected) / 1e6)
        time.sleep(work_s)
        time.sleep(period_s)
    return errors_ms


def sleep_until(deadline_ns: int) -> None:
    while True:
        remaining = deadline_ns - time.monotonic_ns()
        if remaining <= 0:
            return
        if remaining > 500_000:
            time.sleep((remaining - 250_000) / 1e9)


def run_absolute(cycles: int, period_s: float, work_s: float) -> list[float]:
    start = time.monotonic_ns()
    period_ns = round(period_s * 1e9)
    errors_ms = []
    for i in range(cycles):
        deadline = start + i * period_ns
        sleep_until(deadline)
        actual = time.monotonic_ns()
        errors_ms.append((actual - deadline) / 1e6)
        time.sleep(work_s)
    return errors_ms


def stats(values: list[float]) -> dict[str, float]:
    return {
        "mean_ms": statistics.fmean(values),
        "stdev_ms": statistics.pstdev(values),
        "max_ms": max(values),
        "final_ms": values[-1],
    }


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--cycles", type=int, default=80)
    p.add_argument("--period-ms", type=float, default=30.0)
    p.add_argument("--work-ms", type=float, default=2.0)
    args = p.parse_args()
    period_s = args.period_ms / 1000
    work_s = args.work_ms / 1000

    naive = run_naive(args.cycles, period_s, work_s)
    absolute = run_absolute(args.cycles, period_s, work_s)

    summary = {
        "environment": "Linux de desenvolvimento; não representa medição física no Raspberry Pi 3",
        "cycles": args.cycles,
        "period_ms": args.period_ms,
        "work_ms": args.work_ms,
        "naive": stats(naive),
        "absolute": stats(absolute),
    }
    print(json.dumps(summary, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
