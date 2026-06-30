#!/usr/bin/env python3
import argparse
import json
import signal
import socket
import threading
import time

try:
    import serial
except ModuleNotFoundError:
    serial = None


class ClientConnection:
    def __init__(self, bridge, sock, addr, client_id):
        self.bridge = bridge
        self.sock = sock
        self.addr = addr
        self.client_id = client_id
        self.name = f"client-{client_id}"
        self.mode = "monitor"
        self.alive = True
        self.send_lock = threading.Lock()

    def send(self, message):
        if not self.alive:
            return False

        payload = (json.dumps(message, separators=(",", ":")) + "\n").encode("utf-8")
        try:
            with self.send_lock:
                self.sock.sendall(payload)
            return True
        except OSError:
            self.alive = False
            return False

    def close(self):
        self.alive = False
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass


class ExecCapture:
    def __init__(self, owner_id):
        self.owner_id = owner_id
        self.lines = []
        self.last_activity_ts = 0.0
        self.seen_output = False
        self.failed = False
        self.failure_reason = ""


class ShellBridgeDaemon:
    SHELL_BOOT_STATUS_TEXT = "startup_success"
    READ_SIZE = 256
    RECONNECT_RETRY_SEC = 0.5
    PARTIAL_LINE_FLUSH_SEC = 0.15
    EXEC_IDLE_SEC = 0.35

    def __init__(self, serial_port, baud, listen_host, listen_port, verbose=False):
        self.serial_port = serial_port
        self.baud = baud
        self.listen_host = listen_host
        self.listen_port = listen_port
        self.verbose = verbose
        self.running = True

        self.server_sock = None
        self.serial_obj = None
        self.serial_lock = threading.RLock()
        self.state_lock = threading.RLock()

        self.serial_connected = False
        self.shell_ready = False
        self._rx_line_buf = bytearray()
        self._rx_last_was_cr = False
        self._last_rx_byte_ts = 0.0

        self.clients = {}
        self.next_client_id = 1
        self.controller_id = None
        self.active_exec = None
        self.interactive_lock_active = False
        self.interactive_last_activity_ts = 0.0

    def _log(self, text):
        stamp = time.strftime("%H:%M:%S")
        print(f"[{stamp}] {text}", flush=True)

    def stop(self, reason="stopping"):
        if not self.running:
            return

        self.running = False
        self._log(reason)

        with self.state_lock:
            clients = list(self.clients.values())
            self.clients.clear()
            self.controller_id = None
            self.active_exec = None
            self.interactive_lock_active = False
            self.interactive_last_activity_ts = 0.0

        for client in clients:
            client.close()

        if self.server_sock is not None:
            try:
                self.server_sock.close()
            except OSError:
                pass
            self.server_sock = None

        with self.serial_lock:
            if self.serial_obj is not None:
                try:
                    self.serial_obj.close()
                except OSError:
                    pass
                self.serial_obj = None

    def _state_message(self):
        with self.state_lock:
            controller_name = None
            if self.controller_id is not None:
                owner = self.clients.get(self.controller_id)
                controller_name = owner.name if owner is not None else None

            return {
                "type": "state",
                "serial_connected": self.serial_connected,
                "shell_ready": self.shell_ready,
                "controller_id": self.controller_id,
                "controller_name": controller_name,
            }

    def _broadcast(self, message, exclude_client_id=None):
        stale_ids = []
        with self.state_lock:
            snapshot = list(self.clients.items())

        for client_id, client in snapshot:
            if client_id == exclude_client_id:
                continue
            if not client.send(message):
                stale_ids.append(client_id)

        for client_id in stale_ids:
            self._drop_client(client_id)

    def _broadcast_state(self):
        self._broadcast(self._state_message())

    def _set_state(self, serial_connected=None, shell_ready=None):
        changed = False
        with self.state_lock:
            if (serial_connected is not None) and (self.serial_connected != serial_connected):
                self.serial_connected = serial_connected
                changed = True
            if (shell_ready is not None) and (self.shell_ready != shell_ready):
                self.shell_ready = shell_ready
                changed = True

        if changed:
            self._broadcast_state()

    def _connect_serial(self):
        if serial is None:
            raise RuntimeError("pyserial is required. Install it with: pip install pyserial")

        with self.serial_lock:
            self.serial_obj = serial.Serial(port=self.serial_port, baudrate=self.baud, timeout=0.05)
            self._rx_line_buf.clear()
            self._rx_last_was_cr = False
            self._last_rx_byte_ts = 0.0

        self._set_state(serial_connected=True, shell_ready=True)
        self._broadcast({"type": "info", "text": f"serial connected: {self.serial_port} @ {self.baud}"})
        if self.verbose:
            self._log(f"serial connected: {self.serial_port} @ {self.baud}")

    def _disconnect_serial(self, reason):
        with self.serial_lock:
            ser = self.serial_obj
            self.serial_obj = None
            if ser is not None:
                try:
                    ser.close()
                except OSError:
                    pass

        with self.state_lock:
            exec_capture = self.active_exec
            if exec_capture is not None:
                exec_capture.failed = True
                exec_capture.failure_reason = reason
            self.controller_id = None
            self.interactive_lock_active = False
            self.interactive_last_activity_ts = 0.0

        self._set_state(serial_connected=False, shell_ready=False)
        self._broadcast({"type": "warn", "text": f"serial disconnected: {reason}"})
        if self.verbose:
            self._log(f"serial disconnected: {reason}")

    def _send_command_to_serial(self, line):
        payload = line.encode("ascii", errors="ignore") + b"\r\n"
        if line.strip().lower() == "reboot":
            self._set_state(shell_ready=False)

        with self.serial_lock:
            if self.serial_obj is None:
                return False
            try:
                self.serial_obj.write(payload)
                self.serial_obj.flush()
                return True
            except (OSError, serial.SerialException):
                return False

    def _handle_rx_line(self, text):
        status_text = text.strip().lower()
        if status_text == self.SHELL_BOOT_STATUS_TEXT:
            self._set_state(shell_ready=True)
            self._broadcast({"type": "info", "text": "device startup banner detected"})
            return

        with self.state_lock:
            exec_capture = self.active_exec
            if exec_capture is not None:
                exec_capture.lines.append(text)
                exec_capture.last_activity_ts = time.monotonic()
                exec_capture.seen_output = True
                owner = self.clients.get(exec_capture.owner_id)
            else:
                owner = None

            if self.interactive_lock_active:
                self.interactive_last_activity_ts = time.monotonic()

        if owner is not None:
            owner.send({"type": "exec_line", "text": text})

        self._broadcast({"type": "log", "text": text})

    def _flush_partial_rx_line(self, force=False):
        if not self._rx_line_buf:
            return

        now = time.monotonic()
        if (not force) and ((now - self._last_rx_byte_ts) < self.PARTIAL_LINE_FLUSH_SEC):
            return

        text = self._rx_line_buf.decode("utf-8", errors="replace")
        self._rx_line_buf.clear()
        self._rx_last_was_cr = False
        self._handle_rx_line(text)

    def _process_rx_bytes(self, data):
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

    def _serial_loop(self):
        while self.running:
            if self.serial_obj is None:
                try:
                    self._connect_serial()
                except Exception as exc:
                    if not self.running:
                        break
                    self._broadcast({"type": "warn", "text": f"serial connect failed: {exc}"})
                    if self.verbose:
                        self._log(f"serial connect failed: {exc}")
                    time.sleep(self.RECONNECT_RETRY_SEC)
                continue

            try:
                data = self.serial_obj.read(self.READ_SIZE)
            except Exception as exc:
                if not self.running:
                    break
                self._disconnect_serial(str(exc))
                time.sleep(self.RECONNECT_RETRY_SEC)
                continue

            if not data:
                self._flush_partial_rx_line(force=False)
                self._maybe_finish_interactive_lock()
                continue

            self._process_rx_bytes(data)

    def _try_acquire_control(self, client):
        with self.state_lock:
            if self.controller_id in (None, client.client_id):
                self.controller_id = client.client_id
                self._broadcast_state()
                return True, None

            owner = self.clients.get(self.controller_id)
            owner_name = owner.name if owner is not None else "unknown"
            return False, owner_name

    def _release_control(self, client_id):
        changed = False
        with self.state_lock:
            if self.controller_id == client_id:
                self.controller_id = None
                changed = True
            if changed:
                self.interactive_lock_active = False
                self.interactive_last_activity_ts = 0.0

        if changed:
            self._broadcast_state()

    def _maybe_finish_interactive_lock(self):
        changed = False
        with self.state_lock:
            if self.interactive_lock_active:
                if (time.monotonic() - self.interactive_last_activity_ts) >= self.EXEC_IDLE_SEC:
                    self.interactive_lock_active = False
                    self.interactive_last_activity_ts = 0.0
                    if self.controller_id is not None:
                        self.controller_id = None
                        changed = True

        if changed:
            self._broadcast_state()

    def _register_client(self, sock, addr):
        with self.state_lock:
            client_id = self.next_client_id
            self.next_client_id += 1
            client = ClientConnection(self, sock, addr, client_id)
            self.clients[client_id] = client
        return client

    def _drop_client(self, client_id):
        with self.state_lock:
            client = self.clients.pop(client_id, None)
            if client is None:
                return

            was_controller = self.controller_id == client_id
            if self.controller_id == client_id:
                self.controller_id = None

            if (self.active_exec is not None) and (self.active_exec.owner_id == client_id):
                self.active_exec = None

            if self.interactive_lock_active and was_controller:
                self.interactive_lock_active = False
                self.interactive_last_activity_ts = 0.0

        client.close()
        self._broadcast_state()

    def _run_exec(self, client, command, timeout_sec):
        if (not command) or (not command.strip()):
            client.send({"type": "error", "text": "empty command"})
            client.send({"type": "exec_done", "ok": False, "reason": "empty command"})
            return

        ok, owner_name = self._try_acquire_control(client)
        if not ok:
            client.send({"type": "busy", "controller_name": owner_name})
            client.send({"type": "exec_done", "ok": False, "reason": f"busy: {owner_name}"})
            return

        with self.state_lock:
            if (not self.serial_connected) or (not self.shell_ready):
                self._release_control(client.client_id)
                client.send({"type": "exec_done", "ok": False, "reason": "shell not ready"})
                return

            self.active_exec = ExecCapture(owner_id=client.client_id)

        if not self._send_command_to_serial(command):
            with self.state_lock:
                self.active_exec = None
            self._release_control(client.client_id)
            client.send({"type": "exec_done", "ok": False, "reason": "send failed"})
            return

        client.send({"type": "exec_begin", "command": command})
        deadline = time.monotonic() + timeout_sec
        result_ok = False
        result_reason = "timeout"

        while self.running and (time.monotonic() < deadline):
            time.sleep(0.05)
            with self.state_lock:
                capture = self.active_exec
                if capture is None:
                    break

                if capture.failed:
                    result_reason = capture.failure_reason or "serial disconnected"
                    break

                if capture.seen_output and ((time.monotonic() - capture.last_activity_ts) >= self.EXEC_IDLE_SEC):
                    result_ok = True
                    result_reason = ""
                    break

        with self.state_lock:
            capture = self.active_exec
            self.active_exec = None

        self._release_control(client.client_id)
        line_count = len(capture.lines) if capture is not None else 0

        if result_ok:
            client.send({"type": "exec_done", "ok": True, "line_count": line_count})
        else:
            client.send({"type": "exec_done", "ok": False, "reason": result_reason, "line_count": line_count})

    def _handle_interactive_message(self, client, message):
        msg_type = message.get("type", "")
        if msg_type == "line":
            line = str(message.get("text", "")).strip()
            if not line:
                return

            with self.state_lock:
                if (not self.serial_connected) or (not self.shell_ready):
                    client.send({"type": "error", "text": "shell not ready"})
                    return

                if self.active_exec is not None or self.interactive_lock_active:
                    owner = self.clients.get(self.controller_id)
                    owner_name = owner.name if owner is not None else "unknown"
                    client.send({"type": "busy", "controller_name": owner_name})
                    return

                self.controller_id = client.client_id
                self.interactive_lock_active = True
                self.interactive_last_activity_ts = time.monotonic()

            self._broadcast_state()

            if not self._send_command_to_serial(line):
                with self.state_lock:
                    if self.controller_id == client.client_id:
                        self.controller_id = None
                    self.interactive_lock_active = False
                    self.interactive_last_activity_ts = 0.0
                self._broadcast_state()
                client.send({"type": "error", "text": "send failed"})
                return

            client.send({"type": "accepted"})
            return

        if msg_type == "detach":
            client.send({"type": "info", "text": "detaching"})
            raise ConnectionAbortedError("client requested detach")

        if msg_type == "ping":
            client.send({"type": "pong"})
            return

        client.send({"type": "error", "text": f"unsupported message: {msg_type}"})

    def _client_thread(self, client):
        try:
            file_obj = client.sock.makefile("rb")
            raw = file_obj.readline()
            if not raw:
                return

            hello = json.loads(raw.decode("utf-8"))
            if hello.get("type") != "hello":
                client.send({"type": "error", "text": "expected hello"})
                return

            client.name = str(hello.get("name") or client.name)
            client.mode = str(hello.get("mode") or "monitor")
            client.send({"type": "hello", "client_id": client.client_id, "mode": client.mode})
            client.send(self._state_message())

            if client.mode == "interactive":
                client.send({"type": "grant"})

            elif client.mode == "exec":
                command = str(hello.get("command") or "")
                timeout_sec = float(hello.get("timeout_sec") or 5.0)
                self._run_exec(client, command, timeout_sec)
                return

            elif client.mode == "status":
                return

            elif client.mode != "monitor":
                client.send({"type": "error", "text": f"unsupported mode: {client.mode}"})
                return

            while self.running and client.alive:
                raw = file_obj.readline()
                if not raw:
                    break
                message = json.loads(raw.decode("utf-8"))
                if client.mode == "interactive":
                    self._handle_interactive_message(client, message)
                else:
                    if message.get("type") == "ping":
                        client.send({"type": "pong"})
                    else:
                        client.send({"type": "error", "text": "monitor is read-only"})
        except (ConnectionAbortedError, ConnectionResetError, OSError):
            pass
        except json.JSONDecodeError:
            client.send({"type": "error", "text": "invalid json"})
        except Exception as exc:
            client.send({"type": "error", "text": str(exc)})
        finally:
            self._drop_client(client.client_id)

    def serve_forever(self):
        serial_thread = threading.Thread(target=self._serial_loop, daemon=True)
        serial_thread.start()

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_sock:
            server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server_sock.bind((self.listen_host, self.listen_port))
            server_sock.listen()
            server_sock.settimeout(0.5)
            self.server_sock = server_sock
            self._log(f"listening on {self.listen_host}:{self.listen_port}")

            try:
                while self.running:
                    try:
                        client_sock, addr = server_sock.accept()
                    except socket.timeout:
                        continue
                    except OSError:
                        if not self.running:
                            break
                        raise
                    client = self._register_client(client_sock, addr)
                    if self.verbose:
                        self._log(f"client connected: {client.client_id} from {addr}")
                    thread = threading.Thread(target=self._client_thread, args=(client,), daemon=True)
                    thread.start()
            except KeyboardInterrupt:
                self.stop("stopping")
            finally:
                self.stop("stopping")


def parse_args():
    parser = argparse.ArgumentParser(description="BRICOS shell serial bridge daemon")
    parser.add_argument("--serial-port", default="/dev/bot_logbridge", help="serial device path")
    parser.add_argument("--baud", type=int, default=1500000, help="serial baud rate")
    parser.add_argument("--listen-host", default="0.0.0.0", help="bridge listen host")
    parser.add_argument("--listen-port", type=int, default=8765, help="bridge listen port")
    parser.add_argument("--verbose", action="store_true", help="print daemon diagnostics")
    return parser.parse_args()


def main():
    args = parse_args()
    daemon = ShellBridgeDaemon(
        serial_port=args.serial_port,
        baud=args.baud,
        listen_host=args.listen_host,
        listen_port=args.listen_port,
        verbose=args.verbose,
    )

    def _handle_signal(signum, _frame):
        daemon.stop(f"stopping on signal {signum}")

    signal.signal(signal.SIGTERM, _handle_signal)
    signal.signal(signal.SIGINT, _handle_signal)
    daemon.serve_forever()


if __name__ == "__main__":
    main()
