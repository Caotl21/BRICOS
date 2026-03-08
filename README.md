# AUV_STM32

基于 STM32F407ZGTx 的 bricsbot机器人 下位机控制固件工程。

当前仓库主要包含应用层 APP 工程代码，配套 bootloader 工程独立维护。工程覆盖串口通信、传感器采集、推进器与舵机控制、任务调度，以及 OTA 升级相关启动标志处理。该版本为基于轮询调度器的轻量级运控下位机版本。

## 功能概览

- 主控平台：STM32F407ZGTx
- 开发环境：Keil MDK-ARM
- 通信接口
  - USART1：IM948
  - USART2：JY901S
  - USART3：应用侧指令接收
  - USART4：上位机/控制通信
- 传感器支持
  - JY901B
  - IM948
  - MS5837
  - DHT11
  - ADC 电压电流采集
- 执行机构
  - 推进器 PWM
  - 舵机 PWM
  - 灯光控制
- 调度方式
  - 简单任务调度器
- OTA 相关
  - APP 侧支持设置启动标志并软件复位，由 bootloader 决定后续升级流程

## 工程结构

- [User/main.c](User/main.c)  
  应用入口，负责基础初始化与主循环
- [Boot/boot_flag.h](Boot/boot_flag.h)  
  Boot 标志结构体与接口声明
- [Boot/boot_flag.c](Boot/boot_flag.c)  
  Boot 标志读写、启动成功标记、进入 bootloader 请求
- [Hardware/Serial.c](Hardware/Serial.c)  
  串口初始化、收发与中断处理
- [Hardware/Serial.h](Hardware/Serial.h)  
  通信协议、串口接口与传感器数据结构
- [User/TaskScheduler.h](User/TaskScheduler.h)  
  任务调度器定义
- [User/Tasks.h](User/Tasks.h)  
  任务接口声明
- `Hardware/`  
  各类硬件驱动模块
- `System/`  
  基础延时与定时器支持
- `Project/`  
  Keil 工程文件与构建输出

## 编译说明

1. 使用 Keil 打开 [Project/project.uvprojx](Project/project.uvprojx)
2. 选择目标 Target 编译
3. 编译输出位于 `Project/Objects` 目录

当前目标芯片为 STM32F407ZGTx。

## OTA 说明

本仓库主要实现 APP 侧逻辑：

- APP 启动后可调用启动成功标记接口
- 当接收到 OTA 请求时，APP 设置进入 bootloader 标志
- 随后执行软件复位
- 具体升级、镜像切换与跳转逻辑由独立 bootloader 工程负责

相关接口定义见 [Boot/boot_flag.h](Boot/boot_flag.h)。

## 当前状态

当前工程处于调试开发阶段，部分模块初始化与任务调度逻辑可根据实际硬件需求启用或裁剪。
