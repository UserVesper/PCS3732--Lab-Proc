#!/usr/bin/env python3
"""Metrônomo para Raspberry Pi 3 com Freenove Projects Board v1.2.

Biblioteca de GPIO: gpiozero (não utiliza pigpio nem o daemon pigpiod).

Mapeamento BCM da placa Freenove usado neste programa:
    LED azul integrado ............... GPIO17
    Servo (pino SIG do conector) ..... GPIO18
    Buzzer ativo integrado ........... GPIO12
    Buzzer passivo integrado ......... GPIO4  (opção --passive-buzzer)
    Botão S1 / BPM + ................. GPIO21
    Botão S2 / BPM - ................. GPIO20
    Botão S3 / liga/desliga buzzer ... GPIO16
    Botão S4 ......................... GPIO26 (não utilizado)

Antes de executar, ajuste as chaves físicas da placa:
    - função 2 (Button): quatro chaves ligadas;
    - função 5 (Blue LED): ligada;
    - função 3 (Active Buzzer): ligada, quando usar o buzzer ativo.

Restrições da placa:
    - buzzer ativo e relé não devem ser usados simultaneamente;
    - servo e WS2812 LED não devem ser usados simultaneamente.
"""
from __future__ import annotations

import argparse
import os
import queue
import signal
import threading
import time
from dataclasses import dataclass
from datetime import datetime
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
    # Parâmetros do metrônomo
    bpm: int = 60
    min_bpm: int = 30
    max_bpm: int = 240
    bpm_step: int = 5
    pulse_ms: int = 70

    # PWM e atuadores
    led_frequency_hz: int = 1000
    buzzer_frequency_hz: int = 2000
    passive_buzzer: bool = False
    buzzer_enabled: bool = True

    # Movimento pendular. A faixa é propositalmente conservadora.
    servo_left_deg: float = -35.0
    servo_right_deg: float = 35.0
    servo_min_pulse_s: float = 1.0 / 1000.0
    servo_max_pulse_s: float = 2.0 / 1000.0
    servo_min_angle: float = -90.0
    servo_max_angle: float = 90.0

    # Freenove Projects Board for Raspberry Pi v1.2 — numeração BCM
    led_gpio: int = 17
    servo_gpio: int = 18
    active_buzzer_gpio: int = 12
    passive_buzzer_gpio: int = 4
    button_up_gpio: int = 21       # S1
    button_down_gpio: int = 20     # S2
    button_toggle_gpio: int = 16   # S3
    unused_button_gpio: int = 26   # S4

    # Debounce realizado pelo gpiozero
    debounce_seconds: float = 0.20


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


class HardwareBackend(Protocol):
    def set_led(self, value: float) -> None: ...
    def set_servo_angle(self, angle_deg: float) -> None: ...
    def buzzer_on(self) -> None: ...
    def buzzer_off(self) -> None: ...
    def register_button(self, gpio: int, callback: Callable[[], None]) -> None: ...
    def close(self) -> None: ...


class GPIOZeroBackend:
    """Backend real baseado exclusivamente em gpiozero.

    O gpiozero seleciona automaticamente um pin factory disponível no
    Raspberry Pi OS, normalmente lgpio ou RPi.GPIO. Não é necessário iniciar
    o pigpiod.
    """

    def __init__(self, config: Config):
        try:
            from gpiozero import AngularServo, Button, Buzzer, PWMLED, PWMOutputDevice
        except ImportError as exc:
            raise RuntimeError(
                "Biblioteca gpiozero ausente. Instale com: "
                "sudo apt install python3-gpiozero python3-lgpio"
            ) from exc

        self._config = config
        self._lock = threading.RLock()
        self._closed = False
        self._buttons: list[object] = []

        try:
            self._led = PWMLED(
                config.led_gpio,
                initial_value=0.0,
                frequency=config.led_frequency_hz,
            )

            self._servo = AngularServo(
                config.servo_gpio,
                initial_angle=0.0,
                min_angle=config.servo_min_angle,
                max_angle=config.servo_max_angle,
                min_pulse_width=config.servo_min_pulse_s,
                max_pulse_width=config.servo_max_pulse_s,
                frame_width=20.0 / 1000.0,  # 50 Hz
            )

            if config.passive_buzzer:
                self._buzzer = PWMOutputDevice(
                    config.passive_buzzer_gpio,
                    active_high=True,
                    initial_value=0.0,
                    frequency=config.buzzer_frequency_hz,
                )
            else:
                self._buzzer = Buzzer(
                    config.active_buzzer_gpio,
                    active_high=True,
                    initial_value=False,
                )

            self._Button = Button
        except Exception as exc:
            # Libera dispositivos que eventualmente tenham sido criados antes
            # da falha de inicialização.
            for name in ("_buzzer", "_servo", "_led"):
                device = getattr(self, name, None)
                if device is not None:
                    try:
                        device.close()
                    except Exception:
                        pass
            raise RuntimeError(
                "Falha ao inicializar as GPIOs com gpiozero. Verifique a "
                "conexão da placa, permissões e a instalação de python3-lgpio. "
                f"Detalhe: {exc}"
            ) from exc

    def set_led(self, value: float) -> None:
        with self._lock:
            if self._closed:
                return
            self._led.value = max(0.0, min(1.0, float(value)))

    def set_servo_angle(self, angle_deg: float) -> None:
        cfg = self._config
        angle = max(cfg.servo_min_angle, min(cfg.servo_max_angle, float(angle_deg)))
        with self._lock:
            if self._closed:
                return
            self._servo.angle = angle

    def buzzer_on(self) -> None:
        with self._lock:
            if self._closed:
                return
            if self._config.passive_buzzer:
                # O buzzer passivo precisa de uma onda quadrada audível.
                self._buzzer.value = 0.5
            else:
                self._buzzer.on()

    def buzzer_off(self) -> None:
        with self._lock:
            if self._closed:
                return
            if self._config.passive_buzzer:
                self._buzzer.value = 0.0
            else:
                self._buzzer.off()

    def register_button(self, gpio: int, callback: Callable[[], None]) -> None:
        # Os botões da placa são ativos em nível baixo. O pull-up interno deixa
        # a entrada em nível alto quando o botão está solto.
        button = self._Button(
            gpio,
            pull_up=True,
            bounce_time=self._config.debounce_seconds,
        )
        button.when_pressed = callback
        self._buttons.append(button)

    def close(self) -> None:
        with self._lock:
            if self._closed:
                return

            # Primeiro desativa as callbacks para impedir novas alterações de
            # estado durante o encerramento.
            for button in self._buttons:
                try:
                    button.when_pressed = None
                except Exception:
                    pass

            try:
                if self._config.passive_buzzer:
                    self._buzzer.value = 0.0
                else:
                    self._buzzer.off()
                self._led.value = 0.0
                # Remove o trem PWM do servo antes de fechar o dispositivo.
                try:
                    self._servo.detach()
                except (AttributeError, RuntimeError):
                    pass
            finally:
                for button in self._buttons:
                    try:
                        button.close()
                    except Exception:
                        pass
                for device in (self._buzzer, self._servo, self._led):
                    try:
                        device.close()
                    except Exception:
                        pass
                self._closed = True


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
        if self.closed:
            return
        self.closed = True
        self._record("backend", "closed")


class ActuatorWorker:
    """Executa LED, servo e buzzer sem bloquear o agendador."""

    def __init__(
        self,
        hardware: HardwareBackend,
        state: TempoState,
        config: Config,
        clock: Clock,
    ):
        self.hardware = hardware
        self.state = state
        self.config = config
        self.clock = clock
        self._queue: queue.Queue[int | None] = queue.Queue(maxsize=4)
        self._thread = threading.Thread(
            target=self._run,
            name="actuators",
            daemon=True,
        )
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
            print("Aviso: atuação descartada porque a fila está cheia")

    def _run(self) -> None:
        while self._running.is_set():
            item = self._queue.get()
            if item is None:
                break

            _, buzzer_enabled = self.state.snapshot()
            angle = (
                self.config.servo_left_deg
                if item % 2
                else self.config.servo_right_deg
            )

            self.hardware.set_servo_angle(angle)
            self.hardware.set_led(1.0)
            if buzzer_enabled:
                self.hardware.buzzer_on()

            self.clock.sleep(self.config.pulse_ms / 1000.0)

            self.hardware.set_led(0.12)
            if buzzer_enabled:
                self.hardware.buzzer_off()

    def stop(self) -> None:
        if not self._started:
            return
        self._running.clear()
        try:
            self._queue.put_nowait(None)
        except queue.Full:
            try:
                self._queue.get_nowait()
            except queue.Empty:
                pass
            self._queue.put_nowait(None)
        self._thread.join(timeout=2.0)


class BeatScheduler:
    """Agendador periódico por deadlines absolutos, sem drift acumulado."""

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
                self.clock.sleep((remaining - self.busy_wait_ns) / 1_000_000_000)
            elif self.busy_wait_ns == 0:
                self.clock.sleep(remaining / 1_000_000_000)
            else:
                # Espera ativa curta para reduzir o atraso de despertar.
                pass

    def run(self, max_beats: int | None = None) -> None:
        beat = 0
        deadline = self.clock.monotonic_ns()

        while not self.stop_event.is_set() and (
            max_beats is None or beat < max_beats
        ):
            bpm, _ = self.state.snapshot()
            period_ns = self.period_override_ns or round(60_000_000_000 / bpm)

            if beat == 0:
                deadline = self.clock.monotonic_ns()
            else:
                deadline += period_ns
                self._wait_until(deadline)

            if self.stop_event.is_set():
                break

            actual = self.clock.monotonic_ns()
            beat += 1
            self.on_beat(beat, deadline, actual, bpm)

            # Caso a aplicação atrase mais de um período, reinicia a fase para
            # evitar uma rajada de batidas antigas.
            if actual - deadline > period_ns:
                deadline = actual


def configure_process(rt_priority: int | None, cpu_core: int | None) -> None:
    if cpu_core is not None:
        try:
            os.sched_setaffinity(0, {cpu_core})
        except (AttributeError, OSError) as exc:
            print(f"Aviso: não foi possível fixar afinidade na CPU {cpu_core}: {exc}")

    if rt_priority is not None:
        if not 1 <= rt_priority <= 99:
            raise ValueError("rt_priority deve estar entre 1 e 99")
        try:
            os.sched_setscheduler(0, os.SCHED_FIFO, os.sched_param(rt_priority))
        except (AttributeError, PermissionError, OSError) as exc:
            print(f"Aviso: prioridade SCHED_FIFO não aplicada: {exc}")


def wait_for_wall_time(iso_datetime: str, clock: Clock) -> None:
    target = datetime.fromisoformat(iso_datetime)
    if target.tzinfo is None:
        raise ValueError(
            "--start-at exige fuso horário, por exemplo "
            "2026-07-15T20:00:00-03:00"
        )

    target_ns = int(target.timestamp() * 1_000_000_000)
    while True:
        remaining = target_ns - clock.wall_time_ns()
        if remaining <= 0:
            return
        clock.sleep(min(remaining / 1_000_000_000, 1.0))


class MetronomeApp:
    def __init__(
        self,
        config: Config,
        hardware: HardwareBackend,
        clock: Clock | None = None,
    ):
        self.config = config
        self.clock = clock or RealClock()
        self.hardware = hardware
        self.state = TempoState(config)
        self.stop_event = threading.Event()
        self.worker = ActuatorWorker(hardware, self.state, config, self.clock)
        self.scheduler = BeatScheduler(
            self.state,
            self._on_beat,
            self.stop_event,
            self.clock,
        )
        self._stopped = False

    def _on_up(self) -> None:
        bpm = self.state.increase()
        print(f"S1: BPM alterado para {bpm}")

    def _on_down(self) -> None:
        bpm = self.state.decrease()
        print(f"S2: BPM alterado para {bpm}")

    def _on_toggle(self) -> None:
        enabled = self.state.toggle_buzzer()
        if not enabled:
            # Desliga imediatamente; não espera o fim do pulso corrente.
            self.hardware.buzzer_off()
        print(f"S3: buzzer {'ativado' if enabled else 'desativado'}")

    def _on_beat(
        self,
        beat: int,
        scheduled_ns: int,
        actual_ns: int,
        bpm: int,
    ) -> None:
        self.worker.trigger(beat)
        error_ms = (actual_ns - scheduled_ns) / 1_000_000
        print(f"beat={beat:04d} bpm={bpm:3d} erro={error_ms:+.3f} ms")

    def setup(self) -> None:
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
        self.hardware.close()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Metrônomo para Raspberry Pi 3 com Freenove Projects Board v1.2 "
            "e biblioteca gpiozero"
        )
    )
    parser.add_argument("--bpm", type=int, default=60, help="BPM inicial (30-240)")
    parser.add_argument(
        "--passive-buzzer",
        action="store_true",
        help="usa o buzzer passivo em GPIO4; por padrão usa o ativo em GPIO12",
    )
    parser.add_argument(
        "--no-buzzer",
        action="store_true",
        help="inicia com o buzzer desativado",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="executa sem acessar as GPIOs",
    )
    parser.add_argument(
        "--beats",
        type=int,
        default=None,
        help="encerra automaticamente após N batidas",
    )
    parser.add_argument(
        "--start-at",
        type=str,
        help="horário ISO 8601 com fuso para a primeira batida",
    )
    parser.add_argument(
        "--rt-priority",
        type=int,
        default=None,
        help="prioridade SCHED_FIFO opcional",
    )
    parser.add_argument(
        "--cpu-core",
        type=int,
        default=None,
        help="fixa o processo em um núcleo",
    )
    return parser


def print_board_setup(config: Config) -> None:
    buzzer_gpio = (
        config.passive_buzzer_gpio
        if config.passive_buzzer
        else config.active_buzzer_gpio
    )
    buzzer_name = "passivo" if config.passive_buzzer else "ativo"

    print("Freenove Projects Board v1.2 — configuração BCM")
    print(f"  LED azul: GPIO{config.led_gpio}")
    print(f"  Servo SIG: GPIO{config.servo_gpio}")
    print(f"  Buzzer {buzzer_name}: GPIO{buzzer_gpio}")
    print(f"  S1 BPM+: GPIO{config.button_up_gpio}")
    print(f"  S2 BPM-: GPIO{config.button_down_gpio}")
    print(f"  S3 buzzer: GPIO{config.button_toggle_gpio}")
    print("  Chaves: função 2 ON; função 5 ON; função 3 ON para buzzer ativo")


def main(argv: Optional[list[str]] = None) -> int:
    args = build_parser().parse_args(argv)

    config = Config(
        bpm=args.bpm,
        passive_buzzer=args.passive_buzzer,
        buzzer_enabled=not args.no_buzzer,
    )

    configure_process(args.rt_priority, args.cpu_core)
    clock = RealClock()

    if args.dry_run:
        hardware: HardwareBackend = MockBackend(clock)
    else:
        print_board_setup(config)
        hardware = GPIOZeroBackend(config)

    app = MetronomeApp(config, hardware, clock)

    def request_stop(_signum: int, _frame: object) -> None:
        app.stop_event.set()

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

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
