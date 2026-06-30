# GCC + SCons Build

This repository now includes a first-pass `arm-none-eabi-gcc + SCons` build for
the APP1 firmware image.

## Scope

- The runnable firmware is linked for `APP1` at `0x08008000`.
- `APP2` remains an OTA staging slot and is not built as an executable target.
- The generated artifacts are:
  - `build/gcc/app1/<profile>/app1`
  - `build/gcc/app1/<profile>/app1.bin`
  - `build/gcc/app1/<profile>/app1.map`

## Local Build

Install the GNU Arm Embedded toolchain and SCons, then run:

```bash
scons
```

Optional:

```bash
scons BUILD=debug
scons CROSS_COMPILE=arm-none-eabi-
```

## Notes

- The build reuses the existing Keil project as the source of truth for the
  source list, include paths, and preprocessor defines.
- The Keil scatter file is replaced by `tools/scons/linker/app1.ld`.
- FreeRTOS now has a GCC Cortex-M4F portability layer under
  `FreeRTOS/portable/GCC/ARM_CM4F/`.
