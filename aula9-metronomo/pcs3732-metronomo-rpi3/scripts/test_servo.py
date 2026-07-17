#!/usr/bin/env python3
"""Teste isolado do servomotor SG90."""
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
from metronome import Config, PigpioBackend  # noqa: E402


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--gpio", type=int, default=17)
    p.add_argument("--seconds", type=float, default=1.0)
    p.add_argument("--min-pulse", type=int, default=1000)
    p.add_argument("--max-pulse", type=int, default=2000)
    args = p.parse_args()
    cfg = Config(
        servo_gpio=args.gpio,
        servo_min_pulse_us=args.min_pulse,
        servo_max_pulse_us=args.max_pulse,
    )
    hw = PigpioBackend(cfg)
    try:
        for angle in (-60, -30, 0, 30, 60, 0):
            print(f"Servo: {angle:+d} graus")
            hw.set_servo_angle(angle)
            time.sleep(args.seconds)
    finally:
        hw.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
