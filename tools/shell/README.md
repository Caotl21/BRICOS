# Shell Bridge

This folder now contains two host-side entry points for sharing the UART5 shell
without changing firmware:

- `bot_shelld.py`: owns the serial device and exposes a localhost bridge service
- `bot_shellctl.py`: attaches to that bridge for interactive shell, log watch, or one-shot commands

Typical usage:

```bash
python tools/shell/bot_shelld.py --serial-port /dev/bot_logbridge --baud 1500000
```

Open an interactive shell from one terminal:

```bash
python tools/shell/bot_shellctl.py attach
```

Watch logs from another terminal:

```bash
python tools/shell/bot_shellctl.py watch
```

Run one command from a script:

```bash
python tools/shell/bot_shellctl.py exec "fault"
```

Current behavior:

- multiple clients can `attach` or `watch` at the same time
- `attach` clients all receive the same logs and shell output
- when any attached client sends a command, the bridge holds a short server-side lock until output goes idle
- `exec` is serialized through the same control lock
- command responses are collected with an idle-time heuristic because the device shell is a raw UART text stream

Client identity:

- by default `bot_shellctl.py` identifies itself as `hostname:pid:tty`
- this makes `BUSY` messages easier to map back to a specific SSH session
- you can still override it manually with `--name`
- `attach` reuses the BRICOS-style welcome banner and no longer exits during a normal device reboot gap

LAN usage:

- yes, the bridge can run on the RDKX5 while clients run on different PCs in the same subnet
- start the daemon with `--listen-host 0.0.0.0` on the RDKX5
- then connect from another PC with `python bot_shellctl.py --host <rdkx5-ip> attach`
- there is currently no authentication, so only expose it on a trusted subnet or behind firewall rules

Systemd service:

- yes, `bot_shelld.py` can be registered as a long-running `systemd` service, similar to `sshd`
- an example unit file is provided at `tools/shell/systemd/bot_shelld.service`
- before installing it, edit `User`, `Group`, `WorkingDirectory`, and `ExecStart` to match the actual RDKX5 path
- make sure the service user can open `/dev/bot_logbridge`; on Ubuntu-like systems that usually means membership in `dialout`

Typical install steps on the RDKX5:

```bash
sudo cp tools/shell/systemd/bot_shelld.service /etc/systemd/system/bot_shelld.service
sudo systemctl daemon-reload
sudo systemctl enable --now bot_shelld
sudo systemctl status bot_shelld
```

Useful service commands:

```bash
sudo systemctl restart bot_shelld
sudo systemctl stop bot_shelld
sudo journalctl -u bot_shelld -f
```

The daemon now handles `SIGTERM` cleanly, so `systemctl stop bot_shelld` will shut it down more like a normal service process instead of killing it abruptly.
