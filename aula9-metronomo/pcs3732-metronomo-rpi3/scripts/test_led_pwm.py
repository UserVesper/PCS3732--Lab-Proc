#!/usr/bin/env python3
"""Teste isolado do LED em diversas frequências e duty cycles."""
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
from metronome import Config, PigpioBackend  # noqa: E402


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--gpio", type=int, default=18)
    p.add_argument("--seconds", type=float, default=0.6)
    args = p.parse_args()
    cfg = Config(led_gpio=args.gpio)
    hw = PigpioBackend(cfg)
    try:
        for frequency in (50, 100, 500, 1000, 5000, 10000):
            cfg.led_frequency_hz = frequency
            for duty in (0.1, 0.5, 0.9):
                print(f"LED: f={frequency:5d} Hz, duty={duty:0.1f}")
                hw.set_led(duty)
                time.sleep(args.seconds)
        hw.set_led(0.0)
    finally:
        hw.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
