#ifndef __SYS_MONITOR_H
#define __SYS_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

bool System_Runtime_Monitor_Init(void);
uint32_t System_Runtime_GetCounter(void);
uint32_t System_Runtime_GetCpuUsagePercent(void);
uint32_t System_Runtime_GetChipTemperature(void);

#endif /* __SYS_MONITOR_H */