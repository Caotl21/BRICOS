# Shell 接口草图（可切换独立串口）

> 目标：Shell 核心与串口传输解耦，当前可跑在 `UART4(NRT)`，后续无痛切到独立 Shell 串口。

---

## 1. 模块拆分

- `System/sys_shell_core.h/.c`  
  负责命令解析、命令注册、权限校验、执行分发。

- `System/sys_shell_transport.h`  
  定义统一传输接口（抽象层）。

- `Task/task_shell.h/.c`  
  Shell 任务，负责从传输层收包并调用 core。

- `Task/task_shell_transport_nrt.c`  
  当前适配器：复用 NRT 协议帧通道。

- `Task/task_shell_transport_uart.c`（预留）  
  未来适配器：独立 UART 字符流。

---

## 2. 传输抽象接口（核心）

```c
// System/sys_shell_transport.h
#ifndef __SYS_SHELL_TRANSPORT_H__
#define __SYS_SHELL_TRANSPORT_H__

#include <stdint.h>

typedef enum {
    SHELL_TP_NRT_FRAME = 0,     // 当前方案：NRT 协议帧承载
    SHELL_TP_UART_STREAM = 1    // 未来方案：独立 UART 字符流
} shell_transport_type_t;

typedef struct {
    shell_transport_type_t type;
    uint8_t session_id;         // 多会话预留（单会话时可固定为0）
} shell_peer_t;

typedef void (*shell_rx_cb_t)(const shell_peer_t *peer, const uint8_t *data, uint16_t len);

typedef struct {
    int  (*init)(void);
    int  (*start)(shell_rx_cb_t rx_cb);
    int  (*send)(const shell_peer_t *peer, const uint8_t *data, uint16_t len);
    int  (*stop)(void);
} shell_transport_vtable_t;

#endif
```

说明：

- `core` 永远只依赖 `shell_transport_vtable_t`，不直接依赖 `bsp_uart_*`。
- 更换串口时只换 transport 实现，不改 core 与命令逻辑。

---

## 3. Shell Core 接口草图

```c
// System/sys_shell_core.h
#ifndef __SYS_SHELL_CORE_H__
#define __SYS_SHELL_CORE_H__

#include <stdint.h>
#include "sys_shell_transport.h"

typedef enum {
    SHELL_RET_OK = 0,
    SHELL_RET_UNKNOWN_CMD,
    SHELL_RET_BAD_ARGS,
    SHELL_RET_DENIED,
    SHELL_RET_BUSY,
    SHELL_RET_INTERNAL
} shell_ret_t;

typedef struct shell_cmd_ctx_s shell_cmd_ctx_t;
typedef shell_ret_t (*shell_cmd_handler_t)(shell_cmd_ctx_t *ctx, int argc, char **argv);

typedef struct {
    const char *name;
    const char *help;
    shell_cmd_handler_t handler;
    uint32_t perm_mask;
    uint32_t allowed_mode_mask;
} shell_cmd_desc_t;

int System_ShellCore_Init(const shell_transport_vtable_t *tp);
int System_ShellCore_Register(const shell_cmd_desc_t *cmd);
int System_ShellCore_OnRxBytes(const shell_peer_t *peer, const uint8_t *data, uint16_t len);
int System_ShellCore_Printf(shell_cmd_ctx_t *ctx, const char *fmt, ...);

#endif
```

说明：

- `OnRxBytes` 支持两类输入：
- NRT 帧载荷（一帧一命令）
- UART 字符流（逐字节，含 `\r\n`、`Tab`）

---

## 4. Task 层接口草图

```c
// Task/task_shell.h
#ifndef __TASK_SHELL_H__
#define __TASK_SHELL_H__

void Task_Shell_Init(void);                // 创建 Task_Shell
void Task_Shell_BindNrtTransport(void);    // 当前默认
void Task_Shell_BindUartTransport(void);   // 未来独立串口

#endif
```

`Task_Shell` 职责：

- 初始化并绑定 transport。
- 将 transport 收到的数据转给 `System_ShellCore_OnRxBytes`。
- 控制队列长度与速率，避免堵塞 RT 通道。

---

## 5. NRT 与独立 UART 的切换方式

推荐引入配置宏：

```c
// System/sys_shell_cfg.h
#define SHELL_ENABLE                1
#define SHELL_TRANSPORT_DEFAULT     SHELL_TP_NRT_FRAME
// 后续切独立串口时改为 SHELL_TP_UART_STREAM
```

切换原则：

1. `core` 不改。  
2. 命令注册表不改。  
3. 仅切换 `Task_Shell_Bind*Transport()` 绑定目标。  
4. 若切独立 UART，再在 `bsp_uart` 增加 `BSP_UART_SHELL` 端口映射。

---

## 6. 命令执行上下文建议

```c
struct shell_cmd_ctx_s {
    shell_peer_t peer;
    uint8_t auth_level;
    uint8_t sys_mode_snapshot;
    uint8_t motion_mode_snapshot;
    uint32_t fault_flags_snapshot;
};
```

好处：

- 执行命令时使用快照，避免并发读写竞态。
- 每条命令天然带来源信息（哪个 transport/session）。

---

## 7. 迁移到独立 Shell 串口的最小步骤

1. 在 `bsp_uart` 增加 `BSP_UART_SHELL` 配置（波特率/IRQ/DMA）。  
2. 实现 `task_shell_transport_uart.c`，把 UART RX 回调接到 `System_ShellCore_OnRxBytes`。  
3. 把 `SHELL_TRANSPORT_DEFAULT` 从 `NRT_FRAME` 切到 `UART_STREAM`。  
4. 保留 NRT transport 代码用于回滚。  
5. 联调验证：`help/status/mode get` 三条基础命令先通。  

---

## 8. 设计约束（强烈建议）

- 禁止 shell 命令直接改数据池内部变量，必须走 `System_ModeManager`/既有 API。  
- `ARMED` 下默认只读命令，危险命令必须拒绝。  
- 输出统一走 transport `send()`，不要在 core 内直接访问 UART。  
- 单命令执行超时建议 `< 50ms`，重操作异步化。

