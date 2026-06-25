# STM32F407 OTA Bootloader

用于 STM32F407 的双分区 Bootloader。
- 支持通过 Ymodem 协议进行 OTA 升级
- 支持软件复位/硬件复位两种升级模式
- 采用独立 Boot Flag 机制记录升级状态和启动状态

## Python OTA 工具
提供了一个基于 Python 的 OTA 升级工具，支持通过串口发送 Ymodem 数据进行升级。
- 依赖 `pyserial` 和 `libgpiod` 库
- 支持两种触发模式：APP 触发和硬件复位触发 对于硬件触发，需要指定 GPIO 芯片和线号来控制复位引脚
- 脚本使用示例：
```bash
# 软件OTA
python3 ota_send.py --mode app-trigger --cmd-port /dev/ttyS7  --data-port /dev/ttyS6 --file app.bin
# 硬件OTA
sudo chmod 666 /dev/gpiochip1
python3 ota_send.py --mode hw-reset   --cmd-port /dev/ttyS7   --data-port /dev/ttyS6 --file app.bin   --reset-gpiochip /dev/gpiochip1   --reset-line 27
```

## Flash Layout

- Bootloader: `0x08000000 - 0x08003FFF` (16KB)
- Boot Flag: `0x08004000 - 0x08007FFF` (16KB)
- APP1: `0x08008000 - 0x0801FFFF` (96KB, 运行区)
- APP2: `0x08020000 - 0x0803FFFF` (128KB, OTA缓存区)

分区定义见 [Bootloader/bootloader.h](Bootloader/bootloader.h)。

## Boot Flow

上电后 Bootloader 按以下顺序决定是否进入升级模式：

1. 软件复位模式：检查软件请求标志  
如果 APP 之前设置了 `enter_bootloader`，则直接进入 Bootloader 菜单。

2. 硬件复位模式：检查串口命令  
上电后 100 毫秒内，如果 `USART1` 收到字符 `B`，则进入 Bootloader 菜单。

3. 正常启动 APP  
若未请求进入 Bootloader，则执行正常启动流程：
- 检查 `need_copy`
- 必要时把 APP2 复制到 APP1
- 检查启动次数
- 跳转到 APP1

启动入口见 [Bootloader/main.c](Bootloader/main.c)。

## UART Usage

- `USART1`: 调试输出 / Bootloader 命令口
- `USART2`: APP 通信 / Ymodem OTA 数据口

## OTA Update Flow

OTA 下载始终写入 APP2，不直接覆盖 APP1。

流程如下：

1. APP 收到 OTA 触发命令
2. APP 设置 `enter_bootloader` 并软件复位
3. Bootloader 启动后直接进入菜单
4. 主机发送命令 `2`
5. 通过 Ymodem 将固件下载到 APP2
6. 下载成功后设置 `need_copy=1`
7. 下次启动时 Bootloader 将 APP2 复制到 APP1
8. 跳转到新的 APP1

Ymodem 下载处理位于 [Bootloader/bootloader.c](Bootloader/bootloader.c)。

## Boot Flag

Boot Flag 保存在独立扇区，用于保存启动状态和 OTA 状态，包括：

- `boot_attempts`
- `need_copy`
- `ota_complete`
- `enter_bootloader`
- `app1_version`
- `app2_version`

相关定义见 [Bootloader/boot_flag.h](Bootloader/boot_flag.h)。  
相关实现见 [Bootloader/boot_flag.c](Bootloader/boot_flag.c)。

## Notes

- APP1 必须链接到 `0x08008000`
- APP 的向量表必须正确重定位到 APP1 基址
- APP 启动稳定后应清除 `boot_attempts`
- 调试日志只能走 `USART1`，不要混入 `USART2` 的 Ymodem 数据流
