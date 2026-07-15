from __future__ import annotations

import threading
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from metronome import BeatScheduler, Config, TempoState


class FakeClock:
    def __init__(self) -> None:
        self.now_ns = 0

    def monotonic_ns(self) -> int:
        return self.now_ns

    def wall_time_ns(self) -> int:
        return self.now_ns

    def sleep(self, seconds: float) -> None:
        self.now_ns += round(seconds * 1_000_000_000)


def test_tempo_state_limits_and_toggle() -> None:
    cfg = Config(bpm=60, min_bpm=30, max_bpm=70, bpm_step=5)
    state = TempoState(cfg)
    for _ in range(10):
        state.increase()
    assert state.snapshot()[0] == 70
    for _ in range(20):
        state.decrease()
    assert state.snapshot()[0] == 30
    old = state.snapshot()[1]
    assert state.toggle_buzzer() is (not old)


def test_absolute_scheduler_does_not_accumulate_callback_time() -> None:
    cfg = Config(bpm=60)
    state = TempoState(cfg)
    clock = FakeClock()
    stop = threading.Event()
    errors = []

    def on_beat(_beat: int, deadline: int, actual: int, _bpm: int) -> None:
        errors.append(actual - deadline)
        # Simula 2 ms de trabalho em cada iteração.
        clock.now_ns += 2_000_000

    scheduler = BeatScheduler(
        state,
        on_beat,
        stop,
        clock=clock,
        busy_wait_ns=0,
        period_override_ns=20_000_000,
    )
    scheduler.run(max_beats=100)
    assert max(abs(e) for e in errors) == 0
    assert clock.now_ns == 1_982_000_000
