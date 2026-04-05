# BRICOS ROS上下位机通信接口说明

本文档面向香橙派（ROS 节点）开发，按当前固件代码整理上下位机串口协议接口。

- 基准代码目录：`Bricsbot-Motion-Controller`
- 适用对象：ROS1/ROS2 串口收发、解包、指令下发节点
- 说明：以“当前实现”为准，不一致处已在文末标注

---

## 1. 通道与串口参数

- RT 通道：`USART3`（`BSP_UART_OPI_RT`）
  - 主要用于高实时状态回传与运动控制指令
- NRT 通道：`UART4`（`BSP_UART_OPI_NRT`）
  - 主要用于系统状态、执行器状态、日志、配置命令
- 串口统一参数：`115200-8N1`

---

## 2. 通用帧格式

两条通道都使用同一帧格式：

```text
| 0xAA | 0xBB | CmdId(1B) | Len(1B) | Payload(Len bytes) | Checksum(1B) | 0xCC | 0xDD |
```

- `TotalLen = Len + 7`
- `Checksum = XOR(CmdId, Len, Payload[0..Len-1])`

建议解包流程：

1. 搜索帧头 `0xAA 0xBB`
2. 读取 `CmdId` 与 `Len`
3. 校验总长度、帧尾 `0xCC 0xDD`
4. 计算并比对 XOR
5. 按 `CmdId` 分发

---

## 3. 命令字总表

| CmdId | 方向 | 通道 | 名称 |
| --- | --- | --- | --- |
| `0x01` | 上位机 -> MCU | RT | `DATA_TYPE_THRUSTER` |
| `0x02` | MCU -> 上位机 | RT | `DATA_TYPE_STATE_BODY` |
| `0x03` | MCU -> 上位机 | NRT | `DATA_TYPE_STATE_SYS` |
| `0x04` | MCU -> 上位机 | NRT | `DATA_TYPE_STATE_ACTUATOR` |
| `0x05` | MCU -> 上位机 | NRT | `DATA_TYPE_LOG` |
| `0x10` | 上位机 -> MCU | NRT | `DATA_TYPE_OTA` |
| `0x11` | 上位机 -> MCU | NRT | `DATA_TYPE_SET_PID_PARAM` |
| `0x12` | 上位机 -> MCU | NRT | `DATA_TYPE_SET_SYS_MODE` |
| `0x13` | 上位机 -> MCU | NRT | `DATA_TYPE_SET_MOTION_MODE` |
| `0x14` | 上位机 -> MCU | NRT | `DATA_TYPE_SET_SERVO` |
| `0x15` | 上位机 -> MCU | NRT | `DATA_TYPE_SET_TAM` |

---

## 4. 上行数据（MCU -> ROS）

### 4.1 `0x02 DATA_TYPE_STATE_BODY`（RT）

- Payload 长度：`52` 字节（`13 * float32 little-endian`）
- 发送频率：约 `100Hz`

| 偏移 | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 0 | `roll` | `float32_le` | 横滚角（度） |
| 4 | `pitch` | `float32_le` | 俯仰角（度） |
| 8 | `yaw` | `float32_le` | 偏航角（度） |
| 12 | `gyro_x` | `float32_le` | X 角速度 |
| 16 | `gyro_y` | `float32_le` | Y 角速度 |
| 20 | `gyro_z` | `float32_le` | Z 角速度 |
| 24 | `vel_x` | `float32_le` | X 速度 |
| 28 | `vel_y` | `float32_le` | Y 速度 |
| 32 | `vel_z` | `float32_le` | Z 速度 |
| 36 | `acc_x` | `float32_le` | X 加速度 |
| 40 | `acc_y` | `float32_le` | Y 加速度 |
| 44 | `acc_z` | `float32_le` | Z 加速度 |
| 48 | `depth_m` | `float32_le` | 深度（m） |

### 4.2 `0x03 DATA_TYPE_STATE_SYS`（NRT）

- Payload 长度：`33` 字节
- 发送频率：`MONITOR_PERIOD_MS = 1000`（约 `1Hz`）

| 偏移 | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 0 | `sys_mode` | `uint8` | 系统模式 |
| 1 | `motion_mode` | `uint8` | 运动模式 |
| 2 | `water_temp_c` | `float32_le` | 外水温 |
| 6 | `cabin_temp_c` | `float32_le` | 舱温 |
| 10 | `cabin_humi` | `float32_le` | 舱湿 |
| 14 | `bat_voltage_v` | `float32_le` | 电压 |
| 18 | `bat_current_a` | `float32_le` | 电流 |
| 22 | `chip_temp` | `float32_le` | 芯片温度（当前实现是 `摄氏度*100`） |
| 26 | `cpu_usage` | `float32_le` | CPU 使用率（0~100） |
| 30 | `is_leak_detected` | `uint8` | 漏水标志（0/1） |
| 31 | `is_imu_error` | `uint8` | IMU 异常（0/1） |
| 32 | `is_voltage_error` | `uint8` | 低压标志（0/1） |

### 4.3 `0x04 DATA_TYPE_STATE_ACTUATOR`（NRT）

- Payload 长度：`3` 字节

| 偏移 | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 0 | `servo_angle` | `uint8` | 舵机角度（0~180） |
| 1 | `light1_pwm` | `uint8` | 探照灯1亮度（0~100） |
| 2 | `light2_pwm` | `uint8` | 探照灯2亮度（0~100） |

### 4.4 `0x05 DATA_TYPE_LOG`（NRT）

- Payload 为日志文本（ASCII/UTF-8）
- 文本末尾带 `\r\n`
- 当前最大 payload 约 `191` 字节

---

## 5. 下行命令（ROS -> MCU）

### 5.1 `0x01 DATA_TYPE_THRUSTER`（RT）

- Payload 长度：`17` 字节
- 格式：`target_mode(1B) + 4 * float32_le`

| `target_mode` | `float0` | `float1` | `float2` | `float3` |
| --- | --- | --- | --- | --- |
| `0` (`MANUAL`) | `surge` | `sway` | `heave` | `yaw_cmd` |
| `1` (`STABILIZE`) | `surge` | `sway` | `target_depth` | `target_yaw` |
| `2` (`AUTO`) | `surge` | `sway` | `target_depth` | `target_yaw` |

### 5.2 `0x10 DATA_TYPE_OTA`（NRT）

- Payload 固定 4 字节：`DE AD BE EF`
- 收到后进入 Bootloader 并复位

### 5.3 `0x11 DATA_TYPE_SET_PID_PARAM`（NRT）

- Payload 长度：`140` 字节（`7组 * 5float`）
- 单组 5float 顺序：`kp, ki, kd, integral_max, output_max`
- 7组顺序：
  - `roll.outer`
  - `roll.inner`
  - `pitch.outer`
  - `pitch.inner`
  - `yaw.outer`
  - `yaw.inner`
  - `depth`
- 收到后保存参数并复位

### 5.4 `0x12 DATA_TYPE_SET_SYS_MODE`（NRT）

- Payload 长度：`1` 字节
- 取值：
  - `0` = `SYS_MODE_STANDBY`
  - `1` = `SYS_MODE_ACTIVE_DISARMED`
  - `2` = `SYS_MODE_MOTION_ARMED`

### 5.5 `0x13 DATA_TYPE_SET_MOTION_MODE`（NRT）

- Payload 长度：`1` 字节
- 取值：
  - `0` = `MOTION_STATE_MANUAL`
  - `1` = `MOTION_STATE_STABILIZE`
  - `2` = `MOTION_STATE_AUTO`

### 5.6 `0x14 DATA_TYPE_SET_SERVO`（NRT）

- Payload 长度：`1` 字节
- `servo_angle` 范围 `0~180`（超范围会在底层限幅）

### 5.7 `0x15 DATA_TYPE_SET_TAM`（NRT）

- Payload 格式：`thruster_count(1B) + matrix_data`
- 约束：`thruster_count` 范围 `1~8`
- 矩阵长度：`thruster_count * 6 * sizeof(float)` 字节
- 每个推进器 6 个自由度系数顺序：
  - `force_x, force_y, force_z, torque_x, torque_y, torque_z`
- 收到后保存参数并复位

---

## 6. ROS 节点对接建议

建议至少拆成两个串口节点或一个双端口节点：

- RT 端口节点（`/dev/ttyS_RT`）
  - 订阅：`/bricsbot/cmd/thruster_raw` -> 发送 `0x01`
  - 发布：`/bricsbot/state/body` <- 接收 `0x02`

- NRT 端口节点（`/dev/ttyS_NRT`）
  - 订阅：模式切换/PID/TAM/舵机/OTA 命令 -> 发送 `0x10~0x15`
  - 发布：`/bricsbot/state/sys`（`0x03`）
  - 发布：`/bricsbot/state/actuator`（`0x04`）
  - 发布：`/bricsbot/log/text`（`0x05`）

解包实现要点：

1. 每个串口维护独立环形缓冲区
2. 从缓冲区持续扫描帧头，不假设“读一次就是一帧”
3. 做严格长度和 XOR 校验，校验失败丢弃并重同步
4. 按 `CmdId` 路由到不同解析器

---

## 7. 已知实现注意事项（建议先知悉）

1. `0x01 DATA_TYPE_THRUSTER` 回调虽已实现，但 `main` 当前未调用 `Task_RT_Cmd_Init()`，即默认不会注册该命令处理器。  
2. 协议层无 ACK/NAK 返回帧。命令是否生效需靠状态回读与行为观测。  
3. 日志队列满时会静默丢日志（不是串口协议错误）。  
4. `chip_temp` 目前按 `温度 * 100` 作为 float 发出，ROS 侧如要摄氏度需再 `/100.0`。  
5. `SET_PID_PARAM`、`SET_TAM`、`OTA` 命令都会触发 MCU 复位，ROS 侧要做自动重连。  

---

## 8. 版本建议

如果后续协议字段发生变化，建议同步更新：

1. 命令字表（CmdId）
2. 每个消息的 payload 长度与字段偏移
3. ROS 消息定义与解包单元测试用例

