#ifndef FAULT_SNAPSHOT_H
#define FAULT_SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>

bool System_FaultSnapshot_SaveStackOverflowTask(const char *task_name);
bool System_FaultSnapshot_LoadLastStackOverflowTask(char *task_name, uint32_t task_name_size);

#endif // FAULT_SNAPSHOT_H
