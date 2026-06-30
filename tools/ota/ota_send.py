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
import threading
import time
from pathlib import Path

import serial
import gpiod
from gpiod.line import Direction, Value


SOH = 0x01
STX = 0x02
EOT = 0x04
ACK = 0x06
NAK = 0x15
CA = 0x18
CRC16 = 0x43

PACKET_SIZE = 128
PACKET_1K_SIZE = 1024
OTA_TRIGGER = bytes([0xDE, 0xAD, 0xBE, 0xEF])

# --- 协议常量定义 ---
PACKET_START_BYTES = bytes([0xAA, 0xBB])
PACKET_END_BYTES = bytes([0xCC, 0xDD])
DATA_TYPE_OTA = 0x10

def build_protocol_packet(cmd_id: int, payload: bytes) -> bytes:
    """
    根据 C 语言代码规则，组装完整的协议数据包
    """
    payload_len = len(payload)
    if payload_len > 255:
        raise ValueError("Payload length cannot exceed 255 bytes")

    # 需要参与校验的数据段：CMD (1字节) + LEN (1字节) + Payload (N字节)
    checksum_data = bytes([cmd_id, payload_len]) + payload

    # 计算异或校验和 (XOR Checksum)
    checksum = 0
    for b in checksum_data:
        checksum ^= b

    # 拼接完整数据包：帧头 + 参与校验的数据段 + 校验码 + 帧尾
    packet = PACKET_START_BYTES + checksum_data + bytes([checksum]) + PACKET_END_BYTES
    
    return packet

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


def wait_for_console_tokens(
    console: SerialConsole,
    tokens: tuple[str, ...],
    timeout: float,
) -> str | None:
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        time.sleep(0.02)
        console.poll()
        for token in tokens:
            if console.contains(token):
                return token

    return None


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
        if console is not None and console.port is not port:
            console.poll()

        value = port.read(1)
        if not value:
            continue

        byte = value[0]
        if byte in expected:
            return byte
        if byte in ignored:
            continue

    raise TimeoutError(f"Timed out waiting for bytes: {sorted(expected)}")


def append_console_byte(console: SerialConsole, byte: int) -> None:
    text = bytes([byte]).decode("utf-8", errors="replace")
    console.text_buffer += text
    if len(console.text_buffer) > 8192:
        console.text_buffer = console.text_buffer[-4096:]


def select_transfer_port(
    cmd_port: serial.Serial,
    data_port: serial.Serial,
    console: SerialConsole,
    timeout: float,
) -> tuple[serial.Serial, str]:
    print("Waiting for initial Ymodem 'C' on cmd/data ports...")
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        cmd_value = cmd_port.read(1)
        if cmd_value:
            cmd_byte = cmd_value[0]
            if cmd_byte == CRC16:
                return cmd_port, "cmd_port"
            append_console_byte(console, cmd_byte)

        data_value = data_port.read(1)
        if data_value:
            data_byte = data_value[0]
            if data_byte == CRC16:
                return data_port, "data_port"

    raise TimeoutError("Timed out waiting for initial Ymodem 'C' on both ports")


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

def pulse_reset_gpio(
    gpiochip: str,
    line_offset: int,
    hold_ms: float,
    active_low: bool = True,
) -> None:
    polarity = "active-low" if active_low else "active-high"
    print(
        f"Pulsing reset GPIO: {gpiochip} line {line_offset} "
        f"({polarity}, hold={hold_ms:.1f} ms)"
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
        consumer="ota_send",
        config={line_offset: settings},
    ) as request:
        request.set_value(line_offset, inactive_value)
        time.sleep(0.02)
        request.set_value(line_offset, active_value)
        time.sleep(hold_ms / 1000.0)
        request.set_value(line_offset, inactive_value)
        print("Reset released.")

def hardware_reset_stm32(chip_path, line_offset):
    # 使用新版 v2.x 语法
    with gpiod.request_lines(
        chip_path,
        consumer="OTA-Reset",
        config={
            line_offset: gpiod.LineSettings(
                direction=Direction.OUTPUT, 
                output_value=Value.ACTIVE  # 初始设为高电平(1)
            )
        },
    ) as request:
        print(f"Pulsing reset on {chip_path} line {line_offset}...")
        
        # 1. 拉低电平触发复位 (Value.INACTIVE 在低电平有效配置下通常是 0)
        request.set_value(line_offset, Value.INACTIVE) 
        time.sleep(0.1)
        
        # 2. 拉高电平释放复位
        request.set_value(line_offset, Value.ACTIVE)
        print("Reset released.")

def perform_reset_gpio(
    gpiochip: str,
    line_offset: int,
    hold_ms: float,
    active_low: bool,
) -> None:
    pulse_reset_gpio(gpiochip, line_offset, hold_ms, active_low)


def build_header_payload(file_path: Path) -> bytes:
    name = file_path.name.encode("ascii", errors="ignore")
    size = str(file_path.stat().st_size).encode("ascii")
    payload = name + b"\0" + size + b"\0"
    return payload.ljust(PACKET_SIZE, b"\0")


def enter_bootloader_by_app_trigger(
    cmd_port: serial.Serial,
    data_port: serial.Serial,
    console: SerialConsole,
    seconds: float,
) -> None:
    print("Sending OTA trigger to APP on USART4...")
    OTA_Trigger = build_protocol_packet(DATA_TYPE_OTA, OTA_TRIGGER)
    cmd_port.write(OTA_Trigger)
    cmd_port.flush()

    #time.sleep(0.8)

    cmd_port.reset_input_buffer()
    data_port.reset_input_buffer()

    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        time.sleep(0.05)
        console.poll()
        if console.contains("READY"):
            return

    raise TimeoutError("Bootloader menu was not detected after APP trigger")

def enter_bootloader_by_hw_reset(
    cmd_port: serial.Serial,
    console: SerialConsole,
    seconds: float,
    gpiochip: str,
    line_offset: int,
    hold_ms: float,
    active_low: bool,
) -> None:
    cmd_port.reset_input_buffer()
    cmd_port.reset_output_buffer()

    deadline = time.monotonic() + seconds
    prompt_seen = False
    stop_sender = threading.Event()

    def spam_boot_key() -> None:
        while not stop_sender.is_set():
            try:
                cmd_port.write(b"B")
                cmd_port.flush()
            except Exception:
                return
            time.sleep(0.005)

    sender = threading.Thread(target=spam_boot_key, name="ota-boot-key", daemon=True)
    sender.start()

    try:
        time.sleep(0.02)
        perform_reset_gpio(gpiochip, line_offset, hold_ms, active_low)

        while time.monotonic() < deadline:
            time.sleep(0.02)
            console.poll()
            if console.contains("READY"):
                return
            if console.contains("Send 'B' to enter Bootloader mode..."):
                prompt_seen = True
            if prompt_seen and console.contains("Normal Boot Mode"):
                raise RuntimeError("STM32 fell through to normal boot")
    finally:
        stop_sender.set()
        sender.join(timeout=0.2)

    raise TimeoutError("Bootloader menu was not detected after hardware reset")

def enter_bootloader(
    args: argparse.Namespace,
    cmd_port: serial.Serial,
    data_port: serial.Serial,
    console: SerialConsole,
) -> None:
    if args.mode == "app-trigger":
        enter_bootloader_by_app_trigger(cmd_port, data_port, console, args.boot_window)
        return
    if args.mode == "hw-reset":
        if not args.reset_gpiochip or args.reset_line is None:
            raise RuntimeError("hw-reset mode requires --reset-gpiochip and --reset-line")
        enter_bootloader_by_hw_reset(
            cmd_port,
            console,
            args.boot_window,
            args.reset_gpiochip,
            args.reset_line,
            args.reset_hold_ms,
            args.reset_active_low,
        )
        return
    
    raise RuntimeError(f"Unsupported bootloader entry mode: {args.mode}")

def send_command(port: serial.Serial, command: bytes, console: SerialConsole | None = None) -> None:
    payload = command if command.endswith((b"\r", b"\n")) else (command + b"\r")
    port.write(payload)
    port.flush()
    time.sleep(0.15)
    if console is not None:
        console.poll()


def wait_for_download_mode_ack(console: SerialConsole, timeout: float) -> None:
    token = wait_for_console_tokens(
        console,
        (
            ">>> Download to APP2 (Ymodem) <<<",
            "Waiting for file (Ymodem)...",
            "Erasing APP2...",
        ),
        timeout,
    )
    if token is None:
        raise RuntimeError("Download command '2' was not acknowledged")


def wait_for_download_ready(console: SerialConsole, timeout: float) -> None:
    token = wait_for_console_tokens(
        console,
        (
            "Erase APP2 done.",
            "Erase APP2 failed!",
            "Too many errors waiting for header!",
        ),
        timeout,
    )
    if token == "Erase APP2 done.":
        return
    if token is None:
        raise RuntimeError("Bootloader did not reach Ymodem-ready state")
    raise RuntimeError(f"Bootloader failed before Ymodem start: {token}")


def transfer_file(
    transfer_port: serial.Serial,
    file_path: Path,
    console: SerialConsole,
    retries: int,
) -> None:
    header_payload = build_header_payload(file_path)
    print(f"Sending header packet for {file_path.name} ({file_path.stat().st_size} bytes)...")
    send_packet(transfer_port, 0, header_payload)

    wait_for_data_byte(transfer_port, {ACK}, timeout=10.0, console=console, ignored={CRC16})
    wait_for_data_byte(transfer_port, {CRC16}, timeout=10.0, console=console, ignored={ACK, CRC16})

    file_data = file_path.read_bytes()
    total_blocks = (len(file_data) + PACKET_1K_SIZE - 1) // PACKET_1K_SIZE

    for index in range(total_blocks):
        chunk = file_data[index * PACKET_1K_SIZE:(index + 1) * PACKET_1K_SIZE]
        payload = chunk.ljust(PACKET_1K_SIZE, b"\x1A")
        block_no = index + 1

        for attempt in range(1, retries + 1):
            send_packet(transfer_port, block_no, payload)
            response = wait_for_data_byte(
                transfer_port,
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
    transfer_port.write(bytes([EOT]))
    transfer_port.flush()
    response = wait_for_data_byte(transfer_port, {ACK, NAK, CA}, timeout=10.0, console=console)
    if response == NAK:
        transfer_port.write(bytes([EOT]))
        transfer_port.flush()
        response = wait_for_data_byte(transfer_port, {ACK, CA}, timeout=10.0, console=console)
    if response == CA:
        raise RuntimeError("Transfer aborted by STM32 at EOT")

    print("Ymodem transfer finished.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send APP image to STM32 bootloader over two UARTs")
    parser.add_argument("--mode", choices=["app-trigger", "hw-reset"], default="app-trigger")
    parser.add_argument("--cmd-port", required=True, help="Boot key / menu output UART, typically UART4")
    parser.add_argument("--data-port", required=True, help="Bootloader command + Ymodem UART, typically USART3")
    parser.add_argument("--file", required=True, help="APP image to send, usually the APP1 .bin file")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate for both UARTs")
    parser.add_argument("--boot-window", type=float, default=6.0, help="Timeout for entering bootloader")
    parser.add_argument("--retries", type=int, default=10, help="Retries per Ymodem data block")

    parser.add_argument("--reset-gpiochip", help="GPIO chip path for hw-reset mode, for example /dev/gpiochip0")
    parser.add_argument("--reset-line", type=int, help="GPIO line offset for hw-reset mode")
    parser.add_argument("--reset-hold-ms", type=float, default=80.0, help="Reset pulse width in ms")
    parser.add_argument("--reset-active-low", action="store_true", default=True, help="Reset line is active low")
    parser.add_argument("--reset-active-high", action="store_false", dest="reset_active_low", help="Reset line is active high")

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

        enter_bootloader(args, cmd_port, data_port, console)

        print("Bootloader menu detected, sending command '2'...")
        send_command(data_port, b"2")
        wait_for_download_mode_ack(console, timeout=3.0)
        wait_for_download_ready(console, timeout=10.0)
        cmd_port.reset_input_buffer()
        data_port.reset_input_buffer()
        transfer_port, transfer_name = select_transfer_port(
            cmd_port, data_port, console, timeout=20.0
        )
        transfer_file(transfer_port, file_path, console, args.retries)

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
