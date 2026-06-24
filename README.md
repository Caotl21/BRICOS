# BRICOS Motion Controller

BRICOS Motion Controller 是面向 Bricsbot 水下机器人设计的 STM32 下位机控制固件。它不是一个单纯的电机控制板程序，而是为水下具身智能原生设计的嵌入式实时基座：向上为 ROS/上位机智能系统提供稳定的运动、状态、参数和调试接口，向下为推进器、舵机、灯光、惯导、深度、电源和安全保护提供确定性的实时控制能力。

一句话概括：

> 上层智能负责“想做什么”，BRICOS 负责“能不能安全地做、如何实时地做、做到什么状态”。

## 当前硬件资源

### 主控与执行机构

| 资源 | 数量 | 说明 |
| --- | ---: | --- |
| MCU | 1 | STM32F407ZGTx |
| 推进器 | 6 | PWM 输出，参与 TAM 推力分配 |
| 舵机 | 2 | PWM 输出 |
| 探照灯 | 2 | PWM 调光 |
| WS2812 彩灯 | 2 路 | TIM PWM + DMA 驱动 |

### 传感器

| 资源 | 接口 | 说明 |
| --- | --- | --- |
| IMU1 | UART1 | 第一惯导通道 |
| IMU2 | UART2 | 第二惯导通道 |
| IMU3 | UART6 | 第三惯导通道，面向冗余和融合扩展 |
| MS5837 | I2C | 深度和水温 |
| DHT11 | GPIO 单总线 | 舱内温湿度 |
| 电源检测 | ADC | 电池电压、电流 |
| 芯片温度 | ADC | MCU 内部温度 |
| 漏水检测 | GPIO/传感输入 | 水密舱安全检测 |

## 通信接口

| 通道 | UART | 波特率 | 用途 |
| --- | --- | --- | --- |
| RT | UART3 | `921600-8N1` | 实时运动控制、高频本体/推进器状态 |
| NRT | UART4 | `921600-8N1` | 参数、模式、TAM、OTA、ACK、系统状态、持久化日志清理 |
| LOG/Shell | UART5 | `1500000-8N1` | 启动日志、实时日志、`[PERSIST]` replay、人工调试 Shell |
| IMU3 | UART6 | 由 IMU 驱动配置 | 第三路 IMU |

注意：

- RT/NRT 使用 HydroCore 二进制帧协议。
- LOG/Shell 是 UART5 字符流，不属于 ROS 二进制协议帧。
- NRT 与 LOG/Shell 已经分离；上位机不要从 NRT 等待日志文本。
- `0x05 DATA_TYPE_LOG` 当前只是保留定义，实时日志实际走 UART5。

## 系统模式

BRICOS 使用 ModeManager 统一管理系统安全状态。

| SysMode | 说明 |
| --- | --- |
| `standby` | 待机态，系统已启动但不接受运动输出 |
| `disarmed` | 激活但未解锁，可通信、可配置、可读取传感器 |
| `armed` | 运动使能态，允许控制任务驱动推进器 |
| `failsafe` | 故障保护态，强制安全输出并禁止危险动作 |

运动模式包括：

- `manual`：上位机直接给出运动目标。
- `stabilize`：姿态/深度闭环稳定。
- `auto`：面向上层任务规划的自动控制入口。

关键安全约束：

- 外部不能普通 request 直接进入 `failsafe`，必须由故障入口触发。
- `failsafe` 恢复必须先回到 `disarmed`，不能直接进入 `armed`。
- 存在 IMU 故障时，`stabilize/auto` 会被阻止。
- 当前 `SYS_FAULT_LOS` 已预留，但自动 RT 失联检测尚未接入 `Task_Monitor` 主循环。

## 软件架构

BRICOS 采用“分层架构 + 系统组件”的组织方式。

```text
上位机 / ROS / 智能应用
        |
        | RT / NRT / Debug
        v
Task    通信、传感器、控制、监控、NRT 服务、Shell
System  数据池、模式管理、日志、监控、PID/TAM、Shell Core
Driver  IMU、MS5837、电源、推进器、舵机、灯光、WS2812、HydroCore
BSP     UART、DMA、TIM/PWM、ADC、I2C、GPIO、Flash、Watchdog
MCU     STM32F407 + FreeRTOS + 机器人硬件
```

设计约束：

- BSP 只处理 MCU 外设和硬件资源映射。
- Driver 只封装设备行为，不决定系统模式。
- Task 通过 System 数据池交换状态，避免任务之间共享可变全局变量。
- 安全保护优先级高于控制输出，上位机命令不能覆盖本地 failsafe。

## 主要任务

| 任务 | 优先级 | 周期/触发 | 说明 |
| --- | ---: | --- | --- |
| `Task_Control` | 5 | armed 下 100Hz，standby 下 2Hz | PID/TAM、模式执行、推进器输出 |
| `Task_IMU` | 4 | 周期/事件 | IMU 数据解析和身体状态更新 |
| `Task_RT_Comm` | 4 | UART3 DMA/队列 | 实时控制帧和高频状态链路 |
| `Task_NRT_Comm` | 3 | UART4 DMA/队列 | 非实时服务帧、ACK、OTA |
| `Log_Task` | 3 | 队列 | 异步日志输出和持久化日志 replay |
| `ShellU5Rx` | 3 | UART5 RX | Debug Shell 字符流输入 |
| `Task_MS5837` | 3 | 周期 | 深度和水温 |
| `Task_Power` | 2 | 周期 | 电压、电流、芯片温度 |
| `Task_DHT11` | 2 | 周期 | 舱内温湿度 |
| `Task_Monitor` | 1 | 1Hz | 任务心跳、故障位、栈水位、系统状态回传 |

## 监控与安全

当前监控链路包括：

- 任务心跳：`Bot_Task_CheckIn_Monitor()`。
- 栈水位：通过 NRT `0x07 DATA_TYPE_STATE_STACK_WM` 回传。
- 故障位：漏水、IMU、低压、任务超时、LOS 预留。
- 低压告警：`MONITOR_LOW_VOLTAGE_WARN_V = 22.0f`。
- failsafe 阈值：来自 `bot_params_t.failsafe_low_voltage`。
- 看门狗：`Task_Monitor` 在任务健康时喂狗，异常时停止喂狗作为硬件兜底。
- 持久化日志：`LOG_WARNING` 和 `LOG_ERROR` 写入 Flash，重启后以 `[PERSIST]` 前缀 replay。

## Shell 与日志

Shell 当前绑定 UART5 Debug 字符流，波特率 `1500000`。启动后会输出：

```text
startup_success
```

常用命令：

```text
help
sysmode request
sysmode set standby|disarmed|armed|failsafe
momode request
momode set manual|stabilize|auto
fault
euler request
depthtemp request
power request
cabin request
chip request
thruster request
ws2812 request
```

Shell 命令使用 `EXPORT_SHELL_CMD()` + `ShellCmd$$Base/Table/Limit` 链接段自动注册。新增命令通常只需要在 `User/shell/` 中实现 handler 并导出。

## 工程目录

| 目录 | 说明 |
| --- | --- |
| `User/` | 应用入口、Shell 命令、FreeRTOS hook、故障快照 |
| `Task/` | 控制、通信、传感器、监控、RT/NRT 命令处理 |
| `System/` | 数据池、模式管理、日志、监控、Shell Core、PID、启动标志 |
| `Driver/` | HydroCore 协议、IMU、MS5837、电源、推进器、舵机、灯光、WS2812、参数 |
| `Bsp/` | STM32 外设抽象：UART、PWM、ADC、I2C、GPIO、Flash、Timer、Watchdog |
| `Boot/` | Boot flag 与 OTA 入口标志 |
| `FreeRTOS/` | RTOS 内核与配置 |
| `Libraries/` | STM32 标准外设库等依赖 |
| `Project/` | Keil 工程、scatter 文件引用、编译输出 |
| `tools/` | 上位机辅助脚本和调试工具 |
| `docs/` | 架构、协议、Shell、监控与模式设计文档 |

## 编译方式

1. 使用 Keil MDK-ARM 打开 `Project/project.uvprojx`。
2. 选择对应 Target。
3. 编译生成 `Project/Objects/app.axf` 等输出。
4. 如需针对单文件排查调试，可在 Keil 中对对应 `.c` 文件单独设置优化等级。

当前仓库没有提供通用命令行构建脚本，主要以 Keil 工程为准。

## OTA 与参数

当前 APP 侧提供：

- OTA 触发命令：NRT `0x10 DATA_TYPE_OTA`。
- PID 参数写入：NRT `0x11 DATA_TYPE_SET_PID_PARAM`，成功后写 Flash 并复位。
- TAM 写入：NRT `0x15 DATA_TYPE_SET_TAM`，成功后写 Flash 并复位。
- IMU 加速度校准：NRT `0x19 DATA_TYPE_CALIBRATE_IMU_ACC`。
- 持久化日志清理：NRT `0x1A DATA_TYPE_CLEAR_PERSIST_LOG`，带 ACK，不复位。

详细 payload 和 ACK 定义见 `docs/ROS上下位机通信接口.md`。

## 重要文档

- [BRICOS系统架构设计.md](docs/BRICOS系统架构设计.md)
- [ROS上下位机通信接口.md](docs/ROS上下位机通信接口.md)
- [系统调试工具shell设计.md](docs/系统调试工具shell设计.md)
- [监控与安全防线设计.md](docs/监控与安全防线设计.md)
- [模式设置方案.md](docs/模式设置方案.md)

## 当前注意事项

- LOG/Shell 现在是 UART5 字符流，不再通过 NRT 日志脚本读取。
- UART5 波特率是 `1500000`，不是 `921600`。
- `Task_ShellTransportNRT_GetVTable()` 名称带 NRT 是历史遗留，当前行为是 UART5 Debug transport。
- `SYS_FAULT_LOS` 已定义，但自动 RT 失联检测尚未接入 Monitor 主循环。
- NRT `0x05 DATA_TYPE_LOG` 当前保留未使用。
- `SET_WS2812_COLOR` 的 NRT handler 当前仍需要上位机保证 `strip` 参数合法。
