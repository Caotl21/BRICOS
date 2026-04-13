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
ROOT_DIR = SCRIPT_DIR.parent.parent
DEFAULT_BUILD_DIR = ROOT_DIR / "Build"
DEFAULT_HOSTS_FILE = SCRIPT_DIR / "ota_hosts.yaml"
DEFAULT_REMOTE_DIR = "~/BRICOS_OTA/test"


def _load_simple_yaml_hosts(path: Path) -> dict[str, str]:
    hosts: dict[str, str] = {}
    current_section = ""

    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue

        indent = len(line) - len(line.lstrip(" "))
        stripped = line.strip()

        if stripped.endswith(":") and (":" not in stripped[:-1]):
            current_section = stripped[:-1].strip()
            continue

        if ":" not in stripped:
            continue

        key, value = stripped.split(":", 1)
        key = key.strip()
        value = value.strip().strip('"').strip("'")
        if not value:
            continue

        if indent == 0:
            if key in ("hosts", "users"):
                current_section = key
                continue
            hosts[key] = value
        elif current_section in ("hosts", "users"):
            hosts[key] = value

    return hosts


def load_hosts(path: Path) -> dict[str, str]:
    if not path.is_file():
        raise FileNotFoundError(f"Hosts yaml not found: {path}")

    try:
        import yaml  # type: ignore

        data = yaml.safe_load(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError("YAML root must be a mapping")

        if "hosts" in data and isinstance(data["hosts"], dict):
            return {str(k): str(v) for k, v in data["hosts"].items()}
        if "users" in data and isinstance(data["users"], dict):
            return {str(k): str(v) for k, v in data["users"].items()}
        return {str(k): str(v) for k, v in data.items()}
    except ImportError:
        return _load_simple_yaml_hosts(path)


def find_latest_bin(build_dir: Path) -> Path:
    if not build_dir.is_dir():
        raise FileNotFoundError(f"Build directory not found: {build_dir}")

    candidates = [p for p in build_dir.rglob("*.bin") if p.is_file()]
    if not candidates:
        raise FileNotFoundError(f"No .bin found under: {build_dir}")

    return max(candidates, key=lambda p: p.stat().st_mtime)


def run_cmd(cmd: list[str]) -> None:
    print("+", " ".join(shlex.quote(x) for x in cmd))
    subprocess.run(cmd, check=True)


def resolve_tool(name: str, override: str | None = None) -> str:
    if override:
        raw = os.path.expanduser(override)
        p = Path(raw)
        if p.is_file():
            return str(p)

        # 32-bit Python on Windows can redirect System32 -> SysWOW64.
        # If user passes System32 explicitly, try Sysnative fallback.
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

    # Final fallback: ask Windows command resolver.
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


def resolve_ota_send_path() -> Path:
    candidates = [
        SCRIPT_DIR / "ota_send.py",
        SCRIPT_DIR.parent / "ota_send.py",
    ]
    for p in candidates:
        if p.is_file():
            return p.resolve()
    raise FileNotFoundError(
        f"ota_send.py not found. Checked: {', '.join(str(x) for x in candidates)}"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Upload latest firmware to OrangePi and run ota_send.py remotely."
    )
    parser.add_argument("user", help="Target username key in yaml, for example: bricsbot1")
    parser.add_argument(
        "--hosts-file",
        default=str(DEFAULT_HOSTS_FILE),
        help=f"YAML file mapping user to IP. Default: {DEFAULT_HOSTS_FILE}",
    )
    parser.add_argument(
        "--build-dir",
        default=str(DEFAULT_BUILD_DIR),
        help=f"Build directory to scan for latest .bin. Default: {DEFAULT_BUILD_DIR}",
    )
    parser.add_argument(
        "--remote-dir",
        default=DEFAULT_REMOTE_DIR,
        help=f"Remote directory to upload and execute from. Default: {DEFAULT_REMOTE_DIR}",
    )
    parser.add_argument("--cmd-port", default="/dev/ttyS7")
    parser.add_argument("--data-port", default="/dev/ttyS6")
    parser.add_argument("--reset-gpiochip", default="/dev/gpiochip1")
    parser.add_argument("--reset-line", type=int, default=27)
    parser.add_argument(
        "--remote-python",
        default="~/miniconda3/envs/OTA_Test/bin/python",
        help=(
            "Python executable on OrangePi. "
            "Example: ~/miniconda3/envs/OTA_Test/bin/python"
        ),
    )
    parser.add_argument("--ssh-bin", help="Optional absolute path to ssh executable")
    parser.add_argument("--scp-bin", help="Optional absolute path to scp executable")
    parser.add_argument(
        "--ssh-key",
        default="~/.ssh/id_ed25519_bricsbot_ota",
        help=(
            "Private key path for SSH/SCP. "
            "Default: ~/.ssh/id_ed25519_bricsbot_ota"
        ),
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
    ip = hosts.get(args.user)
    if not ip:
        print(f"User '{args.user}' not found in hosts file: {args.hosts_file}", file=sys.stderr)
        return 1

    local_bin = find_latest_bin(Path(args.build_dir).resolve())
    local_ota_send = resolve_ota_send_path()

    ssh_bin = resolve_tool("ssh", args.ssh_bin)
    scp_bin = resolve_tool("scp", args.scp_bin)
    key_path = Path(os.path.expanduser(args.ssh_key))
    if args.use_ssh_key and (not key_path.is_file()):
        raise RuntimeError(f"SSH private key not found: {key_path}")

    target = f"{args.user}@{ip}"
    remote_bin_name = local_bin.name
    remote_dir = args.remote_dir.rstrip("/")
    remote_ota_send = f"{target}:{remote_dir}/ota_send.py"
    remote_bin = f"{target}:{remote_dir}/{remote_bin_name}"

    print(f"User: {args.user}")
    print(f"IP: {ip}")
    print(f"Latest BIN: {local_bin}")

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
    run_cmd([scp_bin, *auth_opts, str(local_ota_send), remote_ota_send])
    run_cmd([scp_bin, *auth_opts, str(local_bin), remote_bin])

    remote_cmd = (
        f"cd {remote_dir} && "
        f"{args.remote_python} ota_send.py "
        f"--mode hw-reset "
        f"--cmd-port {shlex.quote(args.cmd_port)} "
        f"--data-port {shlex.quote(args.data_port)} "
        f"--file {shlex.quote(remote_bin_name)} "
        f"--reset-gpiochip {shlex.quote(args.reset_gpiochip)} "
        f"--reset-line {args.reset_line}"
    )
    run_cmd([ssh_bin, *auth_opts, target, remote_cmd])

    print("OTA completed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
