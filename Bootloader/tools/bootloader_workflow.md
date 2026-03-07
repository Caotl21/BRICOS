# STM32F407 Bootloader 工作流程说明

## 1. 概述

本 Bootloader 的目标是为 STM32F407 提供以下能力：

- 上电后先进入 Bootloader
- 在短时间窗口内允许用户进入命令模式
- 若未进入命令模式，则自动执行正常启动流程
- 支持通过 Ymodem 将新固件下载到 APP2 区域
- 通过 Boot Flag 机制，在复位后将 APP2 拷贝到 APP1
- 当 APP1 有效时跳转执行 APP1
- 当 APP1 无效但 APP2 有效时，自动尝试恢复
- 当 APP1 和 APP2 都无效时，停留在 Bootloader 中等待人工处理

主入口位于 [Bootloader/main.c](Bootloader/main.c)，核心启动逻辑位于 [Bootloader/bootloader.c](Bootloader/bootloader.c)，Ymodem 协议位于 [Bootloader/ymodem.c](Bootloader/ymodem.c)，升级状态保存在 Boot Flag 区域，由 [Bootloader/boot_flag.h](Bootloader/boot_flag.h) 和对应实现管理。

---

## 2. Flash 布局

当前工程采用 STM32F407 的扇区布局，规划如下：

- Bootloader
  - 地址范围：`0x08000000 ~ 0x08003FFF`
  - 大小：16 KB
  - 所在扇区：Sector 0

- Boot Flag
  - 地址范围：`0x08004000 ~ 0x08007FFF`
  - 大小：16 KB
  - 所在扇区：Sector 1

- APP1
  - 起始地址：`0x08008000`
  - 结束地址：`0x0801FFFF`
  - 大小：96 KB
  - 所在扇区：Sector 2 ~ Sector 4
  - 用途：正式运行区

- APP2
  - 起始地址：`0x08020000`
  - 结束地址：`0x0803FFFF`
  - 大小：128 KB
  - 所在扇区：Sector 5
  - 用途：OTA 下载缓存区

其中：

- APP1 是最终跳转执行的区域
- APP2 是 OTA 下载目标区域
- Boot Flag 用于记录升级和启动状态

相关定义见 [Bootloader/bootloader.h](Bootloader/bootloader.h) 和 [Bootloader/boot_flag.h](Bootloader/boot_flag.h)。

---

## 3. 上电启动总流程

系统上电后，Bootloader 的总体流程如下：

1. 进入 `main()`
2. 初始化系统基础外设
3. 等待一段时间，判断是否进入命令模式
4. 如果用户请求进入命令模式，则进入命令循环
5. 如果未进入命令模式，则执行正常启动逻辑
6. 正常启动逻辑中，优先判断是否需要执行 OTA 切换
7. 如果 APP1 有效，则跳转到 APP1
8. 如果 APP1 无效，则尝试从 APP2 恢复 APP1
9. 如果恢复后仍失败，则留在 Bootloader 中等待命令

主入口实现位于 [Bootloader/main.c](Bootloader/main.c)。

---

## 4. 启动阶段详细流程

### 4.1 基础初始化

在 [Bootloader/main.c](Bootloader/main.c) 中，启动阶段会完成以下初始化：

- 设置向量表偏移到 Bootloader 区域
- 初始化 SysTick，提供 1 ms 时基
- 初始化延时模块
- 初始化串口
- 初始化 Bootloader 模块

当前代码中主要对应如下步骤：

- `SCB->VTOR = BOOTLOADER_ADDR;`
- `SysTick_Init();`
- `Delay_Init();`
- `UART_Init();`
- `Bootloader_Init();`

### 4.2 串口角色

当前设计中两路串口的目标职责为：

- USART1
  - 调试打印
  - 用户命令输入

- USART2
  - Ymodem OTA 数据收发

当前实现上：

- [Bootloader/main.c](Bootloader/main.c) 中进入 Bootloader 的触发键 `B` 只检查 USART1
- [Bootloader/bootloader.c](Bootloader/bootloader.c) 中命令循环目前仍同时监听 USART1 和 USART2
- [Bootloader/ymodem.c](Bootloader/ymodem.c) 的 Ymodem 收发已经使用 USART2

因此，从设计目标看，串口分工已经基本明确，但命令循环部分仍建议继续收口到 USART1。

---

## 5. Bootloader 模式检测流程

Bootloader 在启动后会给用户一个有限时间窗口进入命令模式。

当前逻辑位于 [Bootloader/main.c](Bootloader/main.c) 的 `Check_BootloaderMode()`：

- 记录当前系统时基
- 在 `BOOTLOADER_TIMEOUT` 时间内循环检测串口输入
- 如果 USART1 收到字符 `B`，则进入命令模式
- 如果超时未收到，则进入正常启动流程

这一步的意义是：

- 上电后允许人工介入
- 不影响系统自动启动 APP
- 便于现场维护和调试

---

## 6. 正常启动流程

正常启动流程的核心入口是 [Bootloader/bootloader.c](Bootloader/bootloader.c) 中的 `Bootloader_Run()`。

当前流程如下。

### 6.1 显示并读取 Boot Flag 状态

Bootloader 启动时会先读取 Boot Flag，并打印如下关键信息：

- `valid_flag`
- `need_copy`
- `boot_attempts`

其中：

- `valid_flag` 表示 Boot Flag 区域是否已初始化
- `need_copy` 表示是否需要把 APP2 拷贝到 APP1
- `boot_attempts` 表示 APP 连续启动尝试次数

### 6.2 判断是否需要执行 OTA 切换

如果 Boot Flag 满足：

- `valid_flag == APP_VALID_FLAG`
- `need_copy == 1`

则说明：

- 上一次 Ymodem 下载已经成功完成
- 新固件已经在 APP2
- 现在需要把 APP2 正式拷贝到 APP1

这时 Bootloader 会调用 `Bootloader_RecoverFromAPP2()` 执行恢复流程。

### 6.3 启动尝试计数机制

当前版本中，Bootloader 还保留了启动尝试计数机制：

- 先读取当前 `boot_attempts`
- 如果 `boot_attempts >= MAX_BOOT_ATTEMPTS`，则触发恢复流程
- 每次准备跳转 APP1 前，调用 `Bootloader_IncrementBootAttempts()` 增加计数

这个机制的设计目的，是为了在 APP 启动异常时避免反复死循环重启。

### 6.4 检查 APP1 是否有效

Bootloader 会检查 APP1 起始地址处的栈指针是否位于有效 RAM 区间内。

当前实现位于 [Bootloader/bootloader.c](Bootloader/bootloader.c) 的 `Bootloader_CheckApp()`。

有效性判断逻辑是：

- 读取 APP 起始地址处的 MSP
- 判断 MSP 是否在 `0x20000000 ~ 0x2002FFFF` 范围内

如果有效，则认为 APP1 具备基本可启动条件。

### 6.5 跳转到 APP1

若 APP1 有效，则调用 `Bootloader_JumpToApp(APP1_ADDR)`。

跳转前会执行：

- 关闭中断
- 关闭 SysTick
- 清除 NVIC 中断状态
- 设置向量表偏移到 APP 地址
- 设置 MSP 为 APP 的初始栈
- 读取 APP 复位向量并跳转

这是标准的 Bootloader 跳转流程。

### 6.6 APP1 无效时的恢复流程

如果 APP1 无效，Bootloader 会继续判断 APP2 是否有效：

- 如果 APP2 有效，则尝试将 APP2 拷贝到 APP1
- 拷贝成功后再次检查 APP1
- 若 APP1 恢复成功，则跳转执行 APP1
- 若恢复失败，则停留在 Bootloader

---

## 7. APP2 恢复 APP1 的流程

APP2 恢复 APP1 的入口为 [Bootloader/bootloader.c](Bootloader/bootloader.c) 中的 `Bootloader_RecoverFromAPP2()`。

恢复逻辑如下：

1. 调用 `Bootloader_CopyAPP2ToAPP1()`
2. 擦除 APP1 所在扇区
3. 从 APP2 按字复制数据到 APP1
4. 复制完成后进行逐字校验
5. 成功后更新 Boot Flag：
   - `boot_attempts = 0`
   - `need_copy = 0`
   - `app1_version = app2_version`

也就是说：

- APP2 是缓存区
- APP1 是运行区
- 任何正式切换都通过这个恢复流程完成

这种策略的优点是：

- OTA 下载时不直接覆盖 APP1
- 下载失败时更容易保留原 APP1
- 切换动作集中由 Bootloader 控制

---

## 8. OTA 下载流程

OTA 下载逻辑主要在 [Bootloader/bootloader.c](Bootloader/bootloader.c) 的 `Process_YmodemDownload()` 中，底层 Ymodem 协议在 [Bootloader/ymodem.c](Bootloader/ymodem.c)。

### 8.1 下载目标

当前方案固定下载到 APP2：

- APP2 起始地址：`0x08020000`
- APP2 大小：128 KB

### 8.2 OTA 开始

开始下载前，Bootloader 会：

- 调用 `Bootloader_StartOTA()`
- 将 Boot Flag 中的 `ota_complete` 清零

### 8.3 Ymodem 握手

Bootloader 会通过 USART2 发送字符 `C`，通知 PC 端开始以 CRC16 模式发送数据。

### 8.4 擦除 APP2

下载数据前先擦除 APP2 区域，确保目标区域为空。

### 8.5 接收数据包

随后开始循环接收 Ymodem 数据包：

- 第一个包解析文件名和文件大小
- 后续包按长度写入 APP2
- 成功则回复 `ACK`
- 失败则回复 `NAK` 或执行中止

### 8.6 下载完成后的状态处理

当数据接收完成并验证 APP2 有效后，Bootloader 会调用 `Bootloader_FinishOTA(1)`，将 Boot Flag 更新为：

- `ota_complete = 1`
- `need_copy = 1`

这表示：

- APP2 已经准备好
- 下次启动时需要把 APP2 切换到 APP1

随后执行系统复位。

---

## 9. Boot Flag 的作用

Boot Flag 是整个 Bootloader 状态机的核心状态区，定义见 [Bootloader/boot_flag.h](Bootloader/boot_flag.h)。

主要字段包括：

- `valid_flag`
  - 表示 Boot Flag 是否有效

- `boot_attempts`
  - APP 连续启动尝试次数

- `need_copy`
  - 是否需要从 APP2 拷贝到 APP1

- `ota_complete`
  - OTA 是否已完成下载

- `app1_version`
  - APP1 版本号

- `app2_version`
  - APP2 版本号

- `boot_count`
  - 总启动次数

当前 Boot Flag 已被抽成共享模块，目的是：

- Bootloader 可读写升级状态
- APP 也可在启动成功后清零 `boot_attempts`

---

## 10. APP 启动成功清零机制

当前版本保留了 `boot_attempts` 机制，因此 APP 在成功启动后，应调用共享模块中的启动成功清零接口。

建议的思路是：

- APP 不依赖整个 Bootloader 模块
- APP 只依赖共享的 Boot Flag 模块
- APP 在关键初始化稳定完成后，调用一次 `BootFlag_MarkBootSuccess()`

这样做的意义是：

- 告诉 Bootloader 本次 APP 启动成功
- 将 `boot_attempts` 清零
- 防止 Bootloader 误判 APP 启动失败并反复恢复

注意：

- APP 不应过早调用该接口
- 应该在时钟、关键驱动、主要初始化完成后再调用
- 只需要调用一次，不需要周期性调用

---

## 11. 命令模式支持的功能

当前命令模式在 [Bootloader/bootloader.c](Bootloader/bootloader.c) 中支持以下操作：

- `2`
  - 通过 Ymodem 下载固件到 APP2

- `C`
  - 手动执行 APP2 到 APP1 的复制

- `J`
  - 直接跳转到 APP1

- `O`
  - 模拟 OTA 完成，设置 `need_copy`

- `I`
  - 显示系统信息

- `E`
  - 擦除 APP1

- `F`
  - 擦除 APP2

- `R`
  - 软件复位

- `M`
  - 重新显示菜单

这使得 Bootloader 除了自动启动外，也具备人工维护能力。

---

## 12. 当前实现的注意事项

基于当前代码，以下几点值得注意：

### 12.1 命令口与 OTA 口尚未完全彻底隔离

当前设计意图为：

- USART1：调试与命令
- USART2：Ymodem 数据

但 [Bootloader/bootloader.c](Bootloader/bootloader.c) 的命令循环目前仍同时监听 USART1 和 USART2，因此后续建议继续收口。

### 12.2 APP 是否无感依赖 Bootloader

当前版本中：

- APP 运行地址仍固定为 APP1 地址
- APP 若保留 `boot_attempts` 机制，则需要调用共享 Boot Flag 接口清零

因此当前更准确的描述是：

- APP 不依赖整个 Bootloader
- 但 APP 仍轻度依赖共享 Boot Flag 接口

### 12.3 APP 直接烧录与经 Bootloader 启动不是同一件事

当前 APP1 地址为 `0x08008000`。因此：

- 若通过 Bootloader 启动，Bootloader 会跳转到 APP1
- 若没有 Bootloader、直接从 `0x08000000` 上电复位，则同一份 APP 镜像不一定能直接运行

这属于链接地址和启动入口层面的差异，不是 Boot Flag 机制本身能解决的。

---

## 13. 总结

整个 Bootloader 的核心工作流程可以概括为：

- 上电后先进入 Bootloader
- 给用户一个短暂窗口决定是否进入命令模式
- 若不进入命令模式，则自动判断是否需要执行 OTA 切换
- 若 `need_copy=1`，则先将 APP2 拷贝到 APP1
- 若 APP1 有效，则跳转到 APP1
- 若 APP1 无效但 APP2 有效，则尝试恢复
- 若两者都无效，则停留在 Bootloader
- OTA 升级始终先下载到 APP2，再在下次启动时正式切换到 APP1
- Boot Flag 负责记录升级和启动状态
- APP 启动成功后通过共享 Boot Flag 接口清零 `boot_attempts`

这套设计的核心特点是：

- Bootloader 决定切换
- APP1 负责运行
- APP2 负责缓存
- Boot Flag 负责状态
- Ymodem 负责传输