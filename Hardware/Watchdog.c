#include "Watchdog.h"

/**
  * 函    数：看门狗初始化
  * 参    数：无
  * 返 回 值：无
  * 注意事项：超时时间约为1秒
  */
void Watchdog_Init(void)
{
    // 启动LSI时钟
    RCC_LSICmd(ENABLE);
    while(RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET);
    
    // 取消写保护
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    
    // 设置预分频器 (LSI/32 = 40kHz/32 = 1.25kHz)
    IWDG_SetPrescaler(IWDG_Prescaler_32);
    
    // 设置重载值 (1.25kHz下，1250对应1秒)
    IWDG_SetReload(1250);
    
    // 重载计数器
    IWDG_ReloadCounter();
    
    // 启动看门狗
    IWDG_Enable();
}

/**
  * 函    数：喂狗
  * 参    数：无
  * 返 回 值：无
  */
void Watchdog_Feed(void)
{
    IWDG_ReloadCounter();
}

