from __future__ import annotations

import xml.etree.ElementTree as ET
from pathlib import Path


def _split_csv(text: str | None) -> list[str]:
    if not text:
        return []
    return [item.strip() for item in text.split(",") if item.strip()]


def _resolve_project_path(project_dir: Path, raw_path: str) -> Path:
    normalized = raw_path.replace("\\", "/")
    return (project_dir / normalized).resolve()


def _to_repo_relative(root_dir: Path, file_path: Path) -> str:
    return file_path.relative_to(root_dir).as_posix()


def load_keil_project(project_file: Path, target_name: str = "Target 1") -> dict[str, object]:
    project_file = project_file.resolve()
    project_dir = project_file.parent
    root_dir = project_dir.parent

    tree = ET.parse(project_file)
    root = tree.getroot()

    target_node = None
    for candidate in root.findall("./Targets/Target"):
        if candidate.findtext("./TargetName") == target_name:
            target_node = candidate
            break

    if target_node is None:
        raise RuntimeError(f"Target '{target_name}' not found in {project_file}.")

    defines = _split_csv(target_node.findtext("./TargetOption/TargetArmAds/Cads/VariousControls/Define"))
    include_items = []
    include_text = target_node.findtext("./TargetOption/TargetArmAds/Cads/VariousControls/IncludePath") or ""
    for raw_include in include_text.split(";"):
        raw_include = raw_include.strip()
        if not raw_include:
            continue

        resolved = _resolve_project_path(project_dir, raw_include)

        if resolved.as_posix().endswith("/FreeRTOS/portable/RVDS/ARM_CM4F"):
            resolved = (root_dir / "FreeRTOS" / "portable" / "GCC" / "ARM_CM4F").resolve()

        if resolved.exists():
            rel_include = _to_repo_relative(root_dir, resolved)
            if rel_include not in include_items:
                include_items.append(rel_include)

    sources = []
    for file_node in target_node.iterfind(".//FilePath"):
        raw_source = (file_node.text or "").strip()
        if not raw_source:
            continue

        resolved = _resolve_project_path(project_dir, raw_source)
        suffix = resolved.suffix.lower()
        if suffix not in {".c", ".s", ".S"}:
            continue

        rel_source = _to_repo_relative(root_dir, resolved)

        if rel_source == "Libraries/CMSIS/Device/ST/STM32F4xx/Source/Templates/arm/startup_stm32f40_41xxx.s":
            rel_source = "tools/scons/startup/startup_stm32f40_41xxx_gcc.s"
        elif rel_source == "FreeRTOS/portable/RVDS/ARM_CM4F/port.c":
            rel_source = "FreeRTOS/portable/GCC/ARM_CM4F/port.c"

        if rel_source not in sources:
            sources.append(rel_source)

    return {
        "defines": defines,
        "includes": include_items,
        "sources": sources,
    }
