# BRICOS Motion Controller

基于 STM32F407ZGTx 的 bricsbot 水下机器人下位机控制固件，架构遵循 BRICOS 纵向三层主链 + System 横向公共组件。上层计算平台负责视觉与导航，本系统负责高频姿态闭环、传感器采集与硬件驱动。

## 核心特性

- 主控平台：STM32F407ZGTx
- 开发环境：Keil MDK-ARM
- 通信接口
  - USART1/2：高频 IMU 采集 (DMA)
  - USART3：RT 控制与姿态反馈 (DMA + IDLE)
  - UART4：NRT 管理、调参与 OTA 触发 (DMA + IDLE)
- 传感器支持：IMU、MS5837、DHT11、ADC 电压电流采集
- 执行机构：推进器 PWM、舵机 PWM、灯光 PWM
- OTA：APP 设置启动标志并复位，由独立 bootloader 完成升级流程

## BRICOS 架构概要

系统采用纵向主链 BSP -> Driver -> Task，并由 System 提供 OSAL、数据池与系统服务。层间只允许上层调用下层接口，向上通知通过注册回调、队列或数据池实现。

- BSP 层：封装 MCU 外设与硬件初始化，如 [Bsp/bsp_uart.c](Bsp/bsp_uart.c) 与 [Bsp/bsp_pwm.c](Bsp/bsp_pwm.c)。
- Driver 层：与平台无关的协议与设备驱动，如 [Driver/driver_imu.c](Driver/driver_imu.c) 与 [Driver/driver_ms5837.c](Driver/driver_ms5837.c)。
- Task 层：业务任务封装，负责控制、通信与监控，如 [Task/task_control.c](Task/task_control.c) 与 [Task/task_comm.c](Task/task_comm.c)。
- System 组件：系统公共能力与数据一致性保障，如 [System/sys_data_pool.c](System/sys_data_pool.c) 与 [System/sys_monitor.c](System/sys_monitor.c)。

详细架构说明见 [BRICOS系统架构设计.md](BRICOS系统架构设计.md)。

## 任务与数据交互

- Task 间禁止直接函数调用，状态与数据通过全局数据池快照读写。
- 高速数据路径采用指针零拷贝传递，进入全局生命周期前使用值拷贝固化所有权。
- 关键数据池访问需在临界区内完成，避免竞态。

相关实现与约束见 [System/sys_data_pool.c](System/sys_data_pool.c) 与 [System/sys_monitor.c](System/sys_monitor.c)。

## 安全与监控

- Task_Monitor 负责看门狗与通信失控保护，仅修改系统模式，不直接驱动 PWM。
- 监控与安全防线设计见 [监控与安全防线设计.md](监控与安全防线设计.md)。

## 工程结构

- [User/main.c](User/main.c)：应用入口，初始化与主循环
- [Boot/boot_flag.h](Boot/boot_flag.h)：Boot 标志结构体与接口声明
- [Boot/boot_flag.c](Boot/boot_flag.c)：Boot 标志读写与复位请求
- [Bsp/](Bsp/)：底层外设封装
- [Driver/](Driver/)：设备与协议驱动
- [System/](System/)：OSAL、数据池与系统服务
- [Task/](Task/)：控制、通信、监控与传感任务
- [Project/](Project/)：Keil 工程与构建输出

## 编译说明

1. 使用 Keil 打开 [Project/project.uvprojx](Project/project.uvprojx)
2. 选择目标 Target 编译
3. 编译输出位于 [Project/Objects](Project/Objects)

当前目标芯片为 STM32F407ZGTx。

## OTA 说明

本仓库实现 APP 侧逻辑：

- APP 启动后设置启动成功标记
- 接收到 OTA 请求后设置进入 bootloader 标志
- 软件复位，后续升级由 bootloader 完成

相关接口定义见 [Boot/boot_flag.h](Boot/boot_flag.h)。

## 当前状态

工程处于持续开发中，模块初始化与任务策略可根据硬件需求启用或裁剪。
