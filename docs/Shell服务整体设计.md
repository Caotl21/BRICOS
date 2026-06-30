# Shell 服务整体设计

## 1. 设计目标

当前 BRICOS Shell 的整体设计分成两层：

- STM32 固件侧：提供真正的命令解析、权限检查、模式检查和命令执行能力
- RDKX5 主机侧：把独占串口包装成一个可复用的 shell 服务，支持多客户端接入

这样做的核心目的有三个：

- 保持固件不改协议的前提下，解决 `/dev/bot_logbridge` 只能被一个进程独占的问题
- 保留原有 UART5 文本 shell 的使用习惯，不引入复杂的新帧协议
- 让主机侧运行方式更像 `sshd` 一样的长期服务，可被 `systemd` 管理

## 2. 总体架构

```text
                    +------------------------------+
                    |     Remote / Local Client    |
                    |  bot_shellctl.py attach/watch|
                    +---------------+--------------+
                                    |
                                    | TCP
                                    v
                    +------------------------------+
                    |      RDKX5 Shell Service     |
                    |        bot_shelld.py         |
                    |  - owns /dev/bot_logbridge   |
                    |  - broadcasts logs           |
                    |  - serializes command access |
                    +---------------+--------------+
                                    |
                                    | UART5 text stream
                                    v
                    +------------------------------+
                    |        STM32 Firmware        |
                    |  Task_ShellTransportNRT      |
                    |  + System_ShellCore          |
                    +------------------------------+
```

## 3. 固件侧设计

### 3.1 通道定义

当前固件 shell 运行在 UART5 Debug 通道上：

- UART：`UART5`
- 设备侧用途：日志 + Shell
- 波特率：`1500000-8N1`

固件启动 shell transport 时，会先输出：

```text
startup_success
```

这个文本在主机侧被用作“shell 已 ready”的启动标志。

参考：

- [Task/task_shell_transport_nrt.c](D:/ctl/stm32/Bricsbot-Motion-Controller/Task/task_shell_transport_nrt.c:81)
- [docs/系统调试工具shell设计.md](D:/ctl/stm32/Bricsbot-Motion-Controller/docs/系统调试工具shell设计.md:17)

### 3.2 Shell Core

Shell Core 提供统一的命令模型：

- 命令自动注册
- 行缓冲与参数拆分
- 权限检查
- 系统模式检查
- handler 分发
- 文本响应输出

关键接口定义在：

- [System/sys_shell_core.h](D:/ctl/stm32/Bricsbot-Motion-Controller/System/sys_shell_core.h:23)

当前每条命令 handler 的签名是：

```c
typedef shell_ret_t (*shell_cmd_handler_t)(shell_cmd_ctx_t *ctx, int argc, char **argv);
```

返回值 `shell_ret_t` 用于描述命令执行结果，例如：

- `SHELL_RET_OK`
- `SHELL_RET_BAD_ARGS`
- `SHELL_RET_DENIED`
- `SHELL_RET_MODE_BLOCKED`
- `SHELL_RET_BUSY`
- `SHELL_RET_INTERNAL`

### 3.3 固件输入输出模型

输入路径：

```text
UART5 RX ISR
  -> RX queue
  -> ShellU5Rx task
  -> System_ShellCore_OnRxBytes()
  -> 逐字符拼行
  -> 收到 CR/LF 后执行命令
```

输出路径：

```text
handler
  -> System_ShellCore_Printf()
  -> command output buffer
  -> System_ShellCore_SendText()
  -> UART5
```

特点：

- 当前是纯文本 UART stream，不是 request/response frame 协议
- 命令执行结束没有独立的“帧结束”标记
- 单命令输出和后台日志会共用同一条字符流

### 3.4 固件侧当前限制

固件 transport 仍是单会话模型：

- 当前固定 `session_id = 0`
- UART5 transport 不区分多个上位机客户端
- 多会话能力在抽象层保留过接口，但当前实现未启用

所以“多客户端”能力不是由 STM32 直接提供，而是由主机侧 bridge 实现。

## 4. 主机侧 Shell Service 设计

### 4.1 角色定位

`bot_shelld.py` 是一个长期驻留的用户态服务，职责类似精简版 `sshd`：

- 独占打开 `/dev/bot_logbridge`
- 持续读取设备日志与 shell 输出
- 接受多个 TCP 客户端连接
- 为客户端广播日志
- 为命令输入做串行化仲裁

关键文件：

- [tools/shell/bot_shelld.py](D:/ctl/stm32/Bricsbot-Motion-Controller/tools/shell/bot_shelld.py:1)

### 4.2 服务端状态

daemon 内部维护的核心状态包括：

- `serial_connected`：串口是否已连上
- `shell_ready`：设备 shell 是否 ready
- `controller_id`：当前持有交互控制权的客户端
- `active_exec`：是否存在正在执行的 `exec` 请求
- `interactive_lock_active`：是否存在 attach 客户端占用中的短时命令锁

### 4.3 服务端输入输出

服务端有两类 IO：

- 下行：从串口读设备输出
- 上行：从 TCP 客户端读控制消息

串口读到的一切文本，默认会：

- 广播给所有 `attach/watch` 客户端
- 如果当前是 `exec` 模式，还会单独回送给发起者

### 4.4 多客户端并发策略

当前并发策略不是“真多会话 shell”，而是：

- 多个客户端都可以 `attach` 或 `watch`
- 所有人都能同时看到同一份输出
- 同一时刻只允许一个客户端占用“发送命令”的入口

这是一个“多观察者 + 单执行者”的模型。

### 4.5 锁设计

attach 模式的锁是短时锁：

- 某个客户端按回车发出一条命令时，daemon 给它上锁
- 锁保持到“输出静默一小段时间”为止
- 之后自动释放

`exec` 模式与 attach 共用同一把逻辑锁，所以：

- `exec` 与 `attach` 不会并发写串口
- 两个 `exec` 也不会并发

### 4.6 “串口静默”判定

由于固件是文本流，不存在协议级 response end marker，所以 daemon 只能用经验判定：

- 发出一条命令后
- 如果串口在一段时间内没有再收到新的字节
- 就认为这一轮命令输出已经结束

这个机制解决的是“何时释放主机侧命令锁”，不是固件侧的命令返回机制。

局限性：

- 如果后台日志持续很多，锁释放会偏晚
- 如果某条命令输出中间本来会暂停，锁可能偏早释放

## 5. 客户端设计

### 5.1 客户端角色

`bot_shellctl.py` 是 bridge 的前端 CLI，支持四种模式：

- `attach`：交互式 shell
- `watch`：只看日志
- `status`：查询 daemon 状态
- `exec`：发一条命令并等待结果

关键文件：

- [tools/shell/bot_shellctl.py](D:/ctl/stm32/Bricsbot-Motion-Controller/tools/shell/bot_shellctl.py:1)

### 5.2 attach 模式

attach 模式尽量对齐旧的 `shell_client.py` 使用体验：

- 本地提示符
- 本地历史记录
- 本地 Tab 补全
- 设备 reboot 后重新显示欢迎页
- 多种日志级别本地着色

当前本地着色规则：

- `[BOOT]`：深蓝色
- `[INFO]`：青色
- `[WARN]` / `[WARNING]`：黄色
- `[ERR ]` / `[ERROR]`：红色

### 5.3 客户端身份

客户端默认身份格式为：

```text
hostname:pid:tty
```

这样 `BUSY` 提示里的 owner 更容易映射到具体的 SSH 会话。

### 5.4 reboot 行为

为了和旧单会话客户端保持一致：

- 发送 `reboot` 后，本地会先提示等待 startup banner
- 真正收到 shell ready 的完整切换后，再重新打印 BRICOS 欢迎页

这样避免了重复欢迎页和过早恢复提示符的问题。

## 6. systemd 服务化设计

### 6.1 目的

为了让 daemon 更像 `sshd`：

- 开机自启
- 长期驻留
- 可被 `systemctl` 管理
- 日志进入 `journal`
- 被 `SIGTERM` 优雅停止

### 6.2 服务文件

参考文件：

- [tools/shell/systemd/bot_shelld.service](D:/ctl/stm32/Bricsbot-Motion-Controller/tools/shell/systemd/bot_shelld.service:1)

典型配置项：

- `User` / `Group`
- `SupplementaryGroups=dialout`
- `WorkingDirectory`
- `ExecStart`
- `Restart=always`
- `StandardOutput=journal`

### 6.3 当前 RDKX5 部署方式

在 RDKX5 上，推荐：

- daemon 监听 `0.0.0.0`
- 通过 `systemd` 拉起
- 本机或同子网 PC 通过 `bot_shellctl.py --host <rdkx5-ip>` 连接

## 7. 运行流程

### 7.1 启动流程

```text
systemd
  -> bot_shelld.py
       -> 打开 /dev/bot_logbridge
       -> 建立 TCP 监听
       -> 等待客户端连接
```

STM32 重启后：

```text
STM32 boot
  -> UART5 output startup_success
  -> daemon marks shell_ready=true
  -> attach client reprints BRICOS banner
```

### 7.2 attach 交互流程

```text
attach client connects
  -> daemon returns state
  -> client shows BRICOS intro after shell ready
  -> user types command
  -> daemon acquires short interactive lock
  -> serial writes line + CRLF
  -> device outputs logs/response
  -> daemon broadcasts output
  -> serial goes idle
  -> daemon releases lock
```

### 7.3 exec 流程

```text
exec client connects
  -> sends one command
  -> daemon acquires exclusive command lock
  -> serial writes line
  -> collects output until idle/timeout
  -> returns exec_done
```

## 8. 设计边界与取舍

### 8.1 当前优点

- 不改固件协议即可支持多客户端使用
- 保留现有 UART5 shell 生态
- 运维方式接近成熟服务进程
- 本机调试和远程调试统一入口

### 8.2 当前缺点

- 不是协议级多会话
- 输出结束靠静默推断，不是严格可证明的完成语义
- shell 输出和设备日志共享同一条流
- 当前尚无鉴权，跨网段暴露有风险

### 8.3 为什么没直接改固件

如果想做真多会话，必须引入：

- request/response frame
- session_id 路由
- 每会话输入缓冲
- 每会话输出分流

这会显著增加固件复杂度，也会改变现有调试链路。当前主机侧 bridge 是一个工程成本最低、收益最高的折中方案。

## 9. 后续可演进方向

可以继续做但当前不是必需的项：

- 增加 bridge 鉴权 token
- 限制可连接 IP 列表
- 为 `exec` 增加更稳定的完成判定
- 为 attach 客户端显示当前 owner / lock 状态
- 如果未来固件允许，再升级为 frame + session 模式

## 10. 当前结论

你现在这套 shell 服务，本质上是：

- STM32 提供真实 shell 能力
- RDKX5 提供 shell bridge service
- TCP 客户端把多个使用者接进来
- `systemd` 负责把 bridge 变成像 `sshd` 一样的长期服务

这不是“固件原生多会话 shell”，但已经是一个工程上非常实用、稳定、可维护的 shell 服务方案。
