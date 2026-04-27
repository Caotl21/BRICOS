#ifndef SYS_FLASH_LAYOUT_H
#define SYS_FLASH_LAYOUT_H

#include <stdint.h>

/*
 * STM32F407ZGTx internal flash map (1MB, 0x08000000 ~ 0x080FFFFF)
 *
 * +----------------+------------+----------+------------------------------+
 * | Start Addr     | Size       | Sector   | Usage                        |
 * +----------------+------------+----------+------------------------------+
 * | 0x08000000     | 0x00004000 | S0       | Bootloader / vector          |
 * | 0x08004000     | 0x00004000 | S1       | BootFlag                     |
 * | 0x08008000     | 0x00018000 | S2~S4    | APP1                         |
 * | 0x08020000     | 0x00020000 | S5       | APP2                         |
 * | 0x08040000     | 0x00020000 | S6       | Reserved                     |
 * | 0x08060000     | 0x00020000 | S7       | Reserved                     |
 * | 0x08080000     | 0x00020000 | S8       | Reserved: PersistLog bank0   |
 * | 0x080A0000     | 0x00020000 | S9       | Reserved: PersistLog bank1   |
 * | 0x080C0000     | 0x00020000 | S10      | Fault snapshot               |
 * | 0x080E0000     | 0x00020000 | S11      | PID params                   |
 * +----------------+------------+----------+------------------------------+
 *
 * Notes:
 * 1) All flash users should include this file and avoid hard-coded addresses.
 * 2) If OTA/bootloader layout changes, update this table first.
 */

#define SYS_FLASH_BASE_ADDR                     ((uint32_t)0x08000000u)
#define SYS_FLASH_TOTAL_SIZE                    ((uint32_t)0x00100000u)
#define SYS_FLASH_END_ADDR                      (SYS_FLASH_BASE_ADDR + SYS_FLASH_TOTAL_SIZE)

#define SYS_FLASH_BOOT_FLAG_ADDR                ((uint32_t)0x08004000u)
#define SYS_FLASH_BOOT_FLAG_SIZE                ((uint32_t)0x00004000u)

#define SYS_FLASH_APP1_ADDR                     ((uint32_t)0x08008000u)
#define SYS_FLASH_APP1_SIZE                     ((uint32_t)0x00018000u)

#define SYS_FLASH_APP2_ADDR                     ((uint32_t)0x08020000u)
#define SYS_FLASH_APP2_SIZE                     ((uint32_t)0x00020000u)

#define SYS_FLASH_PERSIST_LOG_BANK0_ADDR        ((uint32_t)0x08080000u)
#define SYS_FLASH_PERSIST_LOG_BANK1_ADDR        ((uint32_t)0x080A0000u)
#define SYS_FLASH_PERSIST_LOG_BANK_SIZE         ((uint32_t)0x00020000u)
#define SYS_FLASH_PERSIST_LOG_TOTAL_SIZE        (SYS_FLASH_PERSIST_LOG_BANK_SIZE * 2u)

#define SYS_FLASH_FAULT_SNAPSHOT_ADDR           ((uint32_t)0x080C0000u)
#define SYS_FLASH_FAULT_SNAPSHOT_SIZE           ((uint32_t)0x00020000u)

#define SYS_FLASH_PID_PARAM_ADDR                ((uint32_t)0x080E0000u)
#define SYS_FLASH_PID_PARAM_ERASE_SIZE          ((uint32_t)0x00020000u)

/* Basic compile-time layout checks. */
#if (SYS_FLASH_BOOT_FLAG_ADDR < SYS_FLASH_BASE_ADDR)
#error "BOOT_FLAG start is below flash base."
#endif

#if ((SYS_FLASH_BOOT_FLAG_ADDR + SYS_FLASH_BOOT_FLAG_SIZE) > SYS_FLASH_END_ADDR)
#error "BOOT_FLAG region exceeds flash range."
#endif

#if ((SYS_FLASH_APP1_ADDR + SYS_FLASH_APP1_SIZE) > SYS_FLASH_END_ADDR)
#error "APP1 region exceeds flash range."
#endif

#if ((SYS_FLASH_APP2_ADDR + SYS_FLASH_APP2_SIZE) > SYS_FLASH_END_ADDR)
#error "APP2 region exceeds flash range."
#endif

#if ((SYS_FLASH_PERSIST_LOG_BANK0_ADDR + SYS_FLASH_PERSIST_LOG_BANK_SIZE) > SYS_FLASH_END_ADDR)
#error "PersistLog bank0 exceeds flash range."
#endif

#if ((SYS_FLASH_PERSIST_LOG_BANK1_ADDR + SYS_FLASH_PERSIST_LOG_BANK_SIZE) > SYS_FLASH_END_ADDR)
#error "PersistLog bank1 exceeds flash range."
#endif

#if ((SYS_FLASH_FAULT_SNAPSHOT_ADDR + SYS_FLASH_FAULT_SNAPSHOT_SIZE) > SYS_FLASH_END_ADDR)
#error "Fault snapshot region exceeds flash range."
#endif

#if ((SYS_FLASH_PID_PARAM_ADDR + SYS_FLASH_PID_PARAM_ERASE_SIZE) > SYS_FLASH_END_ADDR)
#error "PID param region exceeds flash range."
#endif

#if ((SYS_FLASH_BOOT_FLAG_ADDR + SYS_FLASH_BOOT_FLAG_SIZE) > SYS_FLASH_APP1_ADDR)
#error "BOOT_FLAG overlaps APP1."
#endif

#if ((SYS_FLASH_APP1_ADDR + SYS_FLASH_APP1_SIZE) > SYS_FLASH_APP2_ADDR)
#error "APP1 overlaps APP2."
#endif

#if ((SYS_FLASH_PERSIST_LOG_BANK0_ADDR + SYS_FLASH_PERSIST_LOG_BANK_SIZE) > SYS_FLASH_PERSIST_LOG_BANK1_ADDR)
#error "PersistLog bank0 overlaps bank1."
#endif

#if ((SYS_FLASH_PERSIST_LOG_BANK1_ADDR + SYS_FLASH_PERSIST_LOG_BANK_SIZE) > SYS_FLASH_FAULT_SNAPSHOT_ADDR)
#error "PersistLog overlaps fault snapshot."
#endif

#if ((SYS_FLASH_FAULT_SNAPSHOT_ADDR + SYS_FLASH_FAULT_SNAPSHOT_SIZE) > SYS_FLASH_PID_PARAM_ADDR)
#error "Fault snapshot overlaps PID param."
#endif

#endif /* SYS_FLASH_LAYOUT_H */
