#include "bsp_cpu.h"
#include "stm32f4xx.h"

void bsp_cpu_reset(void) {
    NVIC_SystemReset();
}
