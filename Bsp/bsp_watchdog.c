#include "bsp_watchdog.h"
#include "stm32f4xx.h"
#include "stm32f4xx_iwdg.h"
#include "stm32f4xx_rcc.h"

void bsp_wdg_init(uint16_t timeout_ms) 
{
    // 启动内部低速时钟 LSI (独立看门狗专用)
    RCC_LSICmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET) {
        // 等待 LSI 稳定
    }

    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    
    // 此时 IWDG 计数器时钟 = 32kHz / 64 = 500Hz (即每 2ms 计一次数)
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    
    // timeout_ms / 2 就是需要的计数值
    uint32_t reload_val = timeout_ms / 2;
    
    // 安全限幅：12位寄存器，最大值为 4095 (4095 * 2ms ≈ 8.19秒)
    if (reload_val > 0x0FFF) {
        reload_val = 0x0FFF; 
    }
    if (reload_val == 0) {
        reload_val = 1;
    }
    
    IWDG_SetReload(reload_val);
    
    IWDG_ReloadCounter();
    
    IWDG_Enable();
}

void bsp_wdg_feed(void) 
{
    // 刷新计数器，防止单片机复位
    IWDG_ReloadCounter();
}