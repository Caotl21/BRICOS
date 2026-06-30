#!/usr/bin/env python3
import argparse
import json
import os
import select
import socket
import sys
import threading
import time

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
        if (not IS_WINDOWS) and (self._old is not None):
            termios.tcsetattr(self.fd, termios.TCSADRAIN, self._old)


def enable_windows_virtual_terminal():
    if not IS_WINDOWS:
        return

    kernel32 = ctypes.windll.kernel32
    stdout_handle = kernel32.GetStdHandle(-11)
    if stdout_handle in (0, -1):
        return

    mode = ctypes.c_uint32()
    if kernel32.GetConsoleMode(stdout_handle, ctypes.byref(mode)) == 0:
        return

    enable_vt = 0x0004
    if mode.value & enable_vt:
        return

    kernel32.SetConsoleMode(stdout_handle, mode.value | enable_vt)


def build_default_client_name():
    host = socket.gethostname()
    pid = os.getpid()

    try:
        tty_name = os.ttyname(sys.stdin.fileno())
        tty_label = os.path.basename(tty_name).replace("/", "_")
        return f"{host}:pid{pid}:{tty_label}"
    except (AttributeError, OSError, ValueError):
        return f"{host}:pid{pid}"


class BridgeSocket:
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.sock = None
        self.file_obj = None
        self.send_lock = threading.Lock()

    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=3.0)
        self.sock.settimeout(None)
        self.file_obj = self.sock.makefile("rb")

    def send(self, message):
        payload = (json.dumps(message, separators=(",", ":")) + "\n").encode("utf-8")
        with self.send_lock:
            self.sock.sendall(payload)

    def recv(self):
        raw = self.file_obj.readline()
        if not raw:
            return None
        return json.loads(raw.decode("utf-8"))

    def close(self):
        if self.file_obj is not None:
            try:
                self.file_obj.close()
            except OSError:
                pass
            self.file_obj = None
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None


class AttachClient:
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
        "task_stack",
    ]
    QUERY_COMMANDS = ["euler", "depthtemp", "power", "cabin", "chip", "task_stack"]
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
    ANSI_BOOT_BLUE = "\033[38;5;25m"
    ANSI_CYAN = "\033[96m"
    ANSI_GREEN = "\033[92m"
    ANSI_RED = "\033[91m"
    ANSI_YELLOW = "\033[93m"
    ANSI_WHITE = "\033[97m"
    ANSI_GRADIENT = [196, 202, 226, 82, 45]

    def __init__(self, host, port, name):
        self.host = host
        self.port = port
        self.name = name
        self.bridge = BridgeSocket(host, port)
        self.running = True
        self.line = ""
        self.prompt_name = "bricsbot1"
        self.prompt_symbol = "$"
        self.history = []
        self.history_index = 0
        self.lock = threading.RLock()
        self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)

        self.connected = False
        self.client_id = None
        self.has_control = False
        self.serial_connected = False
        self.shell_ready = False
        self._intro_printed = False
        self._reboot_waiting_banner = False
        self._reboot_not_ready_seen = False

    @staticmethod
    def _write_line(text=""):
        sys.stdout.write(text + "\r\n")

    @staticmethod
    def _is_reboot_command(line):
        return line.strip().lower() == "reboot"

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

    def _print_intro_unlocked(self):
        self._print_logo()
        self._write_line("BRICOS Shell (Bridge Transport)")
        self._write_line("Ctrl-C to quit")
        self._write_line("This client provides local command + argument completion for Tab.")
        self._write_line("Welcome to our bot world! Type 'help' to get started.")
        self._write_line()
        sys.stdout.flush()

    def _mark_shell_ready_unlocked(self, force_refresh=False):
        if self.shell_ready and (not force_refresh):
            return
        self.shell_ready = True
        self.line = ""
        sys.stdout.write("\r\n")
        self._print_intro_unlocked()
        self._intro_printed = True
        self._redraw_input_unlocked()

    @classmethod
    def _style_log_text(cls, text):
        upper = text.upper()
        if ("[ERROR]" in upper) or ("[ERR ]" in upper) or (" ERROR " in f" {upper} "):
            return f"{cls.ANSI_RED}{text}{cls.ANSI_RESET}"
        if ("[WARNING]" in upper) or ("[WARN]" in upper) or (" WARNING " in f" {upper} "):
            return f"{cls.ANSI_YELLOW}{text}{cls.ANSI_RESET}"
        if "[BOOT]" in upper:
            return f"{cls.ANSI_BOOT_BLUE}{text}{cls.ANSI_RESET}"
        if ("[INFO]" in upper) or (" INFO " in f" {upper} "):
            return f"{cls.ANSI_CYAN}{text}{cls.ANSI_RESET}"
        return text

    def _print_async(self, text):
        with self.lock:
            sys.stdout.write("\r\033[2K" + self._style_log_text(text) + "\r\n")
            self._redraw_input_unlocked()

    def _redraw_input_unlocked(self):
        if (not self._intro_printed) or (not self.connected) or (not self.serial_connected) or (not self.shell_ready):
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

    def _arg_candidates(self, cmd, args_before, arg_index, prefix):
        pool = []
        cmd = cmd.lower()
        pfx = prefix.lower()

        if cmd in self.QUERY_COMMANDS:
            if arg_index == 1:
                pool = ["request"]
        elif cmd == "sysmode":
            if arg_index == 1:
                pool = ["request", "set"]
            elif (arg_index == 2) and args_before and (args_before[0].lower() == "set"):
                pool = self.SYSMODE_TARGETS
        elif cmd == "momode":
            if arg_index == 1:
                pool = ["request", "set"]
            elif (arg_index == 2) and args_before and (args_before[0].lower() == "set"):
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
            elif (arg_index == 2) and args_before:
                if args_before[0].lower() in ("solid", "breath", "chase", "warn"):
                    pool = self.WS2812_COLOR_TARGETS
        elif cmd == "thruster":
            if arg_index == 1:
                pool = self.THRUSTER_ACTIONS
        elif cmd == "ws2812":
            if arg_index == 1:
                pool = self.WS2812_ACTIONS
            elif (arg_index == 2) and args_before:
                action = args_before[0].lower()
                if action in ("clear", "all", "color", "refresh"):
                    pool = self.WS2812_STRIP_TARGETS
                elif action in ("set", "pixel"):
                    pool = self.WS2812_SINGLE_STRIP_TARGETS
            elif (arg_index == 3) and (len(args_before) >= 2) and (args_before[0].lower() == "color"):
                pool = self.WS2812_COLOR_TARGETS
            elif (arg_index == 4) and (len(args_before) >= 3) and (args_before[0].lower() == "pixel"):
                pool = self.WS2812_COLOR_TARGETS

        return [item for item in pool if item.startswith(pfx)]

    def _complete_line(self):
        line = self.line
        ends_with_space = line.endswith(" ")
        tokens = line.split()

        if not tokens:
            hits = self.COMMANDS
            if len(hits) > 1:
                sys.stdout.write("\r\n" + "  ".join(hits) + "\r\n")
                self._redraw_input_unlocked()
                return
            self.line = hits[0] + " "
            self._redraw_input_unlocked()
            return

        first = tokens[0].lower()
        if (len(tokens) == 1) and (not ends_with_space):
            hits = [cmd for cmd in self.COMMANDS if cmd.startswith(first)]
            if len(hits) == 1:
                self.line = hits[0] + " "
                self._redraw_input_unlocked()
                return
            if not hits:
                sys.stdout.write("\a")
                sys.stdout.flush()
                return
            sys.stdout.write("\r\n" + "  ".join(hits) + "\r\n")
            self._redraw_input_unlocked()
            return

        if first not in self.COMMANDS:
            sys.stdout.write("\a")
            sys.stdout.flush()
            return

        arg_index = len(tokens) if ends_with_space else (len(tokens) - 1)
        prefix = "" if ends_with_space else tokens[-1]
        args_before = tokens[1:arg_index]
        hits = self._arg_candidates(first, args_before, arg_index, prefix)
        if not hits:
            sys.stdout.write("\a")
            sys.stdout.flush()
            return
        if len(hits) > 1:
            sys.stdout.write("\r\n" + "  ".join(hits) + "\r\n")
            self._redraw_input_unlocked()
            return

        if ends_with_space:
            self.line = self.line + hits[0] + " "
        else:
            base = self.line[: len(self.line) - len(prefix)] if prefix else self.line
            self.line = base + hits[0] + " "
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
    def _read_escape_seq(fd):
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
            if len(seq) >= 2 and seq[0] in (ord("["), ord("O")) and seq[1] in (ord("A"), ord("B")):
                break
        return bytes(seq)

    @staticmethod
    def _wait_for_key(fd, timeout_sec):
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
    def _read_key(fd):
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
            seq = AttachClient._read_escape_seq(fd)
            if seq in (b"[A", b"OA"):
                return ("up", None)
            if seq in (b"[B", b"OB"):
                return ("down", None)
            return ("other", None)
        if 32 <= c <= 126:
            return ("char", chr(c))
        return ("other", None)

    def _handle_message(self, message):
        msg_type = message.get("type", "")

        if msg_type == "hello":
            self.connected = True
            self.client_id = message.get("client_id")
            return

        if msg_type == "state":
            with self.lock:
                previous_ready = self.shell_ready
                self.serial_connected = bool(message.get("serial_connected"))
                remote_shell_ready = bool(message.get("shell_ready"))
                self.has_control = message.get("controller_id") == self.client_id

                if self._reboot_waiting_banner:
                    if remote_shell_ready:
                        if self._reboot_not_ready_seen:
                            self._mark_shell_ready_unlocked(force_refresh=False)
                            self._reboot_waiting_banner = False
                            self._reboot_not_ready_seen = False
                        else:
                            self.shell_ready = False
                    else:
                        self.shell_ready = False
                        self._reboot_not_ready_seen = True
                else:
                    if remote_shell_ready:
                        if not previous_ready:
                            self._mark_shell_ready_unlocked(force_refresh=False)
                    else:
                        self.shell_ready = False
                        if self.has_control and previous_ready:
                            sys.stdout.write("\r\033[2K[INFO] waiting for startup banner...\r\n")

                self._redraw_input_unlocked()
            return

        if msg_type == "grant":
            with self.lock:
                self._redraw_input_unlocked()
            return

        if msg_type == "busy":
            owner = message.get("controller_name") or "unknown"
            self._print_async(f"[BUSY] shell is controlled by {owner}")
            return

        if msg_type == "accepted":
            return

        if msg_type in ("log", "info", "warn", "error"):
            prefix = ""
            if msg_type == "info":
                prefix = "[INFO] "
            elif msg_type == "warn":
                prefix = "[WARN] "
            elif msg_type == "error":
                prefix = "[ERR ] "
            self._print_async(prefix + str(message.get("text", "")))
            return

    def _rx_loop(self):
        try:
            while self.running:
                message = self.bridge.recv()
                if message is None:
                    self._print_async("[WARN] bridge disconnected")
                    self.running = False
                    break
                self._handle_message(message)
        except Exception as exc:
            self._print_async(f"[WARN] {exc}")
            self.running = False

    def run(self):
        enable_windows_virtual_terminal()
        self.bridge.connect()
        self.bridge.send({"type": "hello", "mode": "interactive", "name": self.name})
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
                        if (not self.serial_connected) or (not self.shell_ready):
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
                                self.bridge.send({"type": "line", "text": line})
                                if self._is_reboot_command(line):
                                    self.shell_ready = False
                                    self._reboot_waiting_banner = True
                                    self._reboot_not_ready_seen = False
                                    sys.stdout.write("[INFO] reboot sent, waiting for startup banner...\r\n")
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
        finally:
            try:
                self.bridge.send({"type": "detach"})
            except Exception:
                pass
            self.bridge.close()
            self.rx_thread.join(timeout=0.2)
            print("\nbye")


def run_watch(host, port, name):
    bridge = BridgeSocket(host, port)
    bridge.connect()
    bridge.send({"type": "hello", "mode": "monitor", "name": name})
    try:
        while True:
            message = bridge.recv()
            if message is None:
                raise SystemExit("bridge disconnected")
            msg_type = message.get("type", "")
            if msg_type == "state":
                controller = message.get("controller_name") or "-"
                print(
                    f"[state] serial_connected={message.get('serial_connected')} "
                    f"shell_ready={message.get('shell_ready')} controller={controller}"
                )
            elif msg_type == "log":
                print(AttachClient._style_log_text(message.get("text", "")))
            elif msg_type == "info":
                print(AttachClient._style_log_text(f"[INFO] {message.get('text', '')}"))
            elif msg_type == "warn":
                print(AttachClient._style_log_text(f"[WARN] {message.get('text', '')}"))
            elif msg_type == "error":
                print(AttachClient._style_log_text(f"[ERR ] {message.get('text', '')}"))
    except KeyboardInterrupt:
        pass
    finally:
        bridge.close()


def run_status(host, port, name):
    bridge = BridgeSocket(host, port)
    bridge.connect()
    bridge.send({"type": "hello", "mode": "status", "name": name})
    try:
        while True:
            message = bridge.recv()
            if message is None:
                raise SystemExit("bridge disconnected")
            if message.get("type") == "state":
                controller = message.get("controller_name") or "-"
                print(
                    f"serial_connected={message.get('serial_connected')} "
                    f"shell_ready={message.get('shell_ready')} "
                    f"controller={controller}"
                )
                return
    finally:
        bridge.close()


def run_exec(host, port, name, command, timeout_sec):
    bridge = BridgeSocket(host, port)
    try:
        bridge.connect()
        bridge.send(
            {
                "type": "hello",
                "mode": "exec",
                "name": name,
                "command": command,
                "timeout_sec": timeout_sec,
            }
        )
        while True:
            message = bridge.recv()
            if message is None:
                raise SystemExit("bridge disconnected")

            msg_type = message.get("type", "")
            if msg_type == "exec_line":
                print(AttachClient._style_log_text(message.get("text", "")))
            elif msg_type == "busy":
                owner = message.get("controller_name") or "unknown"
                raise SystemExit(f"shell is busy: {owner}")
            elif msg_type == "exec_done":
                if not bool(message.get("ok")):
                    reason = message.get("reason") or "unknown error"
                    raise SystemExit(f"exec failed: {reason}")
                return
    finally:
        bridge.close()


def parse_args():
    parser = argparse.ArgumentParser(description="BRICOS shell bridge client")
    parser.add_argument("--host", default="192.168.13.12", help="bridge host")
    parser.add_argument("--port", type=int, default=8765, help="bridge port")
    parser.add_argument("--name", default=build_default_client_name(), help="client display name")

    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("attach", help="interactive shell attach")
    subparsers.add_parser("watch", help="watch logs and state changes")
    subparsers.add_parser("status", help="print bridge state")

    exec_parser = subparsers.add_parser("exec", help="run one shell command")
    exec_parser.add_argument("shell_line", help="shell command to send")
    exec_parser.add_argument("--timeout", type=float, default=5.0, help="response timeout seconds")

    return parser.parse_args()


def main():
    args = parse_args()
    if args.command == "attach":
        client = AttachClient(args.host, args.port, args.name)
        client.run()
        return
    if args.command == "watch":
        run_watch(args.host, args.port, args.name)
        return
    if args.command == "status":
        run_status(args.host, args.port, args.name)
        return
    if args.command == "exec":
        run_exec(args.host, args.port, args.name, args.shell_line, args.timeout)
        return
    raise SystemExit(f"unsupported command: {args.command}")


if __name__ == "__main__":
    main()
