#!/usr/bin/env python3
"""
Windows OTA helper for the current STM32F407 bootloader flow.

Usage example:
    python ota_send.py --cmd-port COM5 --data-port COM6 --file app1.bin

Requirements:
    pip install pyserial
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import serial


SOH = 0x01
STX = 0x02
EOT = 0x04
ACK = 0x06
NAK = 0x15
CA = 0x18
CRC16 = 0x43

PACKET_SIZE = 128
PACKET_1K_SIZE = 1024


class SerialConsole:
    def __init__(self, port: serial.Serial, name: str) -> None:
        self.port = port
        self.name = name
        self.text_buffer = ""

    def poll(self) -> None:
        waiting = self.port.in_waiting
        if waiting <= 0:
            return

        data = self.port.read(waiting)
        if not data:
            return

        text = data.decode("utf-8", errors="replace")
        self.text_buffer += text
        if len(self.text_buffer) > 8192:
            self.text_buffer = self.text_buffer[-4096:]

        sys.stdout.write(text)
        sys.stdout.flush()

    def contains(self, token: str) -> bool:
        return token in self.text_buffer


def crc16_ccitt(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def wait_for_data_byte(
    port: serial.Serial,
    expected: set[int],
    timeout: float,
    console: SerialConsole | None = None,
    ignored: set[int] | None = None,
) -> int:
    deadline = time.monotonic() + timeout
    ignored = ignored or set()

    while time.monotonic() < deadline:
        if console is not None:
            console.poll()

        value = port.read(1)
        if not value:
            continue

        byte = value[0]
        if byte in expected:
            return byte
        if byte in ignored:
            continue

        if 32 <= byte <= 126:
            sys.stdout.write(f"\n[data] unexpected byte: {chr(byte)!r}\n")
        else:
            sys.stdout.write(f"\n[data] unexpected byte: 0x{byte:02X}\n")
        sys.stdout.flush()

    raise TimeoutError(f"Timed out waiting for bytes: {sorted(expected)}")


def send_packet(port: serial.Serial, seq: int, payload: bytes) -> None:
    if len(payload) == PACKET_1K_SIZE:
        start = STX
    elif len(payload) == PACKET_SIZE:
        start = SOH
    else:
        raise ValueError(f"Unsupported payload length: {len(payload)}")

    crc = crc16_ccitt(payload)
    packet = bytearray()
    packet.append(start)
    packet.append(seq & 0xFF)
    packet.append((0xFF - seq) & 0xFF)
    packet.extend(payload)
    packet.append((crc >> 8) & 0xFF)
    packet.append(crc & 0xFF)

    port.write(packet)
    port.flush()


def build_header_payload(file_path: Path) -> bytes:
    name = file_path.name.encode("ascii", errors="ignore")
    size = str(file_path.stat().st_size).encode("ascii")
    payload = name + b"\0" + size + b"\0"
    return payload.ljust(PACKET_SIZE, b"\0")


def wait_for_boot_menu(cmd_port: serial.Serial, console: SerialConsole, seconds: float) -> None:
    print("Reset the board now. Sending 'B' repeatedly on the command UART...")
    deadline = time.monotonic() + seconds

    while time.monotonic() < deadline:
        cmd_port.write(b"B")
        cmd_port.flush()
        time.sleep(0.08)
        console.poll()
        if console.contains("READY") or console.contains("Commands:"):
            return

    raise TimeoutError("Bootloader menu was not detected on the command UART")


def send_command(cmd_port: serial.Serial, command: bytes, console: SerialConsole) -> None:
    cmd_port.write(command)
    cmd_port.flush()
    time.sleep(0.1)
    console.poll()


def transfer_file(
    data_port: serial.Serial,
    file_path: Path,
    console: SerialConsole,
    retries: int,
) -> None:
    print("Waiting for initial Ymodem 'C' from USART2...")
    wait_for_data_byte(data_port, {CRC16}, timeout=15.0, console=console, ignored={CRC16})

    header_payload = build_header_payload(file_path)
    print(f"Sending header packet for {file_path.name} ({file_path.stat().st_size} bytes)...")
    send_packet(data_port, 0, header_payload)

    wait_for_data_byte(data_port, {ACK}, timeout=10.0, console=console, ignored={CRC16})
    wait_for_data_byte(data_port, {CRC16}, timeout=10.0, console=console, ignored={ACK, CRC16})

    file_data = file_path.read_bytes()
    total_blocks = (len(file_data) + PACKET_1K_SIZE - 1) // PACKET_1K_SIZE

    for index in range(total_blocks):
        chunk = file_data[index * PACKET_1K_SIZE:(index + 1) * PACKET_1K_SIZE]
        payload = chunk.ljust(PACKET_1K_SIZE, b"\x1A")
        block_no = index + 1

        for attempt in range(1, retries + 1):
            send_packet(data_port, block_no, payload)
            response = wait_for_data_byte(
                data_port,
                {ACK, NAK, CA},
                timeout=10.0,
                console=console,
                ignored={CRC16},
            )
            if response == ACK:
                print(f"Block {block_no}/{total_blocks} ACK")
                break
            if response == CA:
                raise RuntimeError("Transfer aborted by STM32")
            if attempt == retries:
                raise RuntimeError(f"Block {block_no} failed after {retries} retries")
            print(f"Block {block_no} NAK, retry {attempt}/{retries}")

    print("Sending EOT...")
    data_port.write(bytes([EOT]))
    data_port.flush()
    response = wait_for_data_byte(data_port, {ACK, NAK, CA}, timeout=10.0, console=console)
    if response == NAK:
        data_port.write(bytes([EOT]))
        data_port.flush()
        response = wait_for_data_byte(data_port, {ACK, CA}, timeout=10.0, console=console)
    if response == CA:
        raise RuntimeError("Transfer aborted by STM32 at EOT")

    print("Ymodem transfer finished.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send APP image to STM32 bootloader over two UARTs")
    parser.add_argument("--cmd-port", required=True, help="Command/debug UART, typically USART1 (for example COM5)")
    parser.add_argument("--data-port", required=True, help="Ymodem UART, typically USART2 (for example COM6)")
    parser.add_argument("--file", required=True, help="APP image to send, usually the APP1 .bin file")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate for both UARTs")
    parser.add_argument("--boot-window", type=float, default=4.0, help="Seconds to keep sending 'B' while you reset the board")
    parser.add_argument("--retries", type=int, default=10, help="Retries per Ymodem data block")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    file_path = Path(args.file).expanduser().resolve()
    if not file_path.is_file():
        print(f"File not found: {file_path}", file=sys.stderr)
        return 1

    cmd_port = serial.Serial(args.cmd_port, args.baud, timeout=0.05, write_timeout=1)
    data_port = serial.Serial(args.data_port, args.baud, timeout=0.05, write_timeout=1)

    try:
        cmd_port.reset_input_buffer()
        cmd_port.reset_output_buffer()
        data_port.reset_input_buffer()
        data_port.reset_output_buffer()

        console = SerialConsole(cmd_port, "cmd")

        wait_for_boot_menu(cmd_port, console, args.boot_window)
        print("Bootloader menu detected, sending command '2'...")
        send_command(cmd_port, b"2", console)
        transfer_file(data_port, file_path, console, args.retries)

        print("Waiting for final bootloader output...")
        time.sleep(1.0)
        console.poll()
        print("Done.")
        return 0
    except KeyboardInterrupt:
        print("Cancelled by user.")
        return 2
    except Exception as exc:
        print(f"OTA transfer failed: {exc}", file=sys.stderr)
        return 1
    finally:
        cmd_port.close()
        data_port.close()


if __name__ == "__main__":
    raise SystemExit(main())