#include "driver_pswitch.h"
#include "bsp_gpio.h"
#include "bsp_delay.h"

static void PSWITCH_IO_OUT(void)
{
    // 调用 BSP 接口设置为输出模式（默认推挽）
    bsp_gpio_set_direction(BSP_GPIO_PSWITCH, true);
}

static void PSWITCH_IO_IN(void)
{
    // 调用 BSP 接口设置为输入模式（默认上拉）
    bsp_gpio_set_direction(BSP_GPIO_PSWITCH, false);
}

// 初始化 PSWITCH
// 返回值：1:不存在; 0:存在         
void Driver_PSWITCH_Init(void)
{               
    PSWITCH_IO_OUT();       // SET OUTPUT
    bsp_gpio_write(BSP_GPIO_PSWITCH, false); // 上电默认拉低 PSWITCH
}

void Driver_PSWITCH_ON(void)
{
    bsp_gpio_write(BSP_GPIO_PSWITCH, true);  // 拉高 PSWITCH
}

void Driver_PSWITCH_OFF(void)
{
    bsp_gpio_write(BSP_GPIO_PSWITCH, false);  // 拉低 PSWITCH
}
