#!/usr/bin/env python3
import argparse
import os
import select
import sys
import threading
import time

try:
    import serial
except ModuleNotFoundError:
    serial = None

IS_WINDOWS = os.name == "nt"

if IS_WINDOWS:
    import ctypes
    import msvcrt
else:
    import termios
    import tty


class RawMode:
    def __init__(self, fd):
        self.fd = fd
        self._old = None

    def __enter__(self):
        if not IS_WINDOWS:
            self._old = termios.tcgetattr(self.fd)
            tty.setraw(self.fd)
        return self

    def __exit__(self, exc_type, exc, tb):
        if (not IS_WINDOWS) and self._old is not None:
            termios.tcsetattr(self.fd, termios.TCSADRAIN, self._old)


def enable_windows_virtual_terminal():
    if not IS_WINDOWS:
        return

    kernel32 = ctypes.windll.kernel32
    stdout_handle = kernel32.GetStdHandle(-11)
    if stdout_handle == 0 or stdout_handle == -1:
        return

    mode = ctypes.c_uint32()
    if kernel32.GetConsoleMode(stdout_handle, ctypes.byref(mode)) == 0:
        return

    enable_vt = 0x0004
    if mode.value & enable_vt:
        return

    kernel32.SetConsoleMode(stdout_handle, mode.value | enable_vt)


class ShellClient:
    COMMANDS = [
        "help",
        "echo",
        "sysmode",
        "momode",
        "log",
        "persistlog",
        "persist_log",
        "fault",
        "reboot",
        "thruster",
        "servo",
        "led",
        "ws2812",
        "euler",
        "depthtemp",
        "power",
        "cabin",
        "chip",
    ]
    QUERY_COMMANDS = ["euler", "depthtemp", "power", "cabin", "chip"]
    SYSMODE_TARGETS = ["standby", "disarmed", "armed", "failsafe"]
    MOMODE_TARGETS = ["manual", "stabilize", "auto"]
    THRUSTER_ACTIONS = ["request", "idle", "stop", "all", "set", "pulse"]
    LED_ACTIONS = ["auto", "solid", "breath", "chase", "warn", "clearwarn"]
    WS2812_ACTIONS = ["request", "clear", "all", "color", "set", "pixel", "refresh"]
    WS2812_STRIP_TARGETS = ["1", "2", "all", "strip1", "strip2", "both"]
    WS2812_SINGLE_STRIP_TARGETS = ["1", "2", "strip1", "strip2"]
    WS2812_COLOR_TARGETS = [
        "black",
        "off",
        "white",
        "red",
        "green",
        "blue",
        "yellow",
        "cyan",
        "magenta",
        "pink",
        "orange",
        "purple",
        "violet",
    ]

    ANSI_RESET = "\033[0m"
    ANSI_GREEN = "\033[92m"
    ANSI_WHITE = "\033[97m"
    ANSI_GRADIENT = [196, 202, 226, 82, 45]
    RECONNECT_RETRY_SEC = 0.3
    BOOT_STATUS_DUP_GUARD_SEC = 1.0
    BOOT_STATUS_STARTUP_SETTLE_SEC = 3.0
    PROMPT_REFRESH_DELAY_SEC = 0.15
    PARTIAL_LINE_FLUSH_SEC = 0.15
    SHELL_BOOT_STATUS_TEXT = "startup_success"

    @staticmethod
    def _write_line(text: str = ""):
        # In raw tty mode, "\n" does not reliably return to column 0.
        sys.stdout.write(text + "\r\n")

    def __init__(self, port: str, baud: int, verbose_frames: bool = False):
        self.port = port
        self.baud = baud
        self.ser = None
        self.connected = False
        self.running = True
        self.line = ""
        self.prompt_name = "bricsbot1"
        self.prompt_symbol = "$"
        self.history = []
        self.history_index = 0
        self.lock = threading.RLock()
        self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self.verbose_frames = verbose_frames
        self._shell_ready = False
        self._last_boot_status_ts = 0.0
        self._shell_ready_since_ts = 0.0
        self._rx_line_buf = bytearray()
        self._rx_last_was_cr = False
        self._last_rx_byte_ts = 0.0
        self._next_prompt_refresh_ts = 0.0

    def send_shell_line(self, line: str):
        payload = line.encode("ascii", errors="ignore") + b"\r\n"
        try:
            if (not self.connected) or (self.ser is None):
                return False
            self.ser.write(payload)
            self.ser.flush()
            return True
        except (serial.SerialException, OSError):
            self._handle_disconnect()
            return False

    @staticmethod
    def _is_reboot_command(line: str) -> bool:
        return line.strip().lower() == "reboot"

    def _set_boot_wait_state(self):
        with self.lock:
            self._shell_ready = False
            self._shell_ready_since_ts = 0.0
            self.line = ""
            self._next_prompt_refresh_ts = 0.0
            self._redraw_input_unlocked()

    def _print_shell_intro_unlocked(self):
        self._print_logo()
        self._write_line("BRICOS Shell (UART Stream Transport)")
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
            self.line = ""
            self._shell_ready_since_ts = time.monotonic()
            self._next_prompt_refresh_ts = self._shell_ready_since_ts + self.PROMPT_REFRESH_DELAY_SEC
            sys.stdout.write("\r\n")
            self._print_shell_intro_unlocked()
            self._redraw_input_unlocked()

    def _print_async(self, text: str):
        with self.lock:
            sys.stdout.write("\r\033[2K" + text + "\r\n")
            self._next_prompt_refresh_ts = time.monotonic() + self.PROMPT_REFRESH_DELAY_SEC
            self._redraw_input_unlocked()

    def _redraw_input_unlocked(self):
        if (not self.connected) or (not self._shell_ready):
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
        elif cmd == "fault":
            if arg_index == 1:
                pool = ["clear_overflow"]
        elif cmd in ("log", "persistlog", "persist_log"):
            if arg_index == 1:
                pool = ["clear"]
        elif cmd == "servo":
            if arg_index == 1:
                pool = ["set"]
        elif cmd == "led":
            if arg_index == 1:
                pool = self.LED_ACTIONS
            elif (arg_index == 2) and (len(args_before) >= 1):
                action = args_before[0].lower()
                if action in ("solid", "breath", "chase", "warn"):
                    pool = self.WS2812_COLOR_TARGETS
        elif cmd == "thruster":
            if arg_index == 1:
                pool = self.THRUSTER_ACTIONS
        elif cmd == "ws2812":
            if arg_index == 1:
                pool = self.WS2812_ACTIONS
            elif (arg_index == 2) and (len(args_before) >= 1):
                action = args_before[0].lower()
                if action in ("clear", "all", "color", "refresh"):
                    pool = self.WS2812_STRIP_TARGETS
                elif action in ("set", "pixel"):
                    pool = self.WS2812_SINGLE_STRIP_TARGETS
            elif (arg_index == 3) and (len(args_before) >= 2) and (args_before[0].lower() == "color"):
                pool = self.WS2812_COLOR_TARGETS
            elif (arg_index == 4) and (len(args_before) >= 3) and (args_before[0].lower() == "pixel"):
                pool = self.WS2812_COLOR_TARGETS

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

    @staticmethod
    def _wait_for_key(fd: int, timeout_sec: float) -> bool:
        if IS_WINDOWS:
            deadline = time.monotonic() + timeout_sec
            while time.monotonic() < deadline:
                if msvcrt.kbhit():
                    return True
                time.sleep(0.01)
            return False

        rlist, _, _ = select.select([fd], [], [], timeout_sec)
        return bool(rlist)

    @staticmethod
    def _read_key(fd: int):
        if IS_WINDOWS:
            try:
                ch = msvcrt.getwch()
            except KeyboardInterrupt:
                return ("ctrl_c", None)

            if ch == "\x03":
                return ("ctrl_c", None)
            if ch in ("\r", "\n"):
                return ("enter", None)
            if ch == "\t":
                return ("tab", None)
            if ch == "\b":
                return ("backspace", None)
            if ch == "\x1b":
                return ("escape", None)
            if ch in ("\x00", "\xe0"):
                ext = msvcrt.getwch()
                if ext == "H":
                    return ("up", None)
                if ext == "P":
                    return ("down", None)
                return ("other", None)
            if len(ch) == 1 and 32 <= ord(ch) <= 126:
                return ("char", ch)
            return ("other", None)

        ch = os.read(fd, 1)
        if not ch:
            return ("other", None)

        c = ch[0]
        if c == 3:
            return ("ctrl_c", None)
        if c in (13, 10):
            return ("enter", None)
        if c in (8, 127):
            return ("backspace", None)
        if c == 9:
            return ("tab", None)
        if c == 27:
            seq = ShellClient._read_escape_seq(fd)
            if seq in (b"[A", b"OA"):
                return ("up", None)
            if seq in (b"[B", b"OB"):
                return ("down", None)
            return ("escape", None)
        if 32 <= c <= 126:
            return ("char", chr(c))
        return ("other", None)

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
                self._rx_line_buf.clear()
                self._rx_last_was_cr = False
                self._last_boot_status_ts = 0.0
                self._mark_shell_ready(force_refresh=not initial)
                if not initial:
                    self._print_async("[INFO] serial reconnected.")
                return
            except (serial.SerialException, OSError):
                time.sleep(self.RECONNECT_RETRY_SEC)

    def _handle_disconnect(self):
        was_connected = self.connected
        self.connected = False
        with self.lock:
            self._shell_ready = False
            self._shell_ready_since_ts = 0.0
            self._rx_line_buf.clear()
            self._rx_last_was_cr = False
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
        if was_connected and self.running:
            self._print_async("[WARN] serial disconnected, retrying...")

    def _handle_rx_line(self, text: str):
        status_text = text.strip().lower()
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
            return

        self._print_async(text)

    def _flush_partial_rx_line(self, force: bool = False):
        if not self._rx_line_buf:
            return

        now = time.monotonic()
        if (not force) and ((now - self._last_rx_byte_ts) < self.PARTIAL_LINE_FLUSH_SEC):
            return

        text = self._rx_line_buf.decode("utf-8", errors="replace")
        self._rx_line_buf.clear()
        self._rx_last_was_cr = False
        self._handle_rx_line(text)

    def _maybe_refresh_prompt(self):
        if self._next_prompt_refresh_ts <= 0.0:
            return

        now = time.monotonic()
        if now < self._next_prompt_refresh_ts:
            return

        with self.lock:
            self._next_prompt_refresh_ts = 0.0
            self._redraw_input_unlocked()

    def _process_rx_bytes(self, data: bytes):
        self._last_rx_byte_ts = time.monotonic()
        for byte in data:
            if byte in (0x0D, 0x0A):
                if byte == 0x0A and self._rx_last_was_cr:
                    self._rx_last_was_cr = False
                    continue

                text = self._rx_line_buf.decode("utf-8", errors="replace")
                self._rx_line_buf.clear()
                self._handle_rx_line(text)
                self._rx_last_was_cr = byte == 0x0D
                continue

            self._rx_last_was_cr = False
            self._rx_line_buf.append(byte)

    def _rx_loop(self):
        while self.running:
            if not self.connected:
                self._connect_serial(initial=False)
                continue

            try:
                if self.ser is None:
                    self._handle_disconnect()
                    continue
                data = self.ser.read(256)
            except (serial.SerialException, OSError):
                self._handle_disconnect()
                continue

            if not data:
                self._flush_partial_rx_line(force=False)
                self._maybe_refresh_prompt()
                continue

            self._process_rx_bytes(data)
            self._maybe_refresh_prompt()

    def run(self):
        enable_windows_virtual_terminal()
        self._connect_serial(initial=True)
        self.rx_thread.start()

        fd = sys.stdin.fileno()
        try:
            with RawMode(fd):
                with self.lock:
                    self._redraw_input_unlocked()
                while self.running:
                    if not self._wait_for_key(fd, 0.05):
                        continue

                    key_kind, key_value = self._read_key(fd)
                    if key_kind == "ctrl_c":
                        self.running = False
                        break

                    with self.lock:
                        if (not self.connected) or (not self._shell_ready):
                            sys.stdout.write("\a")
                            sys.stdout.flush()
                            continue

                        if key_kind == "enter":
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
                                        self._set_boot_wait_state()
                                        sys.stdout.write("[INFO] reboot sent, waiting for startup banner...\r\n")
                                        sys.stdout.flush()
                                else:
                                    sys.stdout.write("[WARN] send failed.\r\n")
                                    sys.stdout.flush()
                            else:
                                self.history_index = len(self.history)
                            self._redraw_input_unlocked()
                            continue

                        if key_kind == "backspace":
                            if self.line:
                                self.line = self.line[:-1]
                                self.history_index = len(self.history)
                                self._redraw_input_unlocked()
                            else:
                                sys.stdout.write("\a")
                                sys.stdout.flush()
                            continue

                        if key_kind == "tab":
                            self._complete_line()
                            continue

                        if key_kind == "up":
                            self._history_up()
                            continue

                        if key_kind == "down":
                            self._history_down()
                            continue

                        if key_kind == "char":
                            self.line += key_value
                            self.history_index = len(self.history)
                            sys.stdout.write(key_value)
                            sys.stdout.flush()
                            continue

                        sys.stdout.write("\a")
                        sys.stdout.flush()
        except KeyboardInterrupt:
            self.running = False

        self.running = False
        self.rx_thread.join(timeout=0.2)
        if self.ser is not None:
            self.ser.close()
        print("\nbye")


def parse_args():
    p = argparse.ArgumentParser(description="UART shell client")
    default_port = "COM7" if IS_WINDOWS else "/dev/ttyS7"
    p.add_argument("--port", default=default_port, help="serial port")
    p.add_argument("--baud", type=int, default=1500000, help="baud rate")
    p.add_argument(
        "--verbose-frames",
        action="store_true",
        help="legacy no-op option kept for backward compatibility",
    )
    return p.parse_args()


def main():
    args = parse_args()
    if serial is None:
        raise SystemExit("pyserial is required. Install it with: pip install pyserial")
    client = ShellClient(args.port, args.baud, verbose_frames=args.verbose_frames)
    client.run()


if __name__ == "__main__":
    main()
gi