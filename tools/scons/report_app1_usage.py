from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


SECTION_RE = re.compile(
    r"^\s*\d+\s+"
    r"(?P<name>\S+)\s+"
    r"(?P<size>[0-9a-fA-F]+)\s+"
    r"(?P<vma>[0-9a-fA-F]+)\s+"
    r"(?P<lma>[0-9a-fA-F]+)\s+"
    r"(?P<offset>[0-9a-fA-F]+)\s+"
    r"2\*\*(?P<align>\d+)"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Report APP1 FLASH/RAM usage from an ELF file.")
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--elf", required=True)
    parser.add_argument("--flash-origin", required=True, type=lambda x: int(x, 0))
    parser.add_argument("--flash-limit", required=True, type=lambda x: int(x, 0))
    parser.add_argument("--ram-origin", required=True, type=lambda x: int(x, 0))
    parser.add_argument("--ram-limit", required=True, type=lambda x: int(x, 0))
    parser.add_argument("--report", required=True)
    parser.add_argument("--env", required=True)
    return parser.parse_args()


def overlaps(region_origin: int, region_limit: int, base: int, size: int) -> bool:
    if size <= 0:
        return False
    region_end = region_origin + region_limit
    span_end = base + size
    return not (span_end <= region_origin or base >= region_end)


def collect_sections(objdump: str, elf_path: str) -> list[dict[str, int | str]]:
    result = subprocess.run(
        [objdump, "-h", elf_path],
        check=True,
        capture_output=True,
        text=True,
    )
    sections: list[dict[str, int | str]] = []
    current: dict[str, int | str] | None = None
    for line in result.stdout.splitlines():
        match = SECTION_RE.match(line)
        if match:
            if current is not None:
                sections.append(current)
            size = int(match.group("size"), 16)
            current = None
            if size == 0:
                continue
            current = {
                "name": match.group("name"),
                "size": size,
                "vma": int(match.group("vma"), 16),
                "lma": int(match.group("lma"), 16),
                "flags": "",
            }
            continue

        if current is None:
            continue

        flags = line.strip()
        if flags:
            current["flags"] = flags
            sections.append(current)
            current = None

    if current is not None:
        sections.append(current)
    return sections


def summarize_usage(
    sections: list[dict[str, int | str]],
    flash_origin: int,
    flash_limit: int,
    ram_origin: int,
    ram_limit: int,
) -> tuple[int, int]:
    flash_used = 0
    ram_used = 0
    for section in sections:
        size = int(section["size"])
        vma = int(section["vma"])
        lma = int(section["lma"])
        flags = {flag.strip() for flag in str(section["flags"]).split(",")}
        is_alloc = "ALLOC" in flags
        has_file_contents = "CONTENTS" in flags

        if not is_alloc:
            continue

        if overlaps(flash_origin, flash_limit, vma, size) or (
            has_file_contents and overlaps(flash_origin, flash_limit, lma, size)
        ):
            flash_used += size
        if overlaps(ram_origin, ram_limit, vma, size):
            ram_used += size
    return flash_used, ram_used


def write_outputs(
    report_path: Path,
    env_path: Path,
    flash_used: int,
    flash_limit: int,
    ram_used: int,
    ram_limit: int,
) -> None:
    flash_remaining = flash_limit - flash_used
    ram_remaining = ram_limit - ram_used
    flash_pct = (flash_used * 100.0) / flash_limit
    ram_pct = (ram_used * 100.0) / ram_limit

    report_text = "\n".join(
        [
            f"APP1 FLASH usage: {flash_used} / {flash_limit} bytes ({flash_pct:.2f}%)",
            f"APP1 FLASH remaining: {flash_remaining} bytes",
            f"APP1 RAM usage: {ram_used} / {ram_limit} bytes ({ram_pct:.2f}%)",
            f"APP1 RAM remaining: {ram_remaining} bytes",
        ]
    ) + "\n"
    report_path.write_text(report_text, encoding="utf-8")

    env_text = "\n".join(
        [
            f"APP1_FLASH_USED_BYTES={flash_used}",
            f"APP1_FLASH_LIMIT_BYTES={flash_limit}",
            f"APP1_FLASH_REMAINING_BYTES={flash_remaining}",
            f"APP1_FLASH_USED_PERCENT={flash_pct:.2f}",
            f"APP1_RAM_USED_BYTES={ram_used}",
            f"APP1_RAM_LIMIT_BYTES={ram_limit}",
            f"APP1_RAM_REMAINING_BYTES={ram_remaining}",
            f"APP1_RAM_USED_PERCENT={ram_pct:.2f}",
        ]
    ) + "\n"
    env_path.write_text(env_text, encoding="utf-8")

    print(report_text, end="")


def main() -> int:
    args = parse_args()
    sections = collect_sections(args.objdump, args.elf)
    flash_used, ram_used = summarize_usage(
        sections,
        args.flash_origin,
        args.flash_limit,
        args.ram_origin,
        args.ram_limit,
    )
    write_outputs(
        Path(args.report),
        Path(args.env),
        flash_used,
        args.flash_limit,
        ram_used,
        args.ram_limit,
    )

    if flash_used > args.flash_limit:
        print(
            f"ERROR: APP1 FLASH overflow: {flash_used} > {args.flash_limit} bytes",
            file=sys.stderr,
        )
        return 1

    if ram_used > args.ram_limit:
        print(
            f"ERROR: APP1 RAM overflow: {ram_used} > {args.ram_limit} bytes",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
