# BRICOS 仓库架构速览（协作 Agent）

> 更新时间：2026-04-05  
> 说明：本文件以仓库当前代码实现为准，面向后续协作 Agent 快速上手。

## 1. 项目定位

- 硬件平台：`STM32F407ZGTx`
- 系统角色：水下机器人下位机（高频姿态/深度控制、传感器采集、执行器控制、上下位机通信）
- RTOS：`FreeRTOS`（抢占式，`1kHz tick`）
- 工程入口：`User/main.c`

上位机（香橙派/ROS）负责高级视觉与导航，下位机负责硬实时控制与硬件执行。

## 2. 架构分层（当前实现）

整体采用 `BSP -> Driver -> Task` 主链，`System` 作为横向公共组件：

- `Bsp/`：外设抽象与芯片耦合实现（UART、PWM、ADC、I2C、GPIO、Flash、Watchdog、Timer、CPU Reset）
- `Driver/`：设备/协议驱动与算法基础（IMU、MS5837、DHT11、电源、推进器、协议引擎、参数持久化）
- `Task/`：业务任务层（控制、传感、通信、监控、命令处理）
- `System/`：系统级公共能力（数据池、日志、监控统计、SysTick、BootFlag）

核心依赖方向：

- 允许：`Task -> Driver -> BSP`
- 允许：`Task/Driver -> System`
- 避免：下层直接调用上层业务逻辑

## 3. 启动流程（main）

`User/main.c` 启动顺序如下：

1. 中断优先级分组配置
2. BSP 初始化：`delay / uart / gpio / pwm`
3. Driver 初始化：`IMU / MS5837 / DHT11 / Power / Thruster`
4. System 初始化：`SysTick / BootFlag / Log / DataPool`
5. 任务初始化：`Comm / Sensor / Control / NRT_Cmd / RT_Cmd / Monitor`
6. `vTaskStartScheduler()` 启动调度

## 4. 任务拓扑（实际代码）

### 4.1 周期与优先级

| 任务 | 优先级 | 节拍/触发 | 主要职责 |
|---|---:|---|---|
| `Task_Control` | 5 | `100Hz` (`10ms`) | 串级 PID + 深度控制 + TAM 分配 + 推进器 PWM 输出 + RT 状态回传 |
| `Task_IMU` | 4 | `50Hz` (`20ms`) | 双 IMU DMA 轮询解析、坐标转换、融合、姿态入池 |
| `Task_RT_Comm` | 4 | 事件驱动 | UART3 RT 帧接收队列，协议分发 |
| `Task_MS5837` | 3 | 主循环 `100Hz` | 温压状态机（深度有效更新约 `50Hz`） |
| `Task_NRT_Comm` | 3 | 事件驱动 | UART4 NRT 帧接收队列，协议分发 |
| `Task_Power` | 2 | `5000ms` | 电压电流采样入池 |
| `Task_DHT11` | 2 | `2000ms` | 舱内温湿度、漏水辅助判定 |
| `Task_Monitor` | 1 | `1000ms` | 心跳监控、喂狗门控、系统状态回传 |
| `LogTx`（日志任务） | 1 | 队列阻塞 | 日志帧经 UART4 上报 |

注：FreeRTOS 软件定时器任务优先级配置为 `configMAX_PRIORITIES - 1`，高于业务任务。

### 4.2 命令处理与任务关系

- `Task_Comm` 只做“收包搬运 + 协议分发”。
- 具体命令逻辑不在 `Task_Comm` 内，而是注册到协议引擎：
  - `Task_RT_Cmd_Init()`：注册实时控制命令（`DATA_TYPE_THRUSTER`）
  - `Task_NRT_Cmd_Init()`：注册 NRT 管理命令（模式切换、PID/TAM 参数、OTA、舵机等）

## 5. 数据模型与并发模型

数据池位于 `System/sys_data_pool.*`，核心对象：

- `bot_body_state_t`：姿态/角速度/加速度/深度
- `bot_sys_state_t`：环境、电源、CPU/温度、故障位、任务心跳
- `bot_target_t`：上位机下发目标（MANUAL/STABILIZE/AUTO）
- `bot_params_t`：模式、PID、failsafe 参数、TAM 矩阵
- `bot_actuator_state_t`：舵机/灯光状态

并发策略：

- 对外统一 `Push/Pull` API，不直接暴露全局对象
- 临界区保护结构体拷贝，任务使用“快照副本”计算
- 各任务通过 `Bot_Task_CheckIn_Monitor()` 进行心跳打卡

## 6. 通信与协议链路

### 6.1 串口与通道

- `USART1/2`：IMU DMA 环形接收
- `USART3`：RT 控制与高频状态回传
- `UART4`：NRT 管理、日志、系统状态、OTA 触发

默认初始化中：

- IMU 串口：`115200`
- OPI RT/NRT：`921600`

### 6.2 协议引擎

协议实现：`Driver/driver_hydrocore.*`

- 帧头尾：`AA BB ... CC DD`
- 校验：`XOR(cmd_id + payload_len + payload)`
- 注册表：`s_handlers[256]`
- 路由入口：`Driver_Protocol_Dispatch()`

常见命令字：

- `0x01`：运动控制
- `0x10`：OTA
- `0x11`：PID 参数更新
- `0x12`：系统模式切换
- `0x13`：运动模式切换
- `0x14`：舵机
- `0x15`：TAM 矩阵
- `0xFF`：ACK

## 7. 控制主链（从命令到电机）

RT 控制链路如下：

1. 上位机发 `DATA_TYPE_THRUSTER` 到 `USART3`
2. DMA + IDLE 触发回调，包指针入 RT 队列
3. `Task_RT_Comm` 取包后调用 `Driver_Protocol_Dispatch`
4. `On_Motion_Ctrl_Received` 解析后写入 `Bot_Target_Push`
5. `Task_Control` 每 `10ms` 拉取状态/目标/参数快照
6. 按系统模式与运动模式执行开环/闭环控制
7. TAM 分配得到 6 路推进器输出，调用 `Driver_Thruster_SetSpeed`

## 8. 安全与监控机制

### 8.1 模式门控

- 系统模式：`STANDBY / ACTIVE_DISARMED / MOTION_ARMED`
- 运动模式：`MANUAL / STABILIZE / AUTO`

解锁到 `MOTION_ARMED` 时，至少会检查：

- 漏水标志
- IMU 错误标志
- 电压阈值（低压拒绝解锁）

### 8.2 任务健康与看门狗

- 监控任务周期检查各任务最近心跳 tick 是否超时
- 仅在“全部任务健康”时喂硬件看门狗
- 监控任务同时通过 NRT 通道发送系统状态与执行器状态

## 9. 参数持久化与 OTA

### 9.1 参数持久化

参数驱动：`Driver/driver_param.*`

- 存储地址：`0x080E0000`（Sector 11）
- 结构：`magic + version + payload + checksum`
- 存储内容：PID、模式、failsafe、TAM 等

### 9.2 OTA 入口（APP 侧）

- NRT 收到 OTA 命令后写 `BootFlag.enter_bootloader = 1`
- ACK 后软件复位
- 后续升级流程由 bootloader 侧完成

当前工程使用的是 `System/sys_boot_flag.*` 版本的 BootFlag 接口。

## 10. Flash 分区与链接脚本

可见分区信息（代码与文档综合）：

- `BootFlag`：`0x08004000`（Sector 1）
- `APP1`：`0x08008000`，大小 `0x18000`（`System/scatter/app1.sct`）
- `APP2`：`0x08020000`，大小 `0x20000`（`System/scatter/app2.sct`）
- `Param`：`0x080E0000`（Sector 11）

## 11. 协作开发注意事项（给 Agent）

1. 仓库中存在历史残留文件 `Task/task_imu_parse.c`（整文件注释），当前运行逻辑在 `task_sensor.c`。
2. `Boot/boot_flag.*` 与 `System/sys_boot_flag.*` 功能重叠，主工程当前链接的是 `System` 版本。
3. 旧文档部分频率描述与代码不一致（例如监控任务实际为 `1Hz`），二次分析优先以代码为准。
4. 若调整任务频率/优先级，需同步检查：
   - 监控超时阈值
   - 通信队列长度与丢包计数
   - CPU 占用与日志通道压力

## 12. 推荐阅读顺序（新 Agent 上手）

1. `User/main.c`（启动总览）
2. `System/sys_data_pool.h` + `System/sys_data_pool.c`（数据模型与并发边界）
3. `Task/task_control.c`（控制闭环核心）
4. `Task/task_comm.c` + `Task/task_rt_cmd.c` + `Task/task_nrt_cmd.c`（通信与命令路径）
5. `Driver/driver_hydrocore.c`（协议引擎）
6. `Task/task_monitor.c` + `System/sys_monitor.c`（健康监控）
7. `Driver/driver_param.c` + `System/sys_boot_flag.c`（持久化与 OTA 入口）

