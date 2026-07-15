#!/usr/bin/env python3
"""Teste isolado de buzzer ativo ou passivo."""
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
from metronome import Config, PigpioBackend  # noqa: E402


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--gpio", type=int, default=27)
    p.add_argument("--passive", action="store_true")
    p.add_argument("--pulse-ms", type=int, default=120)
    args = p.parse_args()
    cfg = Config(buzzer_gpio=args.gpio, passive_buzzer=args.passive)
    hw = PigpioBackend(cfg)
    try:
        frequencies = (880, 1320, 1760, 2200) if args.passive else (0, 0, 0, 0)
        for freq in frequencies:
            if args.passive:
                cfg.buzzer_frequency_hz = freq
                print(f"Buzzer passivo: {freq} Hz")
            else:
                print("Buzzer ativo: pulso")
            hw.buzzer_on()
            time.sleep(args.pulse_ms / 1000)
            hw.buzzer_off()
            time.sleep(0.4)
    finally:
        hw.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
