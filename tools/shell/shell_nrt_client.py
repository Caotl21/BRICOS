#!/usr/bin/env python3
import argparse
import os
import select
import sys
import termios
import threading
import time
import tty

import serial

START1 = 0xAA
START2 = 0xBB
END1 = 0xCC
END2 = 0xDD

CMD_SHELL_REQ = 0x20
CMD_SHELL_RESP = 0x21
CMD_SHELL_BOOT_DETECT = 0x22
CMD_SHELL_BOOT_STATUS = 0x23
CMD_ACK = 0xFF
CMD_LOG = 0x05

ACK_MAP = {
    0x01: "ACK_SUCCESS",
    0x02: "INVALID_PARAM",
    0x03: "UNKNOWN_CMD",
    0x04: "LENGTH_ERROR",
}


class RawMode:
    def __init__(self, fd):
        self.fd = fd
        self._old = None

    def __enter__(self):
        self._old = termios.tcgetattr(self.fd)
        tty.setraw(self.fd)
        return self

    def __exit__(self, exc_type, exc, tb):
        if self._old is not None:
            termios.tcsetattr(self.fd, termios.TCSADRAIN, self._old)


class HydroParser:
    def __init__(self):
        self.buf = bytearray()

    @staticmethod
    def _checksum(data: bytes) -> int:
        c = 0
        for b in data:
            c ^= b
        return c

    def feed(self, data: bytes):
        self.buf.extend(data)
        out = []
        while True:
            idx = self.buf.find(bytes([START1, START2]))
            if idx < 0:
                self.buf.clear()
                break
            if idx > 0:
                del self.buf[:idx]
            if len(self.buf) < 4:
                break

            cmd = self.buf[2]
            payload_len = self.buf[3]
            total_len = payload_len + 7
            if len(self.buf) < total_len:
                break

            frame = self.buf[:total_len]
            if frame[-2] != END1 or frame[-1] != END2:
                del self.buf[:2]
                continue

            expected = self._checksum(frame[2:4 + payload_len])
            actual = frame[4 + payload_len]
            if expected != actual:
                del self.buf[:2]
                continue

            payload = bytes(frame[4:4 + payload_len])
            out.append((cmd, payload))
            del self.buf[:total_len]
        return out


class ShellClient:
    COMMANDS = [
        "help",
        "reboot",
        "echo",
        "sysmode",
        "momode",
        "fault",
        "euler",
        "depthtemp",
        "power",
        "cabin",
        "chip",
    ]
    QUERY_COMMANDS = ["euler", "depthtemp", "power", "cabin", "chip"]
    SYSMODE_TARGETS = ["standby", "disarmed", "armed", "failsafe"]
    MOMODE_TARGETS = ["manual", "stabilize", "auto"]
    SHELL_RET_MAP = {
        0: ("OK", "成功"),
        1: ("UNKNOWN_CMD", "未知命令"),
        2: ("BAD_ARGS", "参数错误"),
        3: ("DENIED", "权限不足或被安全策略拒绝"),
        4: ("MODE_BLOCKED", "当前系统模式不允许执行"),
        5: ("BUSY", "资源忙"),
        6: ("INTERNAL_ERROR", "内部错误"),
    }

    ANSI_RESET = "\033[0m"
    ANSI_GREEN = "\033[92m"
    ANSI_WHITE = "\033[97m"
    ANSI_GRADIENT = [196, 202, 226, 82, 45]
    RECONNECT_RETRY_SEC = 0.3
    SHELL_BOOT_PROBE_INTERVAL_SEC = 0.3
    BOOT_STATUS_DUP_GUARD_SEC = 1.0
    BOOT_STATUS_STARTUP_SETTLE_SEC = 3.0
    REBOOT_BOOT_PROBE_DELAY_SEC = 0.25
    SHELL_BOOT_DETECT_PAYLOAD = b"detect"
    SHELL_BOOT_STATUS_TEXT = "startup_success"

    @staticmethod
    def _write_line(text: str = ""):
        # In raw tty mode, '\n' does not reliably return to column 0.
        # Always emit CRLF explicitly for aligned multi-line output.
        sys.stdout.write(text + "\r\n")

    def __init__(self, port: str, baud: int, verbose_frames: bool = False):
        self.port = port
        self.baud = baud
        self.ser = None
        self.connected = False
        self.parser = HydroParser()
        self.running = True
        self.line = ""
        self.prompt_name = "bricsbot1"
        self.prompt_symbol = "$"
        self.waiting_response = False
        self.history = []
        self.history_index = 0
        self.lock = threading.Lock()
        self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self.verbose_frames = verbose_frames
        self._shell_resp_buf = bytearray()
        self._shell_ready = False
        self._next_boot_probe_ts = 0.0
        self._last_boot_status_ts = 0.0
        self._shell_ready_since_ts = 0.0

    @staticmethod
    def _checksum(data: bytes) -> int:
        c = 0
        for b in data:
            c ^= b
        return c

    def _build_frame(self, cmd_id: int, payload: bytes) -> bytes:
        if len(payload) > 255:
            raise ValueError("payload too long")
        frame = bytearray()
        frame.append(START1)
        frame.append(START2)
        frame.append(cmd_id & 0xFF)
        frame.append(len(payload) & 0xFF)
        frame.extend(payload)
        frame.append(self._checksum(frame[2:]))
        frame.append(END1)
        frame.append(END2)
        return bytes(frame)

    def send_shell_line(self, line: str):
        # Clear pending fragments before sending next command.
        self._shell_resp_buf.clear()
        payload = line.encode("utf-8", errors="ignore")
        frame = self._build_frame(CMD_SHELL_REQ, payload)
        try:
            if (not self.connected) or (self.ser is None):
                return False
            self.ser.write(frame)
            self.ser.flush()
            return True
        except (serial.SerialException, OSError):
            self._handle_disconnect()
            return False

    @staticmethod
    def _is_reboot_command(line: str) -> bool:
        return line.strip().lower() == "reboot"

    def _set_boot_wait_state(self, delay_sec: float):
        self._shell_ready = False
        self.waiting_response = False
        self._shell_resp_buf.clear()
        self._shell_ready_since_ts = 0.0
        self._next_boot_probe_ts = time.monotonic() + max(0.0, delay_sec)

    def _print_shell_intro_unlocked(self):
        self._print_logo()
        self._write_line("BRICOS Shell (NRT Frame Transport)")
        self._write_line("Ctrl-C to quit")
        self._write_line("This client provides local command + argument completion for Tab.")
        self._write_line("Welcome to our bot world! Type 'help' to get started.")
        self._write_line()
        sys.stdout.flush()

    def _mark_shell_ready(self, force_refresh: bool = False):
        with self.lock:
            if self._shell_ready and (not force_refresh):
                return
            self._shell_ready = True
            self.waiting_response = False
            self._shell_resp_buf.clear()
            self.line = ""
            self._shell_ready_since_ts = time.monotonic()
            sys.stdout.write("\r\n")
            self._print_shell_intro_unlocked()
            self._redraw_input_unlocked()

    def _send_boot_detect(self):
        frame = self._build_frame(CMD_SHELL_BOOT_DETECT, self.SHELL_BOOT_DETECT_PAYLOAD)
        try:
            if (not self.connected) or (self.ser is None):
                return
            self.ser.write(frame)
            self.ser.flush()
        except (serial.SerialException, OSError):
            self._handle_disconnect()

    def _poll_boot_detect(self):
        if self._shell_ready or (not self.connected):
            return
        now = time.monotonic()
        if now < self._next_boot_probe_ts:
            return
        self._next_boot_probe_ts = now + self.SHELL_BOOT_PROBE_INTERVAL_SEC
        self._send_boot_detect()

    def _print_async(self, text: str):
        with self.lock:
            sys.stdout.write("\r\033[2K" + text + "\r\n")
            self._redraw_input_unlocked()

    def _redraw_input_unlocked(self):
        if not self._shell_ready:
            sys.stdout.write("\r\033[2K")
            sys.stdout.flush()
            return
        if self.waiting_response:
            sys.stdout.write("\r\033[2K")
            sys.stdout.flush()
            return
        sys.stdout.write(
            "\r\033[2K"
            + self.ANSI_GREEN
            + self.prompt_name
            + self.ANSI_WHITE
            + self.prompt_symbol
            + " "
            + self.ANSI_RESET
            + self.line
        )
        sys.stdout.flush()

    @staticmethod
    def _print_hint_list(hits):
        sys.stdout.write("\r\n" + "  ".join(hits) + "\r\n")

    def _arg_candidates(self, cmd: str, args_before, arg_index: int, prefix: str):
        pool = []
        cmd = cmd.lower()
        pfx = prefix.lower()

        if cmd in self.QUERY_COMMANDS:
            if arg_index == 1:
                pool = ["request"]
        elif cmd == "sysmode":
            if arg_index == 1:
                pool = ["request", "set"]
            elif (arg_index == 2) and (len(args_before) >= 1) and (args_before[0].lower() == "set"):
                pool = self.SYSMODE_TARGETS
        elif cmd == "momode":
            if arg_index == 1:
                pool = ["request", "set"]
            elif (arg_index == 2) and (len(args_before) >= 1) and (args_before[0].lower() == "set"):
                pool = self.MOMODE_TARGETS

        return [x for x in pool if x.startswith(pfx)]

    def _resolve_completion(self):
        line = self.line
        ends_with_space = line.endswith(" ")
        tokens = line.split()

        if not tokens:
            return "command", "", self.COMMANDS

        first = tokens[0].lower()
        if (len(tokens) == 1) and (not ends_with_space):
            hits = [c for c in self.COMMANDS if c.startswith(first)]
            if (len(hits) == 1) and (hits[0] == first):
                arg_hits = self._arg_candidates(first, [], 1, "")
                if arg_hits:
                    return "arg", "", arg_hits
            return "command", first, hits

        if first not in self.COMMANDS:
            return "none", "", []

        arg_index = len(tokens) if ends_with_space else (len(tokens) - 1)
        prefix = "" if ends_with_space else tokens[-1]
        args_before = tokens[1:arg_index]
        hits = self._arg_candidates(first, args_before, arg_index, prefix)
        return "arg", prefix, hits

    def _complete_line(self):
        kind, prefix, hits = self._resolve_completion()

        if not hits:
            sys.stdout.write("\a")
            sys.stdout.flush()
            return

        if len(hits) > 1:
            self._print_hint_list(hits)
            self._redraw_input_unlocked()
            return

        hit = hits[0]
        if kind == "command":
            self.line = hit + " "
            self._redraw_input_unlocked()
            return

        if self.line.endswith(" "):
            self.line = self.line + hit + " "
        else:
            base = self.line[: len(self.line) - len(prefix)] if prefix else self.line
            self.line = base + hit + " "
        self._redraw_input_unlocked()

    def _history_up(self):
        if not self.history:
            sys.stdout.write("\a")
            sys.stdout.flush()
            return
        if self.history_index > 0:
            self.history_index -= 1
        self.line = self.history[self.history_index]
        self._redraw_input_unlocked()

    def _history_down(self):
        if not self.history:
            sys.stdout.write("\a")
            sys.stdout.flush()
            return
        if self.history_index < (len(self.history) - 1):
            self.history_index += 1
            self.line = self.history[self.history_index]
        else:
            self.history_index = len(self.history)
            self.line = ""
        self._redraw_input_unlocked()

    @staticmethod
    def _read_escape_seq(fd: int) -> bytes:
        seq = bytearray()
        deadline = time.monotonic() + 0.02
        while time.monotonic() < deadline:
            rlist, _, _ = select.select([fd], [], [], 0.002)
            if not rlist:
                break
            nxt = os.read(fd, 1)
            if not nxt:
                break
            seq.extend(nxt)

            if len(seq) >= 2 and seq[0] in (ord("["), ord("O")) and seq[1] in (ord("A"), ord("B"), ord("C"), ord("D")):
                break
        return bytes(seq)

    def _print_logo(self):
        logo_lines = [
            " ____  ____  ___ ____ ___  ____",
            "| __ )|  _ \\|_ _/ ___/ _ \\/ ___|",
            "|  _ \\| |_) || | |  | | | \\___ \\",
            "| |_) |  _ < | | |__| |_| |___) |",
            "|____/|_| \\_\\___\\____\\___/|____/ ",
        ]

        for idx, line in enumerate(logo_lines):
            color = self.ANSI_GRADIENT[idx % len(self.ANSI_GRADIENT)]
            self._write_line(f"\033[38;5;{color}m{line}{self.ANSI_RESET}")

    def _connect_serial(self, initial: bool):
        while self.running and (not self.connected):
            try:
                self.ser = serial.Serial(port=self.port, baudrate=self.baud, timeout=0.05)
                self.connected = True
                self.parser = HydroParser()
                self._set_boot_wait_state(delay_sec=0.0)
                if not initial:
                    self._print_async("[INFO] serial reconnected, waiting for startup ack...")
                return
            except (serial.SerialException, OSError):
                time.sleep(self.RECONNECT_RETRY_SEC)

    def _handle_disconnect(self):
        self.connected = False
        self._shell_ready = False
        self.waiting_response = False
        self._shell_resp_buf.clear()
        self._next_boot_probe_ts = 0.0
        self._last_boot_status_ts = 0.0
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None

    def _format_shell_response_text(self, raw_text: str) -> str:
        text = raw_text or ""
        if not text.startswith("ret="):
            return text

        tail = text[4:]
        pos = 0
        while pos < len(tail) and tail[pos].isdigit():
            pos += 1

        if pos == 0:
            return text

        ret = int(tail[:pos])
        payload = tail[pos:].lstrip()

        if ret == 0:
            return payload

        ret_name, ret_desc = self.SHELL_RET_MAP.get(ret, (f"RET_{ret}", "未知错误"))
        if payload:
            return f"[ERR {ret}:{ret_name}] {payload}"
        return f"[ERR {ret}:{ret_name}] {ret_desc}"

    def _handle_frame(self, cmd: int, payload: bytes):
        if cmd == CMD_SHELL_BOOT_STATUS:
            status_text = payload.decode("utf-8", errors="replace").strip().lower()
            if status_text == self.SHELL_BOOT_STATUS_TEXT:
                now = time.monotonic()
                last_ts = self._last_boot_status_ts
                self._last_boot_status_ts = now

                if self._shell_ready:
                    ready_since = self._shell_ready_since_ts
                    if (ready_since > 0.0) and ((now - ready_since) < self.BOOT_STATUS_STARTUP_SETTLE_SEC):
                        return
                    if (now - last_ts) < self.BOOT_STATUS_DUP_GUARD_SEC:
                        return
                    self._mark_shell_ready(force_refresh=True)
                else:
                    self._mark_shell_ready()
            elif self.verbose_frames:
                self._print_async(f"[BOOT] unexpected status: {status_text}")
            return

        if cmd == CMD_SHELL_RESP:
            # Shell response may be split across multiple 0x21 frames.
            self._shell_resp_buf.extend(payload)
            if self._shell_resp_buf.endswith(b"\r\n") or self._shell_resp_buf.endswith(b"\n"):
                raw_txt = self._shell_resp_buf.decode("utf-8", errors="replace").rstrip("\r\n")
                txt = self._format_shell_response_text(raw_txt)
                self.waiting_response = False
                self._print_async(txt)
                self._shell_resp_buf.clear()
            return

        if cmd == CMD_ACK and len(payload) >= 4 and payload[0] == CMD_ACK:
            ack_cmd = payload[1]
            ack_code = payload[2]
            seq = payload[3]
            desc = ACK_MAP.get(ack_code, f"0x{ack_code:02X}")
            self._print_async(f"[ACK] cmd=0x{ack_cmd:02X} code={desc} seq={seq}")
            return
        
        if cmd == CMD_LOG:
            txt = payload.decode("utf-8", errors="replace").rstrip("\r\n")
            self._print_async(f"[LOG] {txt}")
            return


        if self.verbose_frames:
            self._print_async(f"[FRAME] cmd=0x{cmd:02X} len={len(payload)}")

    def _rx_loop(self):
        while self.running:
            if not self.connected:
                self._connect_serial(initial=False)
                continue

            self._poll_boot_detect()

            try:
                if self.ser is None:
                    self._handle_disconnect()
                    continue
                data = self.ser.read(256)
            except (serial.SerialException, OSError):
                self._handle_disconnect()
                continue

            if not data:
                continue

            for cmd, payload in self.parser.feed(data):
                self._handle_frame(cmd, payload)

    def run(self):
        self._connect_serial(initial=True)
        self.rx_thread.start()
        print("Waiting for device startup ack...")

        fd = sys.stdin.fileno()
        with RawMode(fd):
            with self.lock:
                self._redraw_input_unlocked()
            while self.running:
                rlist, _, _ = select.select([fd], [], [], 0.05)
                if not rlist:
                    continue

                ch = os.read(fd, 1)
                if not ch:
                    continue
                c = ch[0]

                if c == 3:
                    self.running = False
                    break

                with self.lock:
                    if not self._shell_ready:
                        if c == 27:
                            _ = self._read_escape_seq(fd)
                        sys.stdout.write("\a")
                        sys.stdout.flush()
                        continue

                    if self.waiting_response:
                        if c == 27:
                            _ = self._read_escape_seq(fd)
                        sys.stdout.write("\a")
                        sys.stdout.flush()
                        continue

                    if c in (13, 10):
                        line = self.line.strip()
                        sys.stdout.write("\r\n")
                        sys.stdout.flush()
                        self.line = ""
                        if line:
                            if (not self.history) or (self.history[-1] != line):
                                self.history.append(line)
                            self.history_index = len(self.history)
                            if self.send_shell_line(line):
                                if self._is_reboot_command(line):
                                    self._set_boot_wait_state(self.REBOOT_BOOT_PROBE_DELAY_SEC)
                                    sys.stdout.write("[INFO] reboot sent, waiting for startup ack...\r\n")
                                    sys.stdout.flush()
                                else:
                                    self.waiting_response = True
                            else:
                                self.waiting_response = False
                        else:
                            self.history_index = len(self.history)
                        self._redraw_input_unlocked()
                        continue

                    if c in (8, 127):
                        if self.line:
                            self.line = self.line[:-1]
                            self.history_index = len(self.history)
                            self._redraw_input_unlocked()
                        else:
                            sys.stdout.write("\a")
                            sys.stdout.flush()
                        continue

                    if c == 9:
                        self._complete_line()
                        continue

                    if c == 27:
                        seq = self._read_escape_seq(fd)
                        if seq in (b"[A", b"OA"):
                            self._history_up()
                            continue
                        if seq in (b"[B", b"OB"):
                            self._history_down()
                            continue
                        sys.stdout.write("\a")
                        sys.stdout.flush()
                        continue

                    if 32 <= c <= 126:
                        self.line += chr(c)
                        self.history_index = len(self.history)
                        sys.stdout.write(chr(c))
                        sys.stdout.flush()
                        continue

                    sys.stdout.write("\a")
                    sys.stdout.flush()

        self.running = False
        self.rx_thread.join(timeout=0.2)
        if self.ser is not None:
            self.ser.close()
        print("\nbye")


def parse_args():
    p = argparse.ArgumentParser(description="NRT shell client")
    p.add_argument("--port", default="/dev/ttyS7", help="serial port")
    p.add_argument("--baud", type=int, default=921600, help="baud rate")
    p.add_argument("--verbose-frames", action="store_true", help="print non-shell frames")
    return p.parse_args()


def main():
    args = parse_args()
    client = ShellClient(args.port, args.baud, verbose_frames=args.verbose_frames)
    client.run()


if __name__ == "__main__":
    main()
