#!/usr/bin/env python3
import argparse
import os
import select
import sys
import termios
import threading
import tty

import serial

START1 = 0xAA
START2 = 0xBB
END1 = 0xCC
END2 = 0xDD

CMD_SHELL_REQ = 0x20
CMD_SHELL_RESP = 0x21
CMD_ACK = 0xFF

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
    COMMANDS = ["help", "echo", "sysmode", "momode", "fault", "euler", "depthtemp", "power", "cabin", "chip"]

    def __init__(self, port: str, baud: int):
        self.ser = serial.Serial(port=port, baudrate=baud, timeout=0.05)
        self.parser = HydroParser()
        self.running = True
        self.line = ""
        self.lock = threading.Lock()
        self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)

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
        payload = line.encode("utf-8", errors="ignore")
        frame = self._build_frame(CMD_SHELL_REQ, payload)
        self.ser.write(frame)
        self.ser.flush()

    def _print_async(self, text: str):
        with self.lock:
            sys.stdout.write("\r\n" + text + "\r\n")
            self._redraw_input_unlocked()

    def _redraw_input_unlocked(self):
        sys.stdout.write("\r\033[2K> " + self.line)
        sys.stdout.flush()

    def _handle_frame(self, cmd: int, payload: bytes):
        if cmd == CMD_SHELL_RESP:
            txt = payload.decode("utf-8", errors="replace").rstrip("\r\n")
            self._print_async(txt)
            return

        if cmd == CMD_ACK and len(payload) >= 4 and payload[0] == CMD_ACK:
            ack_cmd = payload[1]
            ack_code = payload[2]
            seq = payload[3]
            desc = ACK_MAP.get(ack_code, f"0x{ack_code:02X}")
            self._print_async(
                f"[ACK] cmd=0x{ack_cmd:02X} code={desc} seq={seq}"
            )
            return

        self._print_async(f"[FRAME] cmd=0x{cmd:02X} len={len(payload)}")

    def _rx_loop(self):
        while self.running:
            try:
                data = self.ser.read(256)
            except serial.SerialException as exc:
                self._print_async(f"[ERROR] serial read failed: {exc}")
                self.running = False
                break
            if not data:
                continue
            for cmd, payload in self.parser.feed(data):
                self._handle_frame(cmd, payload)

    def _complete_first_token(self):
        if " " in self.line:
            sys.stdout.write("\a")
            sys.stdout.flush()
            return

        prefix = self.line
        hits = [c for c in self.COMMANDS if c.startswith(prefix)]
        if len(hits) == 1:
            self.line = hits[0]
            self._redraw_input_unlocked()
            return
        if len(hits) > 1:
            sys.stdout.write("\r\n" + "  ".join(hits))
            sys.stdout.write("\r\n")
            self._redraw_input_unlocked()
            return
        sys.stdout.write("\a")
        sys.stdout.flush()

    def run(self):
        self.rx_thread.start()
        print("Interactive shell over NRT frame mode")
        print("Ctrl-C to quit")
        print("Note: firmware-side Tab completion only works in UART stream mode.")
        print("This client provides local first-token completion for Tab.")

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
                    if c in (13, 10):
                        line = self.line.strip()
                        sys.stdout.write("\r\n")
                        sys.stdout.flush()
                        self.line = ""
                        self._redraw_input_unlocked()
                        if line:
                            self.send_shell_line(line)
                        continue

                    if c in (8, 127):
                        if self.line:
                            self.line = self.line[:-1]
                            self._redraw_input_unlocked()
                        else:
                            sys.stdout.write("\a")
                            sys.stdout.flush()
                        continue

                    if c == 9:
                        self._complete_first_token()
                        continue

                    if 32 <= c <= 126:
                        self.line += chr(c)
                        sys.stdout.write(chr(c))
                        sys.stdout.flush()
                        continue

                    sys.stdout.write("\a")
                    sys.stdout.flush()

        self.running = False
        self.rx_thread.join(timeout=0.2)
        self.ser.close()
        print("\nbye")


def parse_args():
    p = argparse.ArgumentParser(description="NRT shell client")
    p.add_argument("--port", default="/dev/ttyUSB0", help="serial port")
    p.add_argument("--baud", type=int, default=921600, help="baud rate")
    return p.parse_args()


def main():
    args = parse_args()
    client = ShellClient(args.port, args.baud)
    client.run()


if __name__ == "__main__":
    main()
