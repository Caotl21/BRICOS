#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_HOSTS_FILE = SCRIPT_DIR / "reboot_hosts.yaml"
DEFAULT_REMOTE_DIR = "~/BRICOS_REBOOT/test"
DEFAULT_RESET_GPIOCHIP = "/dev/gpiochip1"
DEFAULT_RESET_LINE = 27
DEFAULT_REMOTE_PYTHON = "/usr/bin/python3"


def _parse_scalar(value: str) -> object:
    low = value.lower()
    if low in ("true", "yes", "on"):
        return True
    if low in ("false", "no", "off"):
        return False
    if value.isdigit():
        try:
            return int(value)
        except ValueError:
            pass
    return value


def _load_simple_yaml_hosts(path: Path) -> dict[str, dict[str, object]]:
    hosts: dict[str, dict[str, object]] = {}
    current_section = ""
    current_host = ""

    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue

        indent = len(line) - len(line.lstrip(" "))
        stripped = line.strip()

        if stripped.endswith(":") and (":" not in stripped[:-1]):
            if indent == 0:
                current_section = stripped[:-1].strip()
                current_host = ""
            elif indent == 2 and current_section in ("hosts", "users"):
                current_host = stripped[:-1].strip()
                hosts.setdefault(current_host, {})
            continue

        if ":" not in stripped:
            continue

        key, value = stripped.split(":", 1)
        key = key.strip()
        value = value.strip().strip('"').strip("'")

        if indent == 0:
            if key in ("hosts", "users"):
                current_section = key
                current_host = ""
                continue
            if value:
                hosts[key] = {"host": _parse_scalar(value)}
        elif current_section in ("hosts", "users"):
            if indent == 2:
                if value:
                    hosts[key] = {"host": _parse_scalar(value)}
                    current_host = ""
                else:
                    current_host = key
                    hosts.setdefault(current_host, {})
            elif indent >= 4 and current_host and value:
                hosts.setdefault(current_host, {})[key] = _parse_scalar(value)

    return hosts


def _normalize_host_entry(name: str, raw: object) -> dict[str, object]:
    if isinstance(raw, str):
        return {"host": raw}
    if not isinstance(raw, dict):
        raise ValueError(f"Host entry '{name}' must be a mapping or string")

    data = {str(k): v for k, v in raw.items()}
    host = data.get("host", data.get("ip", data.get("addr", data.get("hostname"))))
    if host is None:
        raise ValueError(f"Host entry '{name}' is missing 'host' or 'ip'")
    data["host"] = str(host)

    if "reset_line" in data:
        data["reset_line"] = int(data["reset_line"])

    return data


def load_hosts(path: Path) -> dict[str, dict[str, object]]:
    if not path.is_file():
        raise FileNotFoundError(f"Hosts yaml not found: {path}")

    try:
        import yaml  # type: ignore

        data = yaml.safe_load(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError("YAML root must be a mapping")

        if "hosts" in data and isinstance(data["hosts"], dict):
            raw_hosts = data["hosts"]
        elif "users" in data and isinstance(data["users"], dict):
            raw_hosts = data["users"]
        else:
            raw_hosts = data

        return {
            str(k): _normalize_host_entry(str(k), v)
            for k, v in raw_hosts.items()
        }
    except ImportError:
        raw_hosts = _load_simple_yaml_hosts(path)
        return {
            str(k): _normalize_host_entry(str(k), v)
            for k, v in raw_hosts.items()
        }


def run_cmd(cmd: list[str]) -> None:
    print("+", " ".join(shlex.quote(x) for x in cmd))
    subprocess.run(cmd, check=True)


def resolve_tool(name: str, override: str | None = None) -> str:
    if override:
        raw = os.path.expanduser(override)
        p = Path(raw)
        if p.is_file():
            return str(p)

        if os.name == "nt":
            low = raw.lower()
            mark = "\\system32\\"
            idx = low.find(mark)
            if idx >= 0:
                alt = raw[:idx] + "\\Sysnative\\" + raw[idx + len(mark):]
                alt_p = Path(alt)
                if alt_p.is_file():
                    return str(alt_p)

        raise RuntimeError(f"{name} override path not found: {p}")

    hit = shutil.which(name) or shutil.which(f"{name}.exe")
    if hit:
        return hit

    candidates: list[Path] = []
    if os.name == "nt":
        windir = os.environ.get("WINDIR", r"C:\Windows")
        candidates.extend(
            [
                Path(windir) / "System32" / "OpenSSH" / f"{name}.exe",
                Path(windir) / "Sysnative" / "OpenSSH" / f"{name}.exe",
                Path(r"C:\Program Files\Git\usr\bin") / f"{name}.exe",
                Path(r"C:\Program Files\Git\bin") / f"{name}.exe",
            ]
        )

    for p in candidates:
        if p.is_file():
            return str(p)

    if os.name == "nt":
        try:
            out = subprocess.check_output(
                ["where", f"{name}.exe"],
                stderr=subprocess.DEVNULL,
                text=True,
                encoding="utf-8",
            )
            for line in out.splitlines():
                line = line.strip()
                if line and Path(line).is_file():
                    return line
        except Exception:
            pass

    raise RuntimeError(
        f"Required command not found: {name}. "
        f"Install OpenSSH client or pass --{name}-bin <path>."
    )


def resolve_host_option(
    cli_value: object,
    host_cfg: dict[str, object],
    key: str,
    default: object,
) -> object:
    if cli_value is not None:
        return cli_value
    if key in host_cfg and host_cfg[key] is not None:
        return host_cfg[key]
    return default


def resolve_reboot_path() -> Path:
    path = SCRIPT_DIR / "reboot.py"
    if not path.is_file():
        raise FileNotFoundError(f"reboot.py not found: {path}")
    return path.resolve()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Upload reboot.py to the RDK and pulse the reset GPIO remotely."
    )
    parser.add_argument("user", help="Target username key in yaml, for example: bricsbot2")
    parser.add_argument(
        "--hosts-file",
        default=str(DEFAULT_HOSTS_FILE),
        help=f"YAML file mapping user to reboot settings. Default: {DEFAULT_HOSTS_FILE}",
    )
    parser.add_argument(
        "--remote-dir",
        help=f"Remote directory to upload and execute from. Default: {DEFAULT_REMOTE_DIR}",
    )
    parser.add_argument("--remote-python", help=f"Remote Python executable. Default: {DEFAULT_REMOTE_PYTHON}")
    parser.add_argument("--reset-gpiochip", help=f"Remote GPIO chip path. Default: {DEFAULT_RESET_GPIOCHIP}")
    parser.add_argument("--reset-line", type=int, help=f"Remote GPIO line offset. Default: {DEFAULT_RESET_LINE}")
    parser.add_argument("--reset-hold-ms", type=float, help="Reset pulse width in milliseconds.")
    parser.add_argument("--settle-ms", type=float, help="Delay after releasing reset in milliseconds.")
    parser.add_argument(
        "--reset-active-low",
        dest="reset_active_low",
        action="store_true",
        default=None,
        help="Drive reset line active-low.",
    )
    parser.add_argument(
        "--reset-active-high",
        dest="reset_active_low",
        action="store_false",
        help="Drive reset line active-high.",
    )
    parser.add_argument("--ssh-bin", help="Optional absolute path to ssh executable")
    parser.add_argument("--scp-bin", help="Optional absolute path to scp executable")
    parser.add_argument(
        "--ssh-key",
        default="~/.ssh/id_ed25519_bricsbot_ota",
        help="Private key path for SSH/SCP. Default: ~/.ssh/id_ed25519_bricsbot_ota",
    )
    parser.add_argument(
        "--use-ssh-key",
        dest="use_ssh_key",
        action="store_true",
        default=True,
        help="Use private key authentication (default: enabled).",
    )
    parser.add_argument(
        "--no-ssh-key",
        dest="use_ssh_key",
        action="store_false",
        help="Disable private key authentication and use password login.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    hosts = load_hosts(Path(args.hosts_file).resolve())
    host_cfg = hosts.get(args.user)
    if not host_cfg:
        print(f"User '{args.user}' not found in hosts file: {args.hosts_file}", file=sys.stderr)
        return 1

    ip = str(host_cfg["host"])
    remote_dir = str(resolve_host_option(args.remote_dir, host_cfg, "remote_dir", DEFAULT_REMOTE_DIR)).rstrip("/")
    remote_python = str(resolve_host_option(args.remote_python, host_cfg, "remote_python", DEFAULT_REMOTE_PYTHON))
    reset_gpiochip = str(resolve_host_option(args.reset_gpiochip, host_cfg, "reset_gpiochip", DEFAULT_RESET_GPIOCHIP))
    reset_line = int(resolve_host_option(args.reset_line, host_cfg, "reset_line", DEFAULT_RESET_LINE))
    reset_hold_ms = float(resolve_host_option(args.reset_hold_ms, host_cfg, "reset_hold_ms", 80.0))
    settle_ms = float(resolve_host_option(args.settle_ms, host_cfg, "settle_ms", 50.0))
    reset_active_low = bool(resolve_host_option(args.reset_active_low, host_cfg, "reset_active_low", True))

    local_reboot = resolve_reboot_path()
    ssh_bin = resolve_tool("ssh", args.ssh_bin)
    scp_bin = resolve_tool("scp", args.scp_bin)

    key_path = Path(os.path.expanduser(args.ssh_key))
    if args.use_ssh_key and (not key_path.is_file()):
        raise RuntimeError(f"SSH private key not found: {key_path}")

    target = f"{args.user}@{ip}"
    remote_reboot = f"{target}:{remote_dir}/reboot.py"

    print(f"User: {args.user}")
    print(f"IP: {ip}")
    print(f"Remote Python: {remote_python}")
    print(f"Reset GPIO: {reset_gpiochip} line {reset_line}")
    print(f"Reset Mode: {'active-low' if reset_active_low else 'active-high'}, hold={reset_hold_ms:.1f} ms, settle={settle_ms:.1f} ms")
    print(f"ssh: {ssh_bin}")
    print(f"scp: {scp_bin}")

    auth_opts: list[str] = []
    if args.use_ssh_key:
        print(f"ssh key: {key_path}")
        auth_opts = [
            "-i",
            str(key_path),
            "-o",
            "IdentitiesOnly=yes",
        ]
    else:
        print("ssh key: disabled")

    run_cmd([ssh_bin, *auth_opts, target, f"mkdir -p {remote_dir}"])
    run_cmd([scp_bin, *auth_opts, str(local_reboot), remote_reboot])

    remote_cmd = (
        f"cd {remote_dir} && "
        f"{remote_python} reboot.py "
        f"--gpiochip {shlex.quote(reset_gpiochip)} "
        f"--line {reset_line} "
        f"--hold-ms {reset_hold_ms} "
        f"--settle-ms {settle_ms} "
        f"{'--active-low' if reset_active_low else '--active-high'}"
    )
    run_cmd([ssh_bin, *auth_opts, target, remote_cmd])

    print("Remote reboot completed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
