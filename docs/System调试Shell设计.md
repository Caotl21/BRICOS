# System 调试 Shell 设计（V1）

> 目标：在不破坏当前实时控制链路的前提下，为 STM32 下位机提供可观测、可诊断、可控的调试入口。  
> 适用场景：岸上联调、参数核对、模式联动验证、故障定位。

---

## 1. 设计目标与边界

### 1.1 目标

- 提供统一调试入口，替代零散调试指令。
- 与现有 `System/Task/Driver` 架构兼容，最小侵入。
- 默认安全：高风险命令在 `ARMED/FAILSAFE` 下受限。
- 响应可控：不阻塞 RT 通信与控制主循环。

### 1.2 非目标

- 不替代上位机业务协议（视觉、导航、任务规划）。
- 不做复杂脚本解释器（V1 仅做轻量命令分发）。
- 不直接修改底层寄存器（避免绕过 Driver/BSP 边界）。

---

## 2. 总体架构

建议新增模块：

- `System/sys_shell.h/.c`：Shell 核心（解析、注册表、权限校验、执行框架）
- `Task/task_shell.h/.c`：Shell 执行任务（队列消费、响应上报）
- `Task/task_nrt_cmd.c`：新增 Shell 协议入口回调（仅搬运，不做重逻辑）

数据流：

1. 3588 通过 NRT 协议发送 Shell 文本命令。
2. `Task_NRT_Cmd` 的 Shell 回调将命令压入 `Task_Shell` 队列。
3. `Task_Shell` 解析命令、执行 handler、输出文本响应。
4. 响应通过 NRT 协议帧回传。

关键原则：

- `Task_Comm/Protocol` 只做收发与分发。
- 复杂逻辑放在 `Task_Shell`，避免占用通信线程。
- 业务动作仍调用现有 `System_ModeManager`、`sys_data_pool`、`Driver` API。

接口草图参考：

- [docs/Shell接口草图（可切换独立串口）.md](Shell接口草图（可切换独立串口）.md)

---

## 3. 协议设计（基于现有 HydroCore）

建议新增命令字：

- `DATA_TYPE_SHELL_REQ  = 0x20`
- `DATA_TYPE_SHELL_RESP = 0x21`

### 3.1 请求载荷（建议）

- `session_id` (1B)：会话编号
- `seq` (1B)：请求序号
- `cmd_len` (1B)：命令长度（建议 <= 96）
- `cmd_ascii` (N B)：ASCII 命令文本

### 3.2 响应载荷（建议）

- `session_id` (1B)
- `seq` (1B)
- `ret_code` (1B)：0=OK，非0=错误
- `more` (1B)：0=最后一帧，1=后续还有
- `text_len` (1B)
- `text_ascii` (N B)

说明：

- 长文本分包返回，便于 MCU 小缓冲运行。
- 兼容现有 ACK 机制：请求先 ACK，执行结果走 RESP。

---

## 4. 命令模型

采用“命令注册表 + handler”：

- 命令名：`status`、`mode get` 等
- 参数规则：最小/最大参数数
- 权限标签：`READONLY`、`SAFE_CTRL`、`DANGEROUS`
- 模式约束：允许在哪些系统模式执行
- handler 输出：写入统一 `shell_out()` 缓冲

建议结构（概念）：

- `name`
- `help`
- `permission_mask`
- `allowed_sys_mode_mask`
- `handler(argc, argv, out)`

---

## 5. 命令集（V1 推荐）

### 5.1 基础命令（只读）

- `help`：列出命令
- `ver`：固件版本、构建时间、Git 短哈希
- `uptime`：运行时长
- `status`：模式、运动模式、故障位摘要
- `state body`：姿态/深度快照
- `state sys`：电压、电流、温湿度、漏水、CPU、芯片温度
- `tasks`：心跳时间差、任务健康状态
- `fault`：故障位与解释
- `log level [error|warn|info|debug]`：查看/设置日志等级

### 5.2 安全控制命令（受限）

- `mode get`
- `mode set standby|disarmed|armed`（内部调用 `System_ModeManager_RequestSysMode`）
- `motion get`
- `motion set manual|stabilize|auto`
- `failsafe enter <mask>`（仅调试构建开放）
- `reboot`

### 5.3 调试专用命令（建议 STANDBY 下开放）

- `standby kick now`：立即触发一次防啸叫脉冲
- `standby kick cfg <interval_ms> <duration_ms> <speed>`：运行时调参（仅 RAM）
- `sensors pause|resume`：联调用开关（默认不对外开放）

---

## 6. 权限与模式约束

默认策略：

- `STANDBY`：允许只读 + 调试命令 + 安全模式切换
- `DISARMED`：允许只读 + 常规控制命令
- `ARMED`：仅允许只读；禁止高风险写操作
- `FAILSAFE`：仅允许只读 + 受控恢复相关命令

可选增强：

- `auth <token>` 解锁高风险命令，限时 5 分钟自动失效。
- 编译开关 `SHELL_ENABLE_DANGEROUS_CMDS`，发布版默认关闭。

---

## 7. 实时性与资源预算

### 7.1 任务模型

- 新建 `Task_Shell`（建议优先级 2~3，低于控制与 RT 通信）
- 队列深度建议 4~8（防突发）
- 单条命令最大长度建议 96B

### 7.2 资源预算（F407）

- Shell RX 缓冲：128B
- Shell TX 缓冲：256B（分包输出）
- 任务栈：768~1024 words（根据 `printf` 使用量调优）

### 7.3 防阻塞策略

- handler 禁止长阻塞（>10ms）
- 长操作分阶段执行并异步回报
- 统一超时保护（如 200ms）

---

## 8. 错误码与可观测性

建议 `ret_code`：

- `0x00 OK`
- `0x01 UNKNOWN_CMD`
- `0x02 BAD_ARGS`
- `0x03 PERMISSION_DENIED`
- `0x04 MODE_BLOCKED`
- `0x05 BUSY`
- `0x06 INTERNAL_ERROR`

可观测性要求：

- 每条写操作命令记一条审计日志：`who/when/cmd/result`
- 关键状态切换命令写 `LOG_INFO`
- 错误返回同时给出可读文本

---

## 9. 与当前工程的对齐建议

- 与现有 `Task_NRT_Cmd` 一致：协议入口回调里仅做参数校验 + 入队。
- 模式切换必须走 `System_ModeManager_*`，不允许 Shell 直接改数据池字段。
- 与 `Task_Control` 现有 FSM 对齐：Shell 只“请求模式”，不直接驱动推进器。
- STANDBY 调试动作（如 kick 调参）只在 STANDBY 执行，防止 ARMED 误动作。

---

## 10. 分阶段落地计划

### 阶段 A（1~2 天）

- 打通 `SHELL_REQ/SHELL_RESP` 通道
- 实现 `help/ver/status/mode get`
- 建立命令注册表框架

### 阶段 B（2~3 天）

- 实现 `mode set`、`motion set`、`tasks/fault/log level`
- 增加权限与模式约束
- 增加命令审计日志

### 阶段 C（按需）

- 增加 `standby kick` 调试命令
- 增加 `auth` 机制与危险命令编译开关
- 增加脚本批量执行（可选）

---

## 11. 验收清单

- 在 STANDBY 下可稳定执行 50+ 次只读命令，无卡死。
- 在 ARMED 下危险命令被拒绝，返回明确错误码。
- Shell 压测下 `Task_Control` 周期抖动不显著增大。
- 模式切换命令与现有 NRT 命令行为一致。
- 断链/非法包/超长命令不会导致系统异常。
