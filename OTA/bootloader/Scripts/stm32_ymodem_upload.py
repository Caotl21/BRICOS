#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
STM32 A/B分区OTA固件上传工具 (Ymodem版本)
通过UART使用Ymodem协议上传固件到STM32

依赖:
    sudo apt install lrzsz python3-serial

使用方法:
    python3 stm32_ymodem_upload.py <bin_file> [app_num]

示例:
    python3 stm32_ymodem_upload.py app.bin        # 自动选择备份分区
    python3 stm32_ymodem_upload.py app.bin 1      # 强制上传到APP1
    python3 stm32_ymodem_upload.py app.bin 2      # 强制上传到APP2
    python3 stm32_ymodem_upload.py app.bin auto   # 自动选择（推荐）
"""

import serial
import time
import sys
import os
import subprocess

# 配置参数
SERIAL_PORT = '/dev/ttyUSB0'  # Jetson Nano默认串口
BAUDRATE = 115200
TIMEOUT = 2

class STM32_Ymodem_Uploader:
    def __init__(self, port=SERIAL_PORT, baudrate=BAUDRATE):
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        
    def open(self):
        """打开串口"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=TIMEOUT
            )
            print(f"串口 {self.port} 打开成功，波特率: {self.baudrate}")
            return True
        except Exception as e:
            print(f"打开串口失败: {e}")
            return False
    
    def close(self):
        """关闭串口"""
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("串口已关闭")
    
    def enter_bootloader(self):
        """进入Bootloader模式"""
        print("尝试进入Bootloader模式...")

        # 发送'B'字符
        self.ser.write(b'B')
        time.sleep(0.5)

        # 读取响应
        response = self.ser.read(500).decode('utf-8', errors='ignore')

        if 'READY' in response:
            print("已进入Bootloader模式")
            print(response)
            return True, response
        else:
            print("未收到READY响应，可能已在Bootloader模式")
            print(f"收到: {response}")
            return True, response

    def get_current_app(self, bootloader_info):
        """从Bootloader信息中提取当前运行的APP"""
        try:
            for line in bootloader_info.split('\n'):
                if 'Current APP:' in line:
                    # 提取APP编号
                    app_num = int(line.split(':')[1].strip())
                    return app_num
        except:
            pass
        return None

    def auto_select_target_app(self):
        """自动选择目标APP（选择非当前运行的分区）"""
        print("\n自动选择目标分区...")

        # 发送'I'命令获取系统信息
        self.ser.write(b'I')
        time.sleep(0.3)
        response = self.ser.read(500).decode('utf-8', errors='ignore')
        print(response)

        # 提取当前APP
        current_app = self.get_current_app(response)

        if current_app == 1:
            target_app = 2
            print(f"✓ 当前运行APP1，将更新到APP2（备份分区）")
        elif current_app == 2:
            target_app = 1
            print(f"✓ 当前运行APP2，将更新到APP1（备份分区）")
        else:
            # 无法确定，默认APP1
            target_app = 1
            print(f"⚠ 无法确定当前APP，默认更新到APP1")

        return target_app
    
    def send_command(self, cmd):
        """发送命令"""
        self.ser.write(cmd.encode())
        time.sleep(0.2)
        
        # 读取响应
        response = self.ser.read(200).decode('utf-8', errors='ignore')
        print(response, end='')
        
        return response
    
    def upload_ymodem(self, bin_file):
        """使用sz命令上传文件(Ymodem协议)"""
        print(f"\n开始上传 {bin_file}...")
        print("使用Ymodem协议传输...")
        
        try:
            # 关闭Python的串口，让sz使用
            self.ser.close()
            
            # 使用sz命令发送文件
            cmd = f"sz --ymodem {bin_file} < {self.port} > {self.port}"
            
            print(f"执行命令: {cmd}")
            result = subprocess.run(
                cmd,
                shell=True,
                capture_output=True,
                text=True,
                timeout=60
            )
            
            # 重新打开串口
            time.sleep(1)
            self.open()
            
            if result.returncode == 0:
                print("✓ 文件上传成功!")
                return True
            else:
                print(f"✗ 上传失败: {result.stderr}")
                return False
                
        except subprocess.TimeoutExpired:
            print("✗ 上传超时!")
            self.open()
            return False
        except Exception as e:
            print(f"✗ 上传出错: {e}")
            self.open()
            return False
    
    def upload_firmware(self, bin_file, app_num='auto'):
        """上传固件到指定APP区域"""

        # 检查文件是否存在
        if not os.path.exists(bin_file):
            print(f"错误: 文件 {bin_file} 不存在!")
            return False

        file_size = os.path.getsize(bin_file)
        print(f"文件大小: {file_size} 字节")

        # 检查文件大小
        if file_size > 24 * 1024:
            print(f"警告: 文件大小超过24KB限制!")
            return False

        # 打开串口
        if not self.open():
            return False

        # 进入Bootloader
        success, bootloader_info = self.enter_bootloader()
        if not success:
            self.close()
            return False

        # 自动选择目标APP
        if app_num == 'auto' or app_num is None:
            app_num = self.auto_select_target_app()
        else:
            app_num = int(app_num)

        # 发送下载命令
        print(f"\n发送下载命令: APP{app_num}")
        self.send_command(str(app_num))

        time.sleep(1)
        
        # 使用Ymodem上传
        if not self.upload_ymodem(bin_file):
            self.close()
            return False
        
        # 等待处理完成
        time.sleep(2)
        response = self.ser.read(500).decode('utf-8', errors='ignore')
        print(response)
        
        # 询问是否跳转
        print("\n是否立即跳转到应用程序? (y/n): ", end='', flush=True)
        choice = input().strip().lower()
        
        if choice == 'y':
            print("发送跳转命令...")
            self.send_command('J')
            time.sleep(1)
        
        self.close()
        return True


def main():
    """主函数"""

    # 检查参数
    if len(sys.argv) < 2:
        print("用法: python3 stm32_ymodem_upload.py <bin_file> [app_num]")
        print("示例:")
        print("  python3 stm32_ymodem_upload.py app.bin        # 自动选择备份分区（推荐）")
        print("  python3 stm32_ymodem_upload.py app.bin 1      # 强制上传到APP1")
        print("  python3 stm32_ymodem_upload.py app.bin 2      # 强制上传到APP2")
        print("  python3 stm32_ymodem_upload.py app.bin auto   # 自动选择")
        sys.exit(1)

    bin_file = sys.argv[1]
    app_num = 'auto' if len(sys.argv) < 3 else sys.argv[2]

    if app_num not in ['auto', '1', '2', 1, 2]:
        print("错误: app_num 必须是 1, 2 或 auto")
        sys.exit(1)
    
    # 检查sz命令是否存在
    result = subprocess.run(['which', 'sz'], capture_output=True)
    if result.returncode != 0:
        print("错误: 未找到sz命令!")
        print("请安装: sudo apt install lrzsz")
        sys.exit(1)
    
    print("=" * 50)
    print("STM32 A/B分区OTA固件上传工具")
    print("=" * 50)
    print(f"固件文件: {bin_file}")
    print(f"目标区域: {'自动选择' if app_num == 'auto' else f'APP{app_num}'}")
    print(f"串口: {SERIAL_PORT}")
    print(f"波特率: {BAUDRATE}")
    print("=" * 50)
    
    # 创建上传器
    uploader = STM32_Ymodem_Uploader(SERIAL_PORT, BAUDRATE)
    
    # 上传固件
    if uploader.upload_firmware(bin_file, app_num):
        print("\n" + "=" * 50)
        print("✓ 固件上传完成!")
        print("=" * 50)
        sys.exit(0)
    else:
        print("\n" + "=" * 50)
        print("✗ 固件上传失败!")
        print("=" * 50)
        sys.exit(1)


if __name__ == '__main__':
    main()

