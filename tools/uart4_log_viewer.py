#!/usr/bin/env python3
"""UART4 log viewer for the BRICOS protocol.

This tool reads the framed UART stream used by the MCU and prints only
DATA_TYPE_LOG frames, so binary status packets do not confuse a terminal
emulator such as minicom.
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from typing import Iterator, Optional

try:
    import serial
except ImportError as exc:  # pragma: no cover - import error path is user-facing
    raise SystemExit(
        "pyserial is required. Install it with: python3 -m pip install pyserial"
    ) from exc


PACKET_START_BYTE1 = 0xAA
PACKET_START_BYTE2 = 0xBB
PACKET_END_BYTE1 = 0xCC
PACKET_END_BYTE2 = 0xDD

DATA_TYPE_LOG = 0x05


@dataclass
class Frame:
    cmd_id: int
    payload: bytes


def checksum_xor(data: bytes) -> int:
    value = 0
    for byte in data:
        value ^= byte
    return value & 0xFF


def iter_frames(buffer: bytearray) -> Iterator[Frame]:
    """Yield parsed frames from the accumulated buffer."""
    while True:
        if len(buffer) < 7:
            return

        start = buffer.find(bytes((PACKET_START_BYTE1, PACKET_START_BYTE2)))
        if start < 0:
            buffer.clear()
            return
        if start > 0:
            del buffer[:start]
            if len(buffer) < 7:
                return

        payload_len = buffer[3]
        total_len = payload_len + 7
        if total_len > 262:
            # Invalid length, drop one byte and resync.
            del buffer[0]
            continue

        if len(buffer) < total_len:
            return

        if buffer[total_len - 2] != PACKET_END_BYTE1 or buffer[total_len - 1] != PACKET_END_BYTE2:
            del buffer[0]
            continue

        cmd_id = buffer[2]
        payload = bytes(buffer[4 : 4 + payload_len])
        checksum = buffer[4 + payload_len]
        expected = checksum_xor(bytes((cmd_id, payload_len)) + payload)

        if checksum != expected:
            del buffer[0]
            continue

        del buffer[:total_len]
        yield Frame(cmd_id=cmd_id, payload=payload)


def format_log_payload(payload: bytes) -> str:
    text = payload.decode("utf-8", errors="replace")
    return text.rstrip("\r\n")


def open_serial(port: str, baud: int) -> serial.Serial:
    return serial.Serial(
        port=port,
        baudrate=baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.1,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    )


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Print BRICOS UART4 log frames from the framed protocol stream."
    )
    parser.add_argument(
        "-D",
        "--port",
        default="/dev/ttyS7",
        help="Serial device path, default: /dev/ttyS7",
    )
    parser.add_argument(
        "-b",
        "--baud",
        type=int,
        default=115200,
        help="Baud rate, default: 115200",
    )
    parser.add_argument(
        "--show-nonlog",
        action="store_true",
        help="Also print non-log frames in a compact hex form.",
    )
    args = parser.parse_args(argv)

    try:
        ser = open_serial(args.port, args.baud)
    except serial.SerialException as exc:
        print(f"Failed to open {args.port}: {exc}", file=sys.stderr)
        return 1

    print(f"Listening on {args.port} @ {args.baud} bps", file=sys.stderr)
    print("Flow control is disabled; only DATA_TYPE_LOG frames will be printed.", file=sys.stderr)

    rx_buffer = bytearray()

    try:
        while True:
            chunk = ser.read(ser.in_waiting or 1)
            if chunk:
                rx_buffer.extend(chunk)
            else:
                time.sleep(0.01)

            for frame in iter_frames(rx_buffer):
                ts = time.strftime("%Y-%m-%d %H:%M:%S")
                if frame.cmd_id == DATA_TYPE_LOG:
                    print(f"[{ts}] {format_log_payload(frame.payload)}")
                elif args.show_nonlog:
                    hex_payload = " ".join(f"{b:02X}" for b in frame.payload)
                    print(f"[{ts}] CMD=0x{frame.cmd_id:02X} LEN={len(frame.payload)} {hex_payload}")
    except KeyboardInterrupt:
        return 0
    finally:
        ser.close()


if __name__ == "__main__":
    raise SystemExit(main())
