# BRICOS ROS 上下位机通信接口说明

本文档面向 ROS 侧串口节点开发，按当前固件实现整理上下位机通信接口。

- 基准代码目录：`Bricsbot-Motion-Controller`
- 协议来源：`Driver/driver_hydrocore.h`、`Task/task_rt_cmd.c`、`Task/task_nrt_cmd.c`、`Task/task_control.c`、`Task/task_monitor.c`
- 字节序：除特别说明外，多字节数值均为 little-endian
- 帧校验：所有协议帧使用 XOR checksum

---

## 1. 通道与串口

| 通道 | BSP 端口 | 典型硬件 | 波特率 | 用途 |
| --- | --- | --- | --- | --- |
| RT | `BSP_UART_OPI_RT` | `USART3` | `921600-8N1` | 运动控制下发、机体状态/推进器状态上报 |
| NRT | `BSP_UART_OPI_NRT` | `UART4` | `921600-8N1` | 系统状态、配置服务、ACK |
| LOG/Shell | `BSP_UART_DEBUG` | `UART5` | `1500000-8N1` | 日志文本、人工调试 Shell 字符流、启动提示 |

当前 `bsp_uart_init_default()` 中，RT/NRT 使用 `921600`，LOG/Shell 使用 `1500000`。

说明：

- RT/NRT 两条 OPI 通道均使用本文第 2 节的二进制帧格式。
- UART5 LOG/Shell 当前是独立字符流通道，启动后会输出 `startup_success\r\n`，不属于 ROS 二进制协议帧。
- NRT 与 LOG/Shell 已分离；ROS 侧不要从 NRT 通道等待日志文本。
- `tools/shell/shell_nrt_client.py` 中的 `0x20~0x23` 是客户端侧 shell-frame 方案；当前固件主路径仍绑定 UART5 字符流 Shell。

---

## 2. 通用帧格式

RT 和 NRT 均使用同一帧格式：

```text
| 0xAA | 0xBB | CmdId(1B) | Len(1B) | Payload(Len bytes) | Checksum(1B) | 0xCC | 0xDD |
```

计算规则：

```text
TotalLen = Len + 7
Checksum = XOR(CmdId, Len, Payload[0], ..., Payload[Len-1])
```

解包建议：

1. 连续扫描帧头 `0xAA 0xBB`。
2. 读取 `CmdId` 与 `Len`。
3. 等待 `Len + 7` 字节完整帧。
4. 校验帧尾 `0xCC 0xDD`。
5. 计算并比对 XOR checksum。
6. 根据 `CmdId` 路由到对应解析器。

注意：串口 DMA 接收可能一次给出半帧、多帧或夹杂噪声，ROS 侧不要假设“一次 read 等于一帧”。

---

## 3. 命令号总表

| CmdId | 方向 | 通道 | 名称 |
| --- | --- | --- | --- |
| `0x01` | ROS -> MCU | RT | `DATA_TYPE_THRUSTER` |
| `0x02` | MCU -> ROS | RT | `DATA_TYPE_STATE_BODY` |
| `0x03` | MCU -> ROS | NRT | `DATA_TYPE_STATE_SYS` |
| `0x04` | MCU -> ROS | NRT | `DATA_TYPE_STATE_ACTUATOR` |
| `0x05` | MCU -> ROS | NRT | `DATA_TYPE_LOG`，保留定义，当前固件未使用 |
| `0x06` | MCU -> ROS | RT | `DATA_TYPE_STATE_THRUSTER` |
| `0x07` | MCU -> ROS | NRT | `DATA_TYPE_STATE_STACK_WM` |
| `0x10` | ROS -> MCU | NRT | `DATA_TYPE_OTA` |
| `0x11` | ROS -> MCU | NRT | `DATA_TYPE_SET_PID_PARAM` |
| `0x12` | ROS -> MCU | NRT | `DATA_TYPE_SET_SYS_MODE` |
| `0x13` | ROS -> MCU | NRT | `DATA_TYPE_SET_MOTION_MODE` |
| `0x14` | ROS -> MCU | NRT | `DATA_TYPE_SET_SERVO` |
| `0x15` | ROS -> MCU | NRT | `DATA_TYPE_SET_TAM` |
| `0x16` | ROS -> MCU / MCU -> ROS | NRT | `DATA_TYPE_READ_PID_PARAM` |
| `0x17` | ROS -> MCU / MCU -> ROS | NRT | `DATA_TYPE_READ_TAM` |
| `0x18` | ROS -> MCU | NRT | `DATA_TYPE_SET_WS2812_COLOR` |
| `0x19` | ROS -> MCU | NRT | `DATA_TYPE_CALIBRATE_IMU_ACC` |
| `0x1A` | ROS -> MCU | NRT | `DATA_TYPE_CLEAR_PERSIST_LOG` |
| `0xFF` | MCU -> ROS | NRT | `DATA_TYPE_CMD_ACK` |

ACK 状态码：

| Code | 名称 | 含义 |
| --- | --- | --- |
| `0x01` | `ACK_SUCCESS` | 执行成功或已受理 |
| `0x02` | `INVALID_PARAM` | 参数非法 |
| `0x03` | `UNKNOWN_CMD` | 命令未注册 |
| `0x04` | `LENGTH_ERROR` | Payload 长度不符 |
| `0x05` | `EXECUTION_FAILED` | 底层执行失败，例如 Flash 擦除失败 |

ACK 帧：

- `CmdId = 0xFF`
- Payload 固定 4 字节

| Offset | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 0 | `cmd_ack` | `uint8` | 固定 `0xFF` |
| 1 | `cmd_id` | `uint8` | 被 ACK 的原命令号 |
| 2 | `ack_code` | `uint8` | ACK 状态码 |
| 3 | `seq_number` | `uint8` | 当前实现固定传 `0` |

---

## 4. MCU -> ROS 上行帧

### 4.1 `0x02 DATA_TYPE_STATE_BODY` RT

- 来源：`Task_Control`
- Payload 长度：`56` 字节，`14 * float32_le`
- 当前上报内容是四元数，不是欧拉角

| Offset | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 0 | `q0` | `float32_le` | 四元数 0 |
| 4 | `q1` | `float32_le` | 四元数 1 |
| 8 | `q2` | `float32_le` | 四元数 2 |
| 12 | `q3` | `float32_le` | 四元数 3 |
| 16 | `gyro_x` | `float32_le` | X 轴角速度 |
| 20 | `gyro_y` | `float32_le` | Y 轴角速度 |
| 24 | `gyro_z` | `float32_le` | Z 轴角速度 |
| 28 | `vel_x` | `float32_le` | X 轴速度 |
| 32 | `vel_y` | `float32_le` | Y 轴速度 |
| 36 | `vel_z` | `float32_le` | Z 轴速度 |
| 40 | `acc_x` | `float32_le` | X 轴加速度 |
| 44 | `acc_y` | `float32_le` | Y 轴加速度 |
| 48 | `acc_z` | `float32_le` | Z 轴加速度 |
| 52 | `depth_m` | `float32_le` | 深度，单位 m |

### 4.2 `0x06 DATA_TYPE_STATE_THRUSTER` RT

- 来源：`Task_Control`
- Payload 长度：`18` 字节，`9 * uint16_le`
- 前 6 个字段是当前推进器 PWM，后 3 个字段是推进器 PWM 限幅常量

| Offset | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 0 | `thruster_1_us` | `uint16_le` | 推进器 1 PWM |
| 2 | `thruster_2_us` | `uint16_le` | 推进器 2 PWM |
| 4 | `thruster_3_us` | `uint16_le` | 推进器 3 PWM |
| 6 | `thruster_4_us` | `uint16_le` | 推进器 4 PWM |
| 8 | `thruster_5_us` | `uint16_le` | 推进器 5 PWM |
| 10 | `thruster_6_us` | `uint16_le` | 推进器 6 PWM |
| 12 | `pwm_stop_us` | `uint16_le` | 停止 PWM |
| 14 | `pwm_max_fwd_us` | `uint16_le` | 最大正向 PWM |
| 16 | `pwm_max_rev_us` | `uint16_le` | 最大反向 PWM |

### 4.3 `0x03 DATA_TYPE_STATE_SYS` NRT

- 来源：`Task_Monitor`
- Payload 长度：`33` 字节
- 周期：`MONITOR_PERIOD_MS`，当前约 1 Hz

| Offset | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 0 | `sys_mode` | `uint8` | 系统模式 |
| 1 | `motion_mode` | `uint8` | 运动模式 |
| 2 | `water_temp_c` | `float32_le` | 水温 |
| 6 | `cabin_temp_c` | `float32_le` | 舱内温度 |
| 10 | `cabin_humi` | `float32_le` | 舱内湿度 |
| 14 | `bat_voltage_v` | `float32_le` | 电池电压 |
| 18 | `bat_current_a` | `float32_le` | 电池电流 |
| 22 | `chip_temp` | `float32_le` | 芯片温度 |
| 26 | `cpu_usage` | `float32_le` | CPU 使用率，0~100 |
| 30 | `is_leak_detected` | `uint8` | 漏水标志 |
| 31 | `is_imu_error` | `uint8` | IMU 异常标志 |
| 32 | `is_voltage_error` | `uint8` | 低压标志，按 `failsafe_low_voltage` 判断 |

系统模式枚举：

| Value | 名称 |
| --- | --- |
| `0` | `SYS_MODE_STANDBY` |
| `1` | `SYS_MODE_ACTIVE_DISARMED` |
| `2` | `SYS_MODE_MOTION_ARMED` |
| `3` | `SYS_MODE_FAILSAFE` |

运动模式枚举：

| Value | 名称 |
| --- | --- |
| `0` | `MOTION_STATE_MANUAL` |
| `1` | `MOTION_STATE_STABILIZE` |
| `2` | `MOTION_STATE_AUTO` |

### 4.4 `0x04 DATA_TYPE_STATE_ACTUATOR` NRT

- 来源：`Task_Monitor`
- Payload 长度：`3` 字节

| Offset | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 0 | `servo_angle` | `uint8` | 舵机角度，0~180 |
| 1 | `light1_pwm` | `uint8` | 探照灯 1 亮度，0~100 |
| 2 | `light2_pwm` | `uint8` | 探照灯 2 亮度，0~100 |

### 4.5 `0x07 DATA_TYPE_STATE_STACK_WM` NRT

- 来源：`Task_Monitor`
- Payload 长度：`18` 字节，`9 * uint16_le`
- 单位：FreeRTOS stack high-water mark，通常为 word 数，具体取决于 FreeRTOS 端口定义

| Offset | 字段 | 类型 |
| --- | --- | --- |
| 0 | `monitor_task` | `uint16_le` |
| 2 | `control_task` | `uint16_le` |
| 4 | `rt_comm_task` | `uint16_le` |
| 6 | `nrt_comm_task` | `uint16_le` |
| 8 | `imu_task` | `uint16_le` |
| 10 | `ms5837_task` | `uint16_le` |
| 12 | `power_task` | `uint16_le` |
| 14 | `dht11_task` | `uint16_le` |
| 16 | `log_task` | `uint16_le` |

### 4.6 `0x05 DATA_TYPE_LOG` NRT

- 该命令号为保留定义，当前固件没有通过 NRT 协议帧主动发送 `0x05`
- 当前实时日志与持久化日志 replay 均从 UART5 LOG/Shell 口直接输出文本
- 若后续恢复 NRT 日志帧，建议 payload 使用 ASCII/UTF-8 文本并保留 `\r\n` 结尾

---

## 5. ROS -> MCU 下行命令

### 5.1 `0x01 DATA_TYPE_THRUSTER` RT

- Payload 长度：`17` 字节
- 格式：`target_mode(1B) + 4 * float32_le`
- 当前 `main.c` 已调用 `Task_RT_Cmd_Init()`，该命令会注册并生效

| `target_mode` | float0 | float1 | float2 | float3 |
| --- | --- | --- | --- | --- |
| `0` manual | `surge` | `sway` | `heave` | `yaw_cmd` |
| `1` stabilize | `surge` | `sway` | `target_depth` | `target_yaw` |
| `2` auto | `surge` | `sway` | `target_depth` | `target_yaw` |

该命令当前没有 ACK，ROS 侧应通过状态反馈判断是否生效。

### 5.2 `0x10 DATA_TYPE_OTA` NRT

- Payload 长度：`4` 字节
- 固定内容：`DE AD BE EF`
- 成功 ACK 后请求进入 Bootloader 并复位

ACK：

- 成功：`ACK_SUCCESS`
- 魔数错误：`INVALID_PARAM`

请求帧示例：

```text
AA BB 10 04 DE AD BE EF 58 CC DD
```

### 5.3 `0x11 DATA_TYPE_SET_PID_PARAM` NRT

- Payload 长度：`140` 字节
- 格式：`7 * (kp, ki, kd, integral_max, output_max)`，每个值为 `float32_le`
- 7 组顺序：
  - `roll.outer`
  - `roll.inner`
  - `pitch.outer`
  - `pitch.inner`
  - `yaw.outer`
  - `yaw.inner`
  - `depth`
- 成功写入 Flash 后复位

ACK：

- 成功：`ACK_SUCCESS`
- 长度不符：`LENGTH_ERROR`

### 5.4 `0x16 DATA_TYPE_READ_PID_PARAM` NRT

- 请求 Payload 长度：当前实现不校验，建议发送 `0`
- MCU 先回 ACK，再用 `CmdId=0x16` 返回当前 PID 参数
- 返回 Payload 长度：`140` 字节，格式同 `SET_PID_PARAM`

ACK：

- 成功：`ACK_SUCCESS`

### 5.5 `0x12 DATA_TYPE_SET_SYS_MODE` NRT

- Payload 长度：`1` 字节
- 字段：`mode(uint8)`

| Value | 目标模式 |
| --- | --- |
| `0` | `SYS_MODE_STANDBY` |
| `1` | `SYS_MODE_ACTIVE_DISARMED` |
| `2` | `SYS_MODE_MOTION_ARMED` |

ACK：

- 成功：`ACK_SUCCESS`
- 长度不符：`LENGTH_ERROR`
- 非法值或模式切换被拒绝：`INVALID_PARAM`

### 5.6 `0x13 DATA_TYPE_SET_MOTION_MODE` NRT

- Payload 长度：`1` 字节
- 字段：`mode(uint8)`

| Value | 目标运动模式 |
| --- | --- |
| `0` | `MOTION_STATE_MANUAL` |
| `1` | `MOTION_STATE_STABILIZE` |
| `2` | `MOTION_STATE_AUTO` |

ACK：

- 成功：`ACK_SUCCESS`
- 长度不符：`LENGTH_ERROR`
- 非法值或模式切换被拒绝：`INVALID_PARAM`

### 5.7 `0x14 DATA_TYPE_SET_SERVO` NRT

- Payload 长度：`1` 字节
- 字段：`servo_angle(uint8)`，建议范围 `0~180`

ACK：

- 成功：`ACK_SUCCESS`
- 长度不符：`LENGTH_ERROR`

### 5.8 `0x15 DATA_TYPE_SET_TAM` NRT

- Payload 格式：`thruster_count(1B) + matrix_data`
- `thruster_count` 范围：`1~8`
- `matrix_data` 长度：`thruster_count * 6 * sizeof(float)`
- 每个推进器 6 自由度系数顺序：`force_x, force_y, force_z, torque_x, torque_y, torque_z`
- 总 Payload 长度：`1 + thruster_count * 24`
- 成功写入 Flash 后复位

ACK：

- 成功：`ACK_SUCCESS`
- 长度不符：`LENGTH_ERROR`
- `thruster_count` 非法：`INVALID_PARAM`

### 5.9 `0x17 DATA_TYPE_READ_TAM` NRT

- 请求 Payload 长度：当前实现不校验，建议发送 `0`
- MCU 先回 ACK，再用 `CmdId=0x17` 返回当前 TAM
- 返回 Payload 格式：`active_thrusters(1B) + active_thrusters * 6 * float32_le`

ACK：

- 成功：`ACK_SUCCESS`

### 5.10 `0x18 DATA_TYPE_SET_WS2812_COLOR` NRT

- Payload 长度：`4` 字节
- 格式：`strip(1B) + r(1B) + g(1B) + b(1B)`

| 字段 | 说明 |
| --- | --- |
| `strip` | `0` = 灯带 1，`1` = 灯带 2 |
| `r/g/b` | RGB 分量，0~255 |

ACK：

- 成功：`ACK_SUCCESS`
- 长度不符：`LENGTH_ERROR`

注意：当前 handler 没有检查 `strip` 是否越界，也没有检查 `Driver_WS2812_Refresh()` 返回值。ROS 侧建议只发送 `0` 或 `1`。

### 5.11 `0x19 DATA_TYPE_CALIBRATE_IMU_ACC` NRT

- Payload 长度：`0`
- 功能：触发 JY901S 加速度校准
- 成功 ACK 后复位

ACK：

- 成功：`ACK_SUCCESS`
- 长度不符：`LENGTH_ERROR`
- 底层校准启动失败：`INVALID_PARAM`

请求帧：

```text
AA BB 19 00 19 CC DD
```

### 5.12 `0x1A DATA_TYPE_CLEAR_PERSIST_LOG` NRT

- Payload 长度：`0`
- 功能：擦除 Flash 中的持久化 warning/error 日志
- 清理成功后不会复位

ACK：

- 成功：`ACK_SUCCESS`
- 长度不符：`LENGTH_ERROR`
- Flash 擦除失败：`EXECUTION_FAILED`

请求帧：

```text
AA BB 1A 00 1A CC DD
```

说明：

- 该命令调用 `System_Log_PersistClear()`，会擦除两个持久化日志 Flash bank。
- 清理后如果系统仍在持续产生 `LOG_ERROR/LOG_WARNING`，新的错误会再次写入持久化日志。

---

## 6. 持久化日志与实时日志

当前 `LOG_WARNING` 和 `LOG_ERROR` 会写入 Flash 持久化日志区；`LOG_INFO` 和 `LOG_DEBUG` 不持久化。

日志输出位置：

- 当前固件实际输出：UART5 LOG/Shell 字符流
- 当前 NRT 通道不承载日志文本
- 保留定义但当前未使用：NRT `0x05 DATA_TYPE_LOG` 协议帧

启动后，日志任务会 replay 最多 64 条持久化日志。replay 出来的文本会带前缀：

```text
[PERSIST] [ERROR] WS2812 init failed
```

本次运行中新产生的日志不带 `[PERSIST]`：

```text
[ERROR] WS2812 init failed
```

ROS 侧建议：

1. 将 `[PERSIST]` 日志作为“上次或更早运行留下的历史告警”处理。
2. 收到 `0x1A CLEAR_PERSIST_LOG` 的 `ACK_SUCCESS` 后，可在 UI 上清空历史告警列表。
3. 如果清理后又出现新的 `[PERSIST]`，说明清理之后系统又产生过 warning/error 并重启 replay。

---

## 7. ROS 节点对接建议

建议至少拆成两个串口节点，或一个节点内部维护两个独立串口状态机：

RT 节点：

- 发送：`0x01 DATA_TYPE_THRUSTER`
- 接收：`0x02 DATA_TYPE_STATE_BODY`
- 接收：`0x06 DATA_TYPE_STATE_THRUSTER`
- 特点：高频、低延迟，不依赖 ACK

NRT 节点：

- 接收：`0x03 DATA_TYPE_STATE_SYS`
- 接收：`0x04 DATA_TYPE_STATE_ACTUATOR`
- 接收：`0x07 DATA_TYPE_STATE_STACK_WM`
- 发送：`0x10~0x1A` 服务命令
- 接收：`0xFF DATA_TYPE_CMD_ACK`
- 特点：配置、诊断、日志、低频状态

实现要点：

1. RT/NRT 各自维护独立 ring buffer 和 parser。
2. 对所有下行 NRT 服务命令建立超时等待 ACK 机制。
3. 对会复位的命令，包括 `OTA`、`SET_PID_PARAM`、`SET_TAM`、`CALIBRATE_IMU_ACC`，ROS 侧需要自动重连。
4. 对 `READ_PID_PARAM` 和 `READ_TAM`，ACK 与数据帧是两帧，ROS 侧不要只等 ACK。
5. 如果 ROS 侧需要日志，当前应额外接入 UART5 LOG/Shell 字符流；不要依赖 NRT `0x05` 日志帧，除非固件后续恢复该发送路径。

---

## 8. 已知注意事项

1. `DATA_TYPE_CMD_ACK` 的 `seq_number` 当前固定为 `0`，固件尚未实现 ROS 请求序号透传。
2. `READ_PID_PARAM` 和 `READ_TAM` 当前不校验请求 payload 长度，ROS 侧建议统一发空 payload。
3. `SET_WS2812_COLOR` 当前未检查 strip 越界和刷新结果，ROS 侧应保证参数合法。
4. NRT 接收 DMA 缓冲区大小为 `180` 字节；当前 `SET_PID_PARAM` 最大 payload 为 `140` 字节，总帧长 `147` 字节，可放入。
5. `0x05 DATA_TYPE_LOG` 当前只是保留定义，固件日志实际走 UART5 LOG/Shell 字符流。
6. `chip_temp` 当前由 `System_Runtime_GetChipTemperature()` 推入，文档按摄氏度字段处理；如 ROS 侧发现值域异常，应以固件运行时校准为准。

---

## 9. 快速构帧示例

Python 构帧：

```python
def build_frame(cmd_id: int, payload: bytes = b"") -> bytes:
    if len(payload) > 255:
        raise ValueError("payload too long")
    body = bytes([cmd_id & 0xFF, len(payload) & 0xFF]) + payload
    checksum = 0
    for b in body:
        checksum ^= b
    return b"\xAA\xBB" + body + bytes([checksum]) + b"\xCC\xDD"

clear_persist_log = build_frame(0x1A)
calibrate_imu_acc = build_frame(0x19)
set_sys_disarmed = build_frame(0x12, bytes([1]))
```
