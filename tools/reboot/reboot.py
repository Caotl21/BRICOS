#!/usr/bin/env python3
from __future__ import annotations

import argparse
import time

import gpiod


def pulse_reset_gpio(
    gpiochip: str,
    line_offset: int,
    hold_ms: float,
    settle_ms: float,
    active_low: bool,
) -> None:
    polarity = "active-low" if active_low else "active-high"
    print(
        f"Pulsing reset GPIO: {gpiochip} line {line_offset} "
        f"({polarity}, hold={hold_ms:.1f} ms, settle={settle_ms:.1f} ms)"
    )

    active_value = (
        gpiod.line.Value.INACTIVE if active_low else gpiod.line.Value.ACTIVE
    )
    inactive_value = (
        gpiod.line.Value.ACTIVE if active_low else gpiod.line.Value.INACTIVE
    )

    settings = gpiod.LineSettings(
        direction=gpiod.line.Direction.OUTPUT,
        output_value=inactive_value,
    )

    with gpiod.request_lines(
        gpiochip,
        consumer="reboot",
        config={line_offset: settings},
    ) as request:
        request.set_value(line_offset, inactive_value)
        time.sleep(0.02)
        request.set_value(line_offset, active_value)
        time.sleep(hold_ms / 1000.0)
        request.set_value(line_offset, inactive_value)
        time.sleep(settle_ms / 1000.0)

    print("Reset pulse completed.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Pulse a GPIO on the RDK X5 to reset STM32."
    )
    parser.add_argument(
        "--gpiochip",
        required=True,
        help="GPIO chip path, for example /dev/gpiochip1",
    )
    parser.add_argument(
        "--line",
        required=True,
        type=int,
        help="GPIO line offset on the gpiochip",
    )
    parser.add_argument(
        "--hold-ms",
        type=float,
        default=80.0,
        help="Reset pulse width in milliseconds",
    )
    parser.add_argument(
        "--settle-ms",
        type=float,
        default=50.0,
        help="Delay after releasing reset in milliseconds",
    )
    parser.add_argument(
        "--active-low",
        action="store_true",
        default=True,
        help="Reset line is active low (default: true)",
    )
    parser.add_argument(
        "--active-high",
        action="store_false",
        dest="active_low",
        help="Reset line is active high",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    pulse_reset_gpio(
        gpiochip=args.gpiochip,
        line_offset=args.line,
        hold_ms=args.hold_ms,
        settle_ms=args.settle_ms,
        active_low=args.active_low,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
