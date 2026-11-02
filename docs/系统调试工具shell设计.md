# 系统调试工具 Shell 设计

## 1. 定位

BRICOS Shell 是运行在 STM32 侧的系统调试工具，用于岸上联调、现场排障、硬件验证和安全状态检查。它面向人工调试和工程诊断，不替代 RT/NRT 上位机业务协议。

当前设计口径：

- Shell 与 LOG 共用 UART5 Debug 通道，波特率为 `1500000-8N1`。
- RT 通道为 UART3，NRT 通道为 UART4，二者不承载 Shell 文本交互。
- NRT 中曾规划的 `DATA_TYPE_SHELL_REQ/RESP 0x20/0x21` 不是当前固件主路径。
- `0x05 DATA_TYPE_LOG` 当前也只是保留定义，实时日志和持久化日志 replay 均从 UART5 输出。
- Shell 命令通过链接段自动注册，新增命令不需要改 `main.c` 或集中注册表。

这个设计的边界很重要：Debug 口可以“看”和“调”，但不能破坏实时控制链路；Shell 命令必须服从 SysMode、故障位和本地安全策略。

## 2. 当前通道

| 通道 | UART | 波特率 | 用途 |
| --- | --- | --- | --- |
| RT | UART3 | 921600 | 实时控制、高频状态 |
| NRT | UART4 | 921600 | 参数、服务、ACK、OTA、持久化日志清理等 |
| LOG/Shell | UART5 | 1500000 | 启动日志、实时日志、`[PERSIST]` replay、人工 Shell |

UART5 启动后会输出：

```text
startup_success
```

随后日志系统和 Shell 都在该字符流通道上工作。调试终端需要按普通串口 CLI 的方式发送 ASCII 命令，并以回车或换行结束。

## 3. 软件结构

```text
User/main.c
  └─ App_Shell_Init()
        ├─ Task_ShellTransportNRT_GetVTable()
        │     └─ UART5 Debug 字符流传输适配器
        ├─ System_ShellCore_Init()
        │     └─ 扫描 ShellCmd 链接段并注册命令
        └─ System_ShellCore_Start()
              └─ 注册 UART5 RX 回调，启动 ShellU5Rx 任务
```

核心文件职责：

| 文件 | 职责 |
| --- | --- |
| `User/shell/app_shell.c` | Shell 组装层，把 Shell Core 绑定到 UART5 Debug transport |
| `Task/task_shell_transport_nrt.c` | UART5 字符流 transport，负责 RX 队列、RX 任务和发送 |
| `System/sys_shell_core.c` | 命令解析、参数拆分、权限/模式检查、handler 分发、输出拼装 |
| `System/sys_shell_transport.h` | Shell 传输抽象，支持 NRT frame 和 UART stream 两种类型 |
| `System/sys_shell_export.h` | `EXPORT_SHELL_CMD` 自动注册宏 |
| `System/sys_shell_cfg.h` | 命令数、行长度、参数数、输出缓冲等容量配置 |
| `User/shell/core_cmd.c` | 基础命令：help、echo、sysmode、momode、fault、reboot |
| `User/shell/query_cmd.c` | 查询命令：euler、depthtemp、power、cabin、chip |
| `User/shell/actuator_cmd.c` | 执行机构调试命令：thruster、servo、ws2812 |

注意：`Task_ShellTransportNRT_GetVTable()` 名称里仍带 `NRT`，这是历史命名遗留；当前实现实际绑定的是 `BSP_UART_DEBUG`，也就是 UART5 Debug 字符流。

## 4. 运行链路

### 4.1 初始化链路

1. `main.c` 在创建通信、传感器、控制、NRT/RT 命令和监控任务后调用 `App_Shell_Init()`。
2. `App_Shell_Init()` 获取 Shell transport vtable。
3. `System_ShellCore_Init()` 初始化 Core，并调用 `System_ShellCore_RegisterBuiltins()`。
4. Core 扫描链接段中的命令描述符，注册所有 `EXPORT_SHELL_CMD` 导出的命令。
5. `System_ShellCore_Start()` 注册 UART5 接收回调，并打印 `startup_success`。
6. Shell transport 创建 `ShellU5Rx` 任务，逐字节把 UART5 输入交给 Shell Core。

### 4.2 输入链路

```text
UART5 RX ISR
  └─ prv_uart_debug_rx_isr()
        └─ xQueueSendFromISR()
              └─ ShellU5Rx
                    └─ System_ShellCore_OnRxBytes()
                          └─ 拼行、解析、执行命令
```

UART stream 模式下，Shell Core 会按字符增量拼接命令行；收到行结束符后执行。`SHELL_ASCII_ONLY=1` 时，只接受可打印 ASCII 字符，便于串口调试并避免异常字节污染命令行。

### 4.3 输出链路

命令 handler 使用：

```c
System_ShellCore_Printf(ctx, "text...");
```

Core 将输出写入命令上下文缓冲，命令结束后通过 transport 的 `send()` 发回 UART5。当前输出缓冲大小为 `SHELL_OUTPUT_BUF_SIZE = 256`。

## 5. 自动注册机制

Shell 命令采用“导出段自动注册”，避免在一个中心文件中手工维护命令表。

### 5.1 注册宏

命令文件包含：

```c
#include "sys_shell_export.h"
```

然后在文件末尾导出：

```c
EXPORT_SHELL_CMD("foo", "foo request", prv_cmd_foo, SHELL_PERM_READONLY, SHELL_MODE_ANY);
```

`EXPORT_SHELL_CMD` 会生成一个 `shell_cmd_desc_t` 常量，并放入 `ShellCmd$$Table$$M` 链接段。

### 5.2 链接段

scatter 文件需要保留 Shell 命令段：

- `System/scatter/app1.sct`
- `System/scatter/app2.sct`

段顺序应包含：

```text
*(ShellCmd$$Base)
*(ShellCmd$$Table)
*(ShellCmd$$Limit)
```

Core 初始化时从 `ShellCmd$$Base + 1` 扫描到 `ShellCmd$$Limit`，逐条调用 `System_ShellCore_Register()`。

### 5.3 新增命令流程

1. 在 `User/shell/` 下选择合适文件，或新增一个业务命令文件。
2. 实现 `static shell_ret_t prv_cmd_xxx(shell_cmd_ctx_t *ctx, int argc, char **argv)`。
3. 用 `System_ShellCore_Printf()` 输出响应，不要直接 `printf` 或直接写 UART。
4. 用 `EXPORT_SHELL_CMD()` 导出命令描述符。
5. 如果新增 `.c` 文件，把它加入 `Project/project.uvprojx`。
6. 编译后通过 UART5 执行 `help`，确认命令已出现。

示例：

```c
static shell_ret_t prv_cmd_foo(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    System_ShellCore_Printf(ctx, "foo ok");
    return SHELL_RET_OK;
}

EXPORT_SHELL_CMD("foo", "foo request", prv_cmd_foo, SHELL_PERM_READONLY, SHELL_MODE_ANY);
```

## 6. 配置参数

当前 Shell 容量配置在 `System/sys_shell_cfg.h`：

| 配置 | 当前值 | 含义 |
| --- | ---: | --- |
| `SHELL_MAX_COMMANDS` | 24 | 最大注册命令数量 |
| `SHELL_MAX_LINE_LEN` | 96 | 单行命令最大长度 |
| `SHELL_MAX_ARGS` | 10 | 最大参数个数 |
| `SHELL_OUTPUT_BUF_SIZE` | 256 | 单次命令输出缓冲 |
| `SHELL_MAX_SESSIONS` | 1 | 当前仅支持单会话 |
| `SHELL_ASCII_ONLY` | 1 | 仅接受可打印 ASCII |

UART5 RX transport 资源：

| 资源 | 当前值 |
| --- | ---: |
| RX 队列深度 | 128 字节 |
| `ShellU5Rx` 栈 | 256 words |
| `ShellU5Rx` 优先级 | 3 |

## 7. 命令清单

### 7.1 基础命令

| 命令 | 说明 |
| --- | --- |
| `help` | 打印当前命令列表 |
| `echo <text>` | 回显文本 |
| `sysmode request` | 查询当前 SysMode、MotionMode 和 fault flags |
| `sysmode set standby|disarmed|armed|failsafe` | 请求系统模式切换；failsafe 走本地故障入口 |
| `momode request` | 查询当前 MotionMode |
| `momode set manual|stabilize|auto` | 请求运动模式切换 |
| `params failsafe request` | 查询最大深度阈值和低压阈值 |
| `params failsafe depth_max <depth_max_m>` | 在 `STANDBY` 下设置最大深度阈值、写入 Flash 并重启；范围 0.01~10000 m |
| `params failsafe voltage_low <voltage_low_v>` | 在 `STANDBY` 下设置低压阈值、写入 Flash 并重启；范围 1.01~100 V |
| `fault` | 打印当前故障位 |
| `reboot` | 触发 MCU 软件复位 |

### 7.2 状态查询命令

| 命令 | 说明 |
| --- | --- |
| `euler request` | 查询 roll/pitch/yaw |
| `depthtemp request` | 查询深度和水温 |
| `power request` | 查询电池电压、电流 |
| `cabin request` | 查询舱内温度、湿度 |
| `chip request` | 查询 CPU 使用率和芯片温度 |

### 7.3 执行机构调试命令

| 命令 | 说明 |
| --- | --- |
| `thruster request` | 查询 6 路推进器当前 PWM |
| `thruster idle` | 推进器回 idle |
| `thruster all <pct>` | 6 路推进器按百分比输出 |
| `thruster set <id 1-6> <pct>` | 单路推进器输出 |
| `thruster pulse <id> <pct> <ms>` | 单路推进器脉冲输出后回 idle |
| `servo set <angle>` | 设置舵机角度 |
| `ws2812 request` | 查询两路 WS2812 LED 数量和 busy 状态 |
| `ws2812 clear <1|2|all>` | 清空指定灯带并刷新 |
| `ws2812 all <strip> <r> <g> <b>` | 整条灯带设置 RGB |
| `ws2812 color <strip> <name>` | 整条灯带设置预置颜色 |
| `ws2812 set <strip> <led> <r> <g> <b>` | 设置单颗 LED RGB |
| `ws2812 pixel <strip> <led> <name>` | 设置单颗 LED 预置颜色 |
| `ws2812 refresh <strip>` | 刷新指定灯带 |

WS2812 颜色名当前包括：`black/off`、`white`、`red`、`green`、`blue`、`yellow`、`cyan`、`magenta/pink`、`orange`、`purple/violet`。

## 8. 权限与模式约束

Shell 命令描述符包含两个安全字段：

- `permission_mask`：命令权限类型，如 `SHELL_PERM_READONLY`、`SHELL_PERM_SAFE_CTRL`、`SHELL_PERM_DANGEROUS`。
- `allowed_mode_mask`：允许执行的 SysMode 掩码，通常用 `SHELL_MODE_MASK(mode)` 或 `SHELL_MODE_ANY`。

Core 执行前会抓取：

- 当前 SysMode。
- 当前 MotionMode。
- 当前 fault flags。
- 当前 auth level。

执行前置检查包括：

- 命令是否存在。
- 参数是否满足 handler 规则。
- 当前 SysMode 是否在 `allowed_mode_mask` 中。
- `SHELL_PERM_DANGEROUS` 命令在未授权时拒绝执行。

当前工程里，推进器调试命令还额外做了本地模式保护：只有 `STANDBY` 和 `DISARMED` 允许直接驱动推进器调试，`ARMED/FAILSAFE` 会返回 `SHELL_RET_MODE_BLOCKED`。

## 9. 安全约束

Shell 是调试工具，不是控制绕行通道。新增命令时遵守以下规则：

- 不能在 Shell handler 中绕过 `System_ModeManager_*` 直接修改系统模式字段。
- 不能在 Shell handler 中绕过 Driver 安全接口直接写 PWM 寄存器。
- 高频控制相关能力优先走 RT/NRT 协议，不要用 Shell 做周期控制。
- 写 Flash、OTA、清日志等服务优先走 NRT 带 ACK 服务；Shell 只用于人工调试入口。
- handler 不应长时间阻塞，尤其不要在 UART5 输入任务里做大块 Flash、长延时或复杂循环。
- 对执行机构命令必须做参数范围检查和模式保护。
- `ARMED` 下默认只开放查询类命令，危险动作必须被拒绝或需要显式授权机制。

## 10. 返回码

Shell Core 返回码定义在 `System/sys_shell_core.h`：

| 返回码 | 含义 |
| --- | --- |
| `SHELL_RET_OK` | 成功 |
| `SHELL_RET_UNKNOWN_CMD` | 未知命令 |
| `SHELL_RET_BAD_ARGS` | 参数错误 |
| `SHELL_RET_DENIED` | 权限不足或安全策略拒绝 |
| `SHELL_RET_MODE_BLOCKED` | 当前模式不允许 |
| `SHELL_RET_BUSY` | 资源忙 |
| `SHELL_RET_INTERNAL` | 内部错误 |

命令 handler 应尽量在返回错误码的同时输出可读说明，方便串口现场定位。

## 11. 常见问题

### 11.1 UART4 NRT 收不到 Shell 响应

这是当前设计的正常行为。Shell 已经从 NRT 文本帧方案切换到 UART5 Debug 字符流。请连接 UART5，波特率使用 `1500000`。

### 11.2 看到 `startup_success` 但命令无响应

检查：

- 串口工具是否发送 CR/LF 行结束符。
- 波特率是否为 `1500000`。
- 输入是否为 ASCII 字符。
- UART5 RX 是否接线正确。
- `ShellCore started on UART5 debug transport` 是否出现在日志中。

### 11.3 `help` 看不到新命令

检查：

- 新命令 `.c` 文件是否加入 `Project/project.uvprojx`。
- 是否包含 `sys_shell_export.h`。
- 是否写了 `EXPORT_SHELL_CMD()`。
- scatter 文件是否包含 `ShellCmd$$Base/Table/Limit`。
- 命令数量是否超过 `SHELL_MAX_COMMANDS`。
- 命令名是否重复。

### 11.4 链接报 ShellCmd 相关错误

优先检查 `System/scatter/app1.sct` 和 `System/scatter/app2.sct` 中是否包含 Shell 命令段，并确认命令文件已进入 Keil 工程。

### 11.5 WS2812 命令偶发 timeout

WS2812 两路当前共用 TIM1，Shell 命令按顺序刷新并等待 busy 清除。如果仍 timeout，优先检查 DMA 中断、TIM 配置、`Driver_WS2812_IsBusy()` 状态和是否有其他上下文同时刷新灯带。

## 12. 后续整理建议

- 将 `Task_ShellTransportNRT_GetVTable()` 重命名为 `Task_ShellTransportUart5_GetVTable()`，避免历史命名误导。
- 如果未来恢复 NRT Shell frame，可以保留 Core 的 `SHELL_TP_NRT_FRAME` 能力，但必须在 ROS 通信文档中重新定义 `0x20/0x21`。
- 对危险命令补充显式 auth/token 或编译开关，发布固件默认关闭危险写操作。
- 为 Shell 增加 `version`、`tasks`、`log clear` 等只读或安全服务类命令时，优先复用已有 System/NRT 服务接口。
