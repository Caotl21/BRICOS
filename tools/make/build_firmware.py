import datetime
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

# Config
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
KEIL_EXE = r"D:\Keil_v5\UV4\UV4.exe"
PROJECT_PATH = os.path.join(ROOT_DIR, "Project", "project.uvprojx")
TARGET_NAME = "Target 1"
LOG_DIR = os.path.join(ROOT_DIR, "Build")
OBJECT_DIR = os.path.join(ROOT_DIR, "Project", "Objects")
HIDE_KEIL_WINDOW = True


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
    """Run Keil build and return (success, build_dir, log_file, output_name, timestamp)."""
    if not os.path.exists(KEIL_EXE):
        print(f"ERROR: UV4.exe not found: {KEIL_EXE}")
        return False, None, None, None, None

    if not os.path.exists(PROJECT_PATH):
        print(f"ERROR: project file not found: {PROJECT_PATH}")
        return False, None, None, None, None

    os.makedirs(LOG_DIR, exist_ok=True)

    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    build_dir = os.path.join(LOG_DIR, timestamp)
    os.makedirs(build_dir, exist_ok=True)
    log_file = os.path.join(build_dir, f"build_{timestamp}.log")
    output_name = get_output_name(PROJECT_PATH)

    cmd = [KEIL_EXE, "-j0", "-r", PROJECT_PATH, "-t", TARGET_NAME, "-o", log_file]
    print(f"Building target '{TARGET_NAME}' ...")

    try:
        run_kwargs = {"check": False}
        if os.name == "nt" and HIDE_KEIL_WINDOW:
            startupinfo = subprocess.STARTUPINFO()
            startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startupinfo.wShowWindow = 0
            run_kwargs["startupinfo"] = startupinfo
            run_kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW
        result = subprocess.run(cmd, **run_kwargs)
    except Exception as exc:
        print(f"ERROR: failed to execute Keil: {exc}")
        return False, build_dir, log_file, output_name, timestamp

    # Keil return code: 0=no warning, 1=warnings, >=2=errors
    if result.returncode <= 1:
        print(f"Build completed with return code {result.returncode}.")
        return True, build_dir, log_file, output_name, timestamp

    print(f"ERROR: build failed with return code {result.returncode}.")
    if os.path.exists(log_file):
        print(f"See build log: {log_file}")
    return False, build_dir, log_file, output_name, timestamp


def export_build_bin(build_dir, output_name, timestamp):
    """Copy build bin artifact into Build/<timestamp>/ and return output path."""
    src_bin = os.path.join(OBJECT_DIR, f"{output_name}.bin")
    if not os.path.exists(src_bin):
        return None

    dst_bin = os.path.join(build_dir, f"{output_name}_{timestamp}.bin")
    shutil.copy2(src_bin, dst_bin)
    return dst_bin


if __name__ == "__main__":
    # Optional: update macro defines before build
    # modify_keil_macros(PROJECT_PATH, "STM32F10X_HD,USE_STDPERIPH_DRIVER,VERSION_V1")

    ok, build_dir, log_file, output_name, timestamp = build_keil_project()
    if not ok:
        sys.exit(1)

    copied_bin = export_build_bin(build_dir, output_name, timestamp)
    if copied_bin:
        print("Build output:")
        print(f" - BIN: {copied_bin}")
    else:
        print("ERROR: build succeeded but no .bin firmware was found.")
        sys.exit(2)

    if log_file:
        print(f"Build log: {log_file}")
