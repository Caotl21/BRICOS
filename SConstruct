import os
import sys
from pathlib import Path

from SCons.Script import ARGUMENTS, Alias, AlwaysBuild, Default, Dir, Environment, Exit, Help, VariantDir


ROOT_DIR = Path(Dir("#").abspath)
TOOLS_DIR = ROOT_DIR / "tools" / "scons"
sys.path.insert(0, str(TOOLS_DIR))

from keil_project import load_keil_project


BUILD_PROFILE = ARGUMENTS.get("BUILD", "release")
TARGET_NAME = ARGUMENTS.get("TARGET", "Target 1")
TOOLCHAIN_PREFIX = ARGUMENTS.get("CROSS_COMPILE", os.environ.get("CROSS_COMPILE", "arm-none-eabi-"))

if BUILD_PROFILE not in {"release", "debug"}:
    Exit(f"Unsupported BUILD profile: {BUILD_PROFILE}")

project = load_keil_project(ROOT_DIR / "Project" / "project.uvprojx", target_name=TARGET_NAME)

build_root = ROOT_DIR / "build" / "gcc" / "app1" / BUILD_PROFILE
variant_root = build_root / "worktree"
VariantDir(str(variant_root), ".", duplicate=0)

source_nodes = [str((variant_root / rel_path).as_posix()) for rel_path in project["sources"]]
linker_script = ROOT_DIR / "tools" / "scons" / "linker" / "app1.ld"

common_arch_flags = [
    "-mcpu=cortex-m4",
    "-mthumb",
    "-mfpu=fpv4-sp-d16",
    "-mfloat-abi=hard",
]

common_compile_flags = common_arch_flags + [
    "-ffunction-sections",
    "-fdata-sections",
    "-fno-common",
    "-g3",
]

opt_flags = {
    "release": "-O2",
    "debug": "-Og",
}[BUILD_PROFILE]

env = Environment(ENV=os.environ)
env.Replace(
    CC=f"{TOOLCHAIN_PREFIX}gcc",
    AS=f"{TOOLCHAIN_PREFIX}gcc",
    AR=f"{TOOLCHAIN_PREFIX}ar",
    RANLIB=f"{TOOLCHAIN_PREFIX}ranlib",
    OBJCOPY=f"{TOOLCHAIN_PREFIX}objcopy",
    SIZE=f"{TOOLCHAIN_PREFIX}size",
)

env.Append(
    CPPDEFINES=project["defines"],
    CPPPATH=[str((ROOT_DIR / include_path).as_posix()) for include_path in project["includes"]],
    CCFLAGS=common_compile_flags + [opt_flags],
    CFLAGS=[
        "-std=gnu11",
        "-Wno-unused-parameter",
        "-Wno-unused-function",
        "-Wno-sign-compare",
        "-Wno-missing-field-initializers",
    ],
    ASFLAGS=common_arch_flags + ["-x", "assembler-with-cpp"],
    LINKFLAGS=common_arch_flags + [
        "-nostartfiles",
        "--specs=nano.specs",
        "--specs=nosys.specs",
        f"-T{linker_script.as_posix()}",
        "-Wl,--gc-sections",
        f"-Wl,-Map,{(build_root / 'app1.map').as_posix()}",
        "-Wl,--cref",
        "-Wl,--print-memory-usage",
        "-Wl,-u,_printf_float",
    ],
)

program = env.Program(target=str((build_root / "app1").as_posix()), source=source_nodes)
binary = env.Command(
    str((build_root / "app1.bin").as_posix()),
    program,
    "$OBJCOPY -O binary $SOURCE $TARGET",
)

AlwaysBuild(Alias("size", program, "$SIZE $SOURCE"))
Alias("elf", program)
Alias("bin", binary)

Default(binary)

Help(
    "\n".join(
        [
            "GCC + SCons build for the APP1 firmware image.",
            "",
            "Examples:",
            "  scons",
            "  scons BUILD=debug",
            "  scons CROSS_COMPILE=arm-none-eabi-",
        ]
    )
)
