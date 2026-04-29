#ifndef SYS_LOG_H
#define SYS_LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_INFO,
	  LOG_LEVEL_DEBUG
} log_level_t;

void System_Log_Init(void);
void Log_Print(log_level_t level, const char *fmt, ...);
void System_Log_PersistReplay(uint16_t max_count);
bool System_Log_PersistClear(void);

#define LOG_ERROR(fmt, ...)   Log_Print(LOG_LEVEL_ERROR,   fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) Log_Print(LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)    Log_Print(LOG_LEVEL_INFO,    fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)   Log_Print(LOG_LEVEL_DEBUG,    fmt, ##__VA_ARGS__)

#endif
