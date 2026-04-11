import datetime
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

# Config
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(SCRIPT_DIR)
KEIL_EXE = r"D:\Keil_v5\UV4\UV4.exe"
PROJECT_PATH = os.path.join(ROOT_DIR, "Project", "project.uvprojx")
TARGET_NAME = "Target 1"
LOG_DIR = os.path.join(ROOT_DIR, "Build")
OBJECT_DIR = os.path.join(ROOT_DIR, "Project", "Objects")


def modify_keil_macros(xml_path, new_defines):
    """Update all <Define> nodes in a .uvprojx file."""
    tree = ET.parse(xml_path)
    root = tree.getroot()

    found = False
    for define_node in root.findall(".//Define"):
        define_node.text = new_defines
        found = True

    if found:
        tree.write(xml_path, encoding="utf-8", xml_declaration=True)
        print(f"Updated macro defines: {new_defines}")
    else:
        print("No <Define> node found in project file.")


def get_output_name(xml_path):
    """Read <OutputName> from project file, fallback to 'app'."""
    try:
        tree = ET.parse(xml_path)
        root = tree.getroot()
        output_name = root.findtext(".//OutputName")
        return output_name.strip() if output_name else "app"
    except Exception:
        return "app"


def build_keil_project():
    """Run Keil build and return (success, log_file, timestamp, output_name)."""
    if not os.path.exists(KEIL_EXE):
        print(f"ERROR: UV4.exe not found: {KEIL_EXE}")
        return False, None, None, None

    if not os.path.exists(PROJECT_PATH):
        print(f"ERROR: project file not found: {PROJECT_PATH}")
        return False, None, None, None

    os.makedirs(LOG_DIR, exist_ok=True)

    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = os.path.join(LOG_DIR, f"build_log_{timestamp}.txt")
    output_name = get_output_name(PROJECT_PATH)

    cmd = [KEIL_EXE, "-r", PROJECT_PATH, "-t", TARGET_NAME, "-o", log_file]
    print(f"Building target '{TARGET_NAME}' ...")

    try:
        result = subprocess.run(cmd, check=False)
    except Exception as exc:
        print(f"ERROR: failed to execute Keil: {exc}")
        return False, log_file, timestamp, output_name

    # Keil return code: 0=no warning, 1=warnings, >=2=errors
    if result.returncode <= 1:
        print(f"Build completed with return code {result.returncode}.")
        return True, log_file, timestamp, output_name

    print(f"ERROR: build failed with return code {result.returncode}.")
    if os.path.exists(log_file):
        print(f"See build log: {log_file}")
    return False, log_file, timestamp, output_name


def export_timestamped_firmware(timestamp, output_name):
    """Copy firmware outputs to timestamped names and return created files."""
    created_files = []
    candidates = [
        os.path.join(OBJECT_DIR, f"{output_name}.hex"),
        os.path.join(OBJECT_DIR, f"{output_name}.bin"),
    ]

    for src in candidates:
        if not os.path.exists(src):
            continue
        stem, ext = os.path.splitext(src)
        dst = f"{stem}_{timestamp}{ext}"
        shutil.copy2(src, dst)
        created_files.append(dst)

    return created_files


if __name__ == "__main__":
    # Optional: update macro defines before build
    # modify_keil_macros(PROJECT_PATH, "STM32F10X_HD,USE_STDPERIPH_DRIVER,VERSION_V1")

    ok, log_file, timestamp, output_name = build_keil_project()
    if not ok:
        sys.exit(1)

    timestamped_files = export_timestamped_firmware(timestamp, output_name)
    if timestamped_files:
        print("Timestamped firmware files:")
        for file_path in timestamped_files:
            print(f" - {file_path}")
    else:
        print("WARNING: build succeeded but no .hex/.bin firmware was found to timestamp.")

    if log_file:
        print(f"Build log: {log_file}")
