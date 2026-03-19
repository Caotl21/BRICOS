#ifndef SYS_LOG_H
#define SYS_LOG_H

#include <stdarg.h>

typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_INFO
} log_level_t;

void Log_Init(void);
void Log_Print(log_level_t level, const char *fmt, ...);

#define LOG_ERROR(fmt, ...)   Log_Print(LOG_LEVEL_ERROR,   fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) Log_Print(LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)    Log_Print(LOG_LEVEL_INFO,    fmt, ##__VA_ARGS__)

#endif