# STM32F103 -> STM32F407 Bootloader Migration Handoff

## 1. Current Status

I have moved the current bootloader-related source files into an STM32F407 project.

### Files already migrated
- `bootloader/Bootloader/main.c`
- `bootloader/Bootloader/bootloader.c`
- `bootloader/Bootloader/bootloader.h`
- `bootloader/Bootloader/flash_if.c`
- `bootloader/Bootloader/flash_if.h`
- `bootloader/Bootloader/flash_layout.h`
- `bootloader/Bootloader/ymodem.c`
- `bootloader/Bootloader/ymodem.h`
- `bootloader/Bootloader/bootloader.sct`

## 2. Migration decisions already made

### Flash layout (STM32F407, Option E: dedicate one sector for flags)
- Bootloader: Sector 0  
  `0x08000000` - `0x08003FFF` (16KB)
- Boot Flag: Sector 1  
  `0x08004000` - `0x08007FFF` (16KB)
- APP1 (run): Sector 2~4  
  start `0x08008000`, size `96KB`
- APP2 (OTA cache): Sector 5  
  start `0x08020000`, size `128KB`

### Architecture changes
- Main flow is simplified in `main.c`
- Menu / command handling is moved into `bootloader.c`
- OTA strategy: download to APP2 first, then copy APP2 -> APP1 via flag mechanism

## 3. What has been adapted to F407

- `main.c` now uses F4 clock/GPIO/USART init style (AHB1 + AF mapping)
- `bootloader.h` address definitions updated to F407 sector layout
- `bootloader.c` app check/jump/copy/recovery logic updated around new addresses
- Flash API switched to sector-based concept (`flash_layout.h` + `flash_if.*`)
- Ymodem headers switched from F1 include to F4 include path direction

## 4. Remaining work (must do)

### A. Fix Ymodem integration consistency
- Ensure `ymodem.c` TX/RX uses the same UART as OTA data path (currently expected: `USART2`)
- Implement missing `Ymodem_Abort()` in `ymodem.c` (already declared in `ymodem.h`)
- Confirm `Ymodem_ReceivePacket` signature matches all call sites
- Remove any remaining F103 comments/references in Ymodem files

### B. Fix function/interface consistency in `bootloader.c`
- Ensure naming is consistent:
  - `Bootloader_ShowMenu`
  - `Bootloader_ShowSystemInfo`
  - `Bootloader_ProcessCommand`
  - `Show_Version` (or统一命名)
- Ensure `Process_YmodemDownload` is implemented in `bootloader.c` and no unresolved symbol remains
- Ensure functions declared in `bootloader.h` are non-static in `.c`
- Keep internal helpers as `static` and not declared in `.h`

### C. Project configuration for STM32F407 (Keil)
- Device: `STM32F407ZGTx`
- Preprocessor defines:
  - `STM32F40_41xxx`
  - `USE_STDPERIPH_DRIVER`
- Include paths must contain:
  - CMSIS include
  - `STM32F4xx` device include
  - `STM32F4xx_StdPeriph_Driver/inc`
  - project local bootloader folder
- Use F407 startup file (`startup_stm32f407xx.s`)
- Use F407 system file (`system_stm32f4xx.c`)
- Use `bootloader/Bootloader/bootloader.sct` as scatter file

### D. Verify F4 StdPeriph source files are added
Minimum required drivers:
- `stm32f4xx_rcc.c`
- `stm32f4xx_gpio.c`
- `stm32f4xx_usart.c`
- `stm32f4xx_flash.c`
- `misc.c`

## 5. Build/runtime acceptance checklist

- [ ] Project compiles with zero unresolved symbols
- [ ] Bootloader prints startup banner
- [ ] Press/send `B` enters command loop
- [ ] Ymodem download to APP2 works end-to-end
- [ ] `need_copy` flag set after OTA success
- [ ] After reset, APP2 -> APP1 copy succeeds
- [ ] Jump to APP1 succeeds when APP1 is valid
- [ ] Recovery path works when APP1 invalid and APP2 valid
- [ ] Stays in bootloader safely when both APP1/APP2 invalid

## 6. Known risks / checkpoints for Copilot

- Do not reintroduce F103 headers (`stm32f10x*`)
- Keep Flash erase/write behavior sector-based for F407
- Keep APP addresses aligned to sector boundaries
- Keep command/OTA logic in `bootloader.c`, keep `main.c` minimal
- Verify vector table and MSP validity check before jump

## 7. Suggested next action for Copilot

1. Run compile error sweep and resolve all symbol/signature mismatches first.
2. Lock Ymodem UART path to a single USART (recommended `USART2`) consistently.
3. Run first serial smoke test:
   - banner
   - command loop
   - fake/real Ymodem session
4. Finalize with one clean “migration completed” status summary.