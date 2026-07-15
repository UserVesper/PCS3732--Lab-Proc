#!/usr/bin/env python3
"""Metrônomo para Raspberry Pi 3 usando pigpio.

Projeto acadêmico: LED por PWM, servomotor, buzzer, botões físicos e
agendamento periódico com prazos absolutos baseados em CLOCK_MONOTONIC.

O módulo também possui um backend simulado (``--dry-run``), permitindo
validar a lógica sem acesso ao hardware.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import queue
import signal
import tempfile
import threading
import time
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Callable, Optional, Protocol


class Clock(Protocol):
    def monotonic_ns(self) -> int: ...
    def wall_time_ns(self) -> int: ...
    def sleep(self, seconds: float) -> None: ...


class RealClock:
    def monotonic_ns(self) -> int:
        return time.monotonic_ns()

    def wall_time_ns(self) -> int:
        return time.time_ns()

    def sleep(self, seconds: float) -> None:
        if seconds > 0:
            time.sleep(seconds)


@dataclass(slots=True)
class Config:
    bpm: int = 60
    min_bpm: int = 30
    max_bpm: int = 240
    bpm_step: int = 5
    pulse_ms: int = 70
    led_frequency_hz: int = 1000
    buzzer_frequency_hz: int = 2000
    passive_buzzer: bool = False
    buzzer_enabled: bool = True
    servo_left_deg: float = -35.0
    servo_right_deg: float = 35.0
    servo_min_pulse_us: int = 1000
    servo_max_pulse_us: int = 2000
    servo_min_angle: float = -90.0
    servo_max_angle: float = 90.0
    led_gpio: int = 18
    servo_gpio: int = 17
    buzzer_gpio: int = 27
    button_up_gpio: int = 22
    button_down_gpio: int = 23
    button_toggle_gpio: int = 24
    debounce_us: int = 50_000
    state_file: str = "/var/lib/rpi3-metronomo/state.json"
    timing_csv: str = "/var/log/rpi3-metronomo/timing.csv"


class TempoState:
    """Estado concorrente do metrônomo, protegido por lock."""

    def __init__(self, config: Config):
        self._lock = threading.Lock()
        self._min_bpm = config.min_bpm
        self._max_bpm = config.max_bpm
        self._step = config.bpm_step
        self._bpm = max(self._min_bpm, min(config.bpm, self._max_bpm))
        self._buzzer_enabled = config.buzzer_enabled

    def snapshot(self) -> tuple[int, bool]:
        with self._lock:
            return self._bpm, self._buzzer_enabled

    def change_bpm(self, delta: int) -> int:
        with self._lock:
            self._bpm = max(self._min_bpm, min(self._max_bpm, self._bpm + delta))
            return self._bpm

    def increase(self) -> int:
        return self.change_bpm(self._step)

    def decrease(self) -> int:
        return self.change_bpm(-self._step)

    def toggle_buzzer(self) -> bool:
        with self._lock:
            self._buzzer_enabled = not self._buzzer_enabled
            return self._buzzer_enabled

    def restore(self, bpm: int, buzzer_enabled: bool) -> None:
        with self._lock:
            self._bpm = max(self._min_bpm, min(int(bpm), self._max_bpm))
            self._buzzer_enabled = bool(buzzer_enabled)

    def to_dict(self) -> dict[str, object]:
        bpm, enabled = self.snapshot()
        return {"bpm": bpm, "buzzer_enabled": enabled}


class HardwareBackend(Protocol):
    def set_led(self, value: float) -> None: ...
    def set_servo_angle(self, angle_deg: float) -> None: ...
    def buzzer_on(self) -> None: ...
    def buzzer_off(self) -> None: ...
    def register_button(self, gpio: int, callback: Callable[[], None]) -> None: ...
    def close(self) -> None: ...


class PigpioBackend:
    """Backend real. O daemon ``pigpiod`` deve estar ativo."""

    def __init__(self, config: Config):
        try:
            import pigpio  # type: ignore
        except ImportError as exc:
            raise RuntimeError(
                "Módulo pigpio ausente. Instale: sudo apt install pigpio python3-pigpio"
            ) from exc

        self._pigpio = pigpio
        self._config = config
        self._pi = pigpio.pi()
        if not self._pi.connected:
            raise RuntimeError("Não foi possível conectar ao pigpiod. Execute: sudo systemctl enable --now pigpiod")

        self._callbacks = []
        self._pi.set_mode(config.led_gpio, pigpio.OUTPUT)
        self._pi.set_mode(config.servo_gpio, pigpio.OUTPUT)
        self._pi.set_mode(config.buzzer_gpio, pigpio.OUTPUT)
        self._pi.write(config.buzzer_gpio, 0)
        self._pi.set_servo_pulsewidth(config.servo_gpio, 0)
        self.set_led(0.0)

    def set_led(self, value: float) -> None:
        value = max(0.0, min(1.0, float(value)))
        duty = int(round(value * 1_000_000))
        # GPIO18 expõe PWM0 por hardware no RPi3. Para outro GPIO, usa PWM
        # amostrado por DMA do pigpio.
        if self._config.led_gpio in (12, 13, 18, 19):
            rc = self._pi.hardware_PWM(
                self._config.led_gpio,
                self._config.led_frequency_hz,
                duty,
            )
            if rc < 0:
                raise RuntimeError(f"hardware_PWM falhou: código {rc}")
        else:
            self._pi.set_PWM_frequency(self._config.led_gpio, self._config.led_frequency_hz)
            self._pi.set_PWM_range(self._config.led_gpio, 255)
            self._pi.set_PWM_dutycycle(self._config.led_gpio, round(value * 255))

    def set_servo_angle(self, angle_deg: float) -> None:
        cfg = self._config
        angle = max(cfg.servo_min_angle, min(cfg.servo_max_angle, float(angle_deg)))
        fraction = (angle - cfg.servo_min_angle) / (cfg.servo_max_angle - cfg.servo_min_angle)
        pulse = round(cfg.servo_min_pulse_us + fraction * (cfg.servo_max_pulse_us - cfg.servo_min_pulse_us))
        rc = self._pi.set_servo_pulsewidth(cfg.servo_gpio, pulse)
        if rc < 0:
            raise RuntimeError(f"set_servo_pulsewidth falhou: código {rc}")

    def buzzer_on(self) -> None:
        cfg = self._config
        if cfg.passive_buzzer:
            self._pi.set_PWM_frequency(cfg.buzzer_gpio, cfg.buzzer_frequency_hz)
            self._pi.set_PWM_range(cfg.buzzer_gpio, 255)
            self._pi.set_PWM_dutycycle(cfg.buzzer_gpio, 128)
        else:
            self._pi.write(cfg.buzzer_gpio, 1)

    def buzzer_off(self) -> None:
        cfg = self._config
        if cfg.passive_buzzer:
            self._pi.set_PWM_dutycycle(cfg.buzzer_gpio, 0)
        else:
            self._pi.write(cfg.buzzer_gpio, 0)

    def register_button(self, gpio: int, callback: Callable[[], None]) -> None:
        p = self._pigpio
        self._pi.set_mode(gpio, p.INPUT)
        self._pi.set_pull_up_down(gpio, p.PUD_UP)
        self._pi.set_glitch_filter(gpio, self._config.debounce_us)

        def _wrapper(_gpio: int, level: int, _tick: int) -> None:
            if level == 0:  # borda de descida, botão ligado ao GND
                callback()

        cb = self._pi.callback(gpio, p.FALLING_EDGE, _wrapper)
        self._callbacks.append(cb)

    def close(self) -> None:
        for cb in self._callbacks:
            cb.cancel()
        try:
            self.buzzer_off()
            self.set_led(0.0)
            self._pi.set_servo_pulsewidth(self._config.servo_gpio, 0)
        finally:
            self._pi.stop()


class MockBackend:
    """Backend para testes sem GPIO; armazena eventos com timestamp."""

    def __init__(self, clock: Clock | None = None):
        self.clock = clock or RealClock()
        self.events: list[tuple[int, str, float | int | str]] = []
        self.buttons: dict[int, Callable[[], None]] = {}
        self.closed = False

    def _record(self, name: str, value: float | int | str) -> None:
        self.events.append((self.clock.monotonic_ns(), name, value))

    def set_led(self, value: float) -> None:
        self._record("led", round(float(value), 3))

    def set_servo_angle(self, angle_deg: float) -> None:
        self._record("servo", round(float(angle_deg), 2))

    def buzzer_on(self) -> None:
        self._record("buzzer", "on")

    def buzzer_off(self) -> None:
        self._record("buzzer", "off")

    def register_button(self, gpio: int, callback: Callable[[], None]) -> None:
        self.buttons[gpio] = callback

    def press(self, gpio: int) -> None:
        self.buttons[gpio]()

    def close(self) -> None:
        self.closed = True
        self._record("backend", "closed")


class TimingLogger:
    def __init__(self, path: str | Path | None):
        self.path = Path(path) if path else None
        self._lock = threading.Lock()
        self._initialized = False

    def log(self, beat: int, scheduled_ns: int, actual_ns: int, bpm: int) -> None:
        if self.path is None:
            return
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            with self._lock:
                mode = "a" if self._initialized or self.path.exists() else "w"
                with self.path.open(mode, encoding="utf-8") as f:
                    if mode == "w":
                        f.write("beat,scheduled_ns,actual_ns,error_us,bpm\n")
                    error_us = (actual_ns - scheduled_ns) / 1000.0
                    f.write(f"{beat},{scheduled_ns},{actual_ns},{error_us:.3f},{bpm}\n")
                self._initialized = True
        except OSError as exc:
            print(f"Aviso: não foi possível registrar temporização: {exc}")


class ActuatorWorker:
    """Executa a atuação sem bloquear o laço de temporização."""

    def __init__(self, hardware: HardwareBackend, state: TempoState, config: Config, clock: Clock):
        self.hardware = hardware
        self.state = state
        self.config = config
        self.clock = clock
        self._queue: queue.Queue[int | None] = queue.Queue(maxsize=4)
        self._thread = threading.Thread(target=self._run, name="actuators", daemon=True)
        self._running = threading.Event()
        self._started = False
        self.dropped_beats = 0

    def start(self) -> None:
        if self._started:
            return
        self._running.set()
        self._thread.start()
        self._started = True

    def trigger(self, beat_number: int) -> None:
        try:
            self._queue.put_nowait(beat_number)
        except queue.Full:
            self.dropped_beats += 1

    def _run(self) -> None:
        while self._running.is_set():
            item = self._queue.get()
            if item is None:
                break
            _, buzzer_enabled = self.state.snapshot()
            angle = self.config.servo_left_deg if item % 2 else self.config.servo_right_deg
            self.hardware.set_servo_angle(angle)
            self.hardware.set_led(1.0)
            if buzzer_enabled:
                self.hardware.buzzer_on()
            self.clock.sleep(self.config.pulse_ms / 1000.0)
            self.hardware.set_led(0.12)  # brilho de repouso
            if buzzer_enabled:
                self.hardware.buzzer_off()

    def stop(self) -> None:
        if not self._started:
            return
        self._running.clear()
        try:
            self._queue.put_nowait(None)
        except queue.Full:
            # Descarta um evento antigo para garantir o encerramento.
            try:
                self._queue.get_nowait()
            except queue.Empty:
                pass
            self._queue.put_nowait(None)
        self._thread.join(timeout=2.0)


class BeatScheduler:
    """Agendador periódico com prazos absolutos, evitando drift acumulado."""

    def __init__(
        self,
        state: TempoState,
        on_beat: Callable[[int, int, int, int], None],
        stop_event: threading.Event,
        clock: Clock | None = None,
        busy_wait_ns: int = 250_000,
        period_override_ns: int | None = None,
    ):
        self.state = state
        self.on_beat = on_beat
        self.stop_event = stop_event
        self.clock = clock or RealClock()
        self.busy_wait_ns = max(0, busy_wait_ns)
        self.period_override_ns = period_override_ns

    def _wait_until(self, deadline_ns: int) -> None:
        while not self.stop_event.is_set():
            remaining = deadline_ns - self.clock.monotonic_ns()
            if remaining <= 0:
                return
            if remaining > self.busy_wait_ns:
                sleep_ns = remaining - self.busy_wait_ns
                self.clock.sleep(sleep_ns / 1_000_000_000)
            elif self.busy_wait_ns == 0:
                self.clock.sleep(remaining / 1_000_000_000)
            else:
                # Espera ativa curta para reduzir o overshoot do escalonador.
                pass

    def run(self, max_beats: int | None = None) -> None:
        beat = 0
        deadline = self.clock.monotonic_ns()
        while not self.stop_event.is_set() and (max_beats is None or beat < max_beats):
            bpm, _ = self.state.snapshot()
            period_ns = self.period_override_ns or round(60_000_000_000 / bpm)
            if beat == 0:
                deadline = self.clock.monotonic_ns()
            else:
                deadline += period_ns
                self._wait_until(deadline)
            actual = self.clock.monotonic_ns()
            beat += 1
            self.on_beat(beat, deadline, actual, bpm)

            # Caso o processo tenha atrasado mais que um período, evita rajada de
            # eventos atrasados e reinicia a fase a partir do instante atual.
            if actual - deadline > period_ns:
                deadline = actual


def atomic_save_json(path: str | Path, data: dict[str, object]) -> None:
    """Grava JSON por arquivo temporário + fsync + replace atômico."""
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=target.name + ".", dir=target.parent)
    tmp = Path(tmp_name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
            f.write("\n")
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, target)
        try:
            dir_fd = os.open(target.parent, os.O_DIRECTORY)
            try:
                os.fsync(dir_fd)
            finally:
                os.close(dir_fd)
        except (AttributeError, OSError):
            pass
    finally:
        if tmp.exists():
            tmp.unlink(missing_ok=True)


def load_state(path: str | Path, state: TempoState) -> None:
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
        state.restore(int(data["bpm"]), bool(data["buzzer_enabled"]))
    except FileNotFoundError:
        return
    except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError) as exc:
        print(f"Aviso: estado persistido inválido; usando padrão: {exc}")


def configure_process(rt_priority: int | None, cpu_core: int | None) -> None:
    if cpu_core is not None:
        try:
            os.sched_setaffinity(0, {cpu_core})
        except (AttributeError, OSError) as exc:
            print(f"Aviso: não foi possível fixar afinidade na CPU {cpu_core}: {exc}")

    if rt_priority is not None:
        if not (1 <= rt_priority <= 99):
            raise ValueError("rt_priority deve estar entre 1 e 99")
        try:
            os.sched_setscheduler(0, os.SCHED_FIFO, os.sched_param(rt_priority))
        except (AttributeError, PermissionError, OSError) as exc:
            print(f"Aviso: prioridade SCHED_FIFO não aplicada: {exc}")


def wait_for_wall_time(iso_datetime: str, clock: Clock) -> None:
    target = datetime.fromisoformat(iso_datetime)
    if target.tzinfo is None:
        raise ValueError("--start-at exige fuso horário, por exemplo 2026-07-15T20:00:00-03:00")
    target_ns = int(target.timestamp() * 1_000_000_000)
    while True:
        remaining = target_ns - clock.wall_time_ns()
        if remaining <= 0:
            return
        clock.sleep(min(remaining / 1_000_000_000, 1.0))


class MetronomeApp:
    def __init__(self, config: Config, hardware: HardwareBackend, clock: Clock | None = None):
        self.config = config
        self.clock = clock or RealClock()
        self.hardware = hardware
        self.state = TempoState(config)
        self.stop_event = threading.Event()
        self.logger = TimingLogger(config.timing_csv)
        self.worker = ActuatorWorker(hardware, self.state, config, self.clock)
        self.scheduler = BeatScheduler(self.state, self._on_beat, self.stop_event, self.clock)
        self._stopped = False

    def _persist(self) -> None:
        try:
            atomic_save_json(self.config.state_file, self.state.to_dict())
        except OSError as exc:
            print(f"Aviso: falha ao persistir estado: {exc}")

    def _on_up(self) -> None:
        bpm = self.state.increase()
        self._persist()
        print(f"BPM alterado para {bpm}")

    def _on_down(self) -> None:
        bpm = self.state.decrease()
        self._persist()
        print(f"BPM alterado para {bpm}")

    def _on_toggle(self) -> None:
        enabled = self.state.toggle_buzzer()
        self._persist()
        print(f"Buzzer {'ativado' if enabled else 'desativado'}")

    def _on_beat(self, beat: int, scheduled_ns: int, actual_ns: int, bpm: int) -> None:
        self.logger.log(beat, scheduled_ns, actual_ns, bpm)
        self.worker.trigger(beat)
        error_ms = (actual_ns - scheduled_ns) / 1_000_000
        print(f"beat={beat:04d} bpm={bpm:3d} erro={error_ms:+.3f} ms")

    def setup(self) -> None:
        load_state(self.config.state_file, self.state)
        self.hardware.register_button(self.config.button_up_gpio, self._on_up)
        self.hardware.register_button(self.config.button_down_gpio, self._on_down)
        self.hardware.register_button(self.config.button_toggle_gpio, self._on_toggle)
        self.hardware.set_led(0.12)
        self.hardware.set_servo_angle(0.0)
        self.worker.start()

    def run(self, max_beats: int | None = None) -> None:
        self.setup()
        self.scheduler.run(max_beats=max_beats)

    def stop(self) -> None:
        if self._stopped:
            return
        self._stopped = True
        self.stop_event.set()
        self.worker.stop()
        self._persist()
        self.hardware.close()


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Metrônomo com Raspberry Pi 3, PWM, servo, buzzer e botões")
    p.add_argument("--bpm", type=int, default=60, help="BPM inicial (30-240)")
    p.add_argument("--passive-buzzer", action="store_true", help="gera tom PWM; omita para buzzer ativo")
    p.add_argument("--no-buzzer", action="store_true", help="inicia com buzzer desativado")
    p.add_argument("--dry-run", action="store_true", help="executa sem GPIO e imprime eventos")
    p.add_argument("--beats", type=int, default=None, help="encerra após N batidas")
    p.add_argument("--start-at", type=str, help="horário ISO 8601 com fuso para a primeira batida")
    p.add_argument("--rt-priority", type=int, default=None, help="prioridade SCHED_FIFO opcional (requer permissão)")
    p.add_argument("--cpu-core", type=int, default=None, help="fixa o processo em um núcleo")
    p.add_argument("--state-file", type=str, default=None)
    p.add_argument("--timing-csv", type=str, default=None)
    return p


def main(argv: Optional[list[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    cfg = Config(
        bpm=args.bpm,
        passive_buzzer=args.passive_buzzer,
        buzzer_enabled=not args.no_buzzer,
    )
    if args.state_file:
        cfg.state_file = args.state_file
    if args.timing_csv:
        cfg.timing_csv = args.timing_csv

    configure_process(args.rt_priority, args.cpu_core)
    clock = RealClock()
    hardware: HardwareBackend = MockBackend(clock) if args.dry_run else PigpioBackend(cfg)
    app = MetronomeApp(cfg, hardware, clock)

    def _request_stop(_signum: int, _frame: object) -> None:
        app.stop_event.set()

    signal.signal(signal.SIGINT, _request_stop)
    signal.signal(signal.SIGTERM, _request_stop)

    try:
        if args.start_at:
            wait_for_wall_time(args.start_at, clock)
        app.run(max_beats=args.beats)
    finally:
        app.stop()

    if isinstance(hardware, MockBackend):
        print(f"Eventos simulados: {len(hardware.events)}")
        for event in hardware.events[-12:]:
            print(event)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
