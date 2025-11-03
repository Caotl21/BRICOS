#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
STM32 Ymodem固件上传工具
专门用于上传固件到APP2区域作为备份
"""

import serial
import time
import sys
import os

# 配置参数
SERIAL_PORT = '/dev/ttyUSB0'  # 固件传输串口
BAUDRATE = 115200
TIMEOUT = 5

class YmodemUploader:
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
            print(f"✓ 串口 {self.port} 打开成功")
            return True
        except Exception as e:
            print(f"✗ 打开串口失败: {e}")
            return False
    
    def close(self):
        """关闭串口"""
        if self.ser and self.ser.is_open:
            self.ser.close()
    
    def _calc_crc16(self, data):
        """计算CRC16校验"""
        crc = 0
        for byte in data:
            crc ^= byte << 8
            for _ in range(8):
                if crc & 0x8000:
                    crc = (crc << 1) ^ 0x1021
                else:
                    crc = crc << 1
                crc &= 0xFFFF
        return crc
    
    def _wait_for_char(self, expected_char, timeout=5):
        """等待指定字符"""
        start_time = time.time()
        while time.time() - start_time < timeout:
            if self.ser.in_waiting > 0:
                c = self.ser.read(1)
                if c == expected_char:
                    return True
            time.sleep(0.01)
        return False
    
    def _wait_for_ack(self, timeout=5):
        """等待ACK确认"""
        return self._wait_for_char(b'\x06', timeout)
    
    def _make_header_packet(self, filename, filesize):
        """创建文件信息包"""
        # 文件信息字符串
        file_info = f"{filename}\x00{filesize}\x00".encode()
        
        # 填充到128字节
        packet_data = bytearray(128)
        packet_data[0:len(file_info)] = file_info
        
        # 计算CRC
        crc = self._calc_crc16(packet_data)
        
        # 组装包: SOH + 包号 + 包号取反 + 数据 + CRC
        packet = bytearray()
        packet.append(0x01)  # SOH
        packet.append(0x00)  # 包号0
        packet.append(0xFF)  # 包号取反
        packet.extend(packet_data)
        packet.append((crc >> 8) & 0xFF)
        packet.append(crc & 0xFF)
        
        return bytes(packet)
    
    def _make_data_packet(self, packet_num, data):
        """创建数据包"""
        # 选择包大小
        if len(data) <= 128:
            packet_data = bytearray(128)
            header = 0x01  # SOH
        else:
            packet_data = bytearray(1024)
            header = 0x02  # STX
        
        # 复制数据，剩余部分自动为0
        packet_data[0:len(data)] = data
        
        # 计算CRC
        crc = self._calc_crc16(packet_data)
        
        # 组装包
        packet = bytearray()
        packet.append(header)
        packet.append(packet_num & 0xFF)
        packet.append((~packet_num) & 0xFF)
        packet.extend(packet_data)
        packet.append((crc >> 8) & 0xFF)
        packet.append(crc & 0xFF)
        
        return bytes(packet)
    
    def upload_firmware(self, bin_file):
        """上传固件"""
        # 检查文件
        if not os.path.exists(bin_file):
            print(f"✗ 文件不存在: {bin_file}")
            return False
        
        file_size = os.path.getsize(bin_file)
        if file_size > 24 * 1024:
            print(f"✗ 文件过大: {file_size} > 24KB")
            return False
        
        print(f"文件: {bin_file}")
        print(f"大小: {file_size} 字节")
        
        # 打开串口
        if not self.open():
            return False
        
        try:
            # 1. 进入Bootloader
            print("进入Bootloader模式...")
            self.ser.write(b'B')
            time.sleep(0.5)
            
            # 清空接收缓冲区
            self.ser.read(self.ser.in_waiting)
            
            # 2. 发送下载命令
            print("发送下载命令...")
            self.ser.write(b'2')  # 下载到APP2
            time.sleep(0.5)
            
            # 3. 等待'C'字符（Ymodem开始）
            print("等待Ymodem开始信号...")
            if not self._wait_for_char(b'C', timeout=10):
                print("✗ 未收到Ymodem开始信号")
                return False
            
            # 4. 发送文件信息包
            print("发送文件信息...")
            filename = os.path.basename(bin_file)
            header_packet = self._make_header_packet(filename, file_size)
            self.ser.write(header_packet)
            
            if not self._wait_for_ack():
                print("✗ 文件信息包未确认")
                return False
            
            # 等待下一个'C'
            if not self._wait_for_char(b'C', timeout=5):
                print("✗ 未收到数据传输开始信号")
                return False
            
            # 5. 发送数据包
            print("开始传输数据...")
            with open(bin_file, 'rb') as f:
                packet_num = 1
                bytes_sent = 0
                
                while bytes_sent < file_size:
                    # 读取数据块
                    remaining = file_size - bytes_sent
                    chunk_size = min(1024, remaining)
                    data = f.read(chunk_size)
                    
                    if not data:
                        break
                    
                    # 发送数据包
                    packet = self._make_data_packet(packet_num, data)
                    self.ser.write(packet)
                    
                    # 等待确认
                    if not self._wait_for_ack():
                        print(f"✗ 数据包 {packet_num} 未确认")
                        return False
                    
                    bytes_sent += len(data)
                    packet_num = (packet_num + 1) % 256
                    
                    # 显示进度
                    progress = (bytes_sent * 100) // file_size
                    print(f"\r进度: {progress}% ({bytes_sent}/{file_size})", end='', flush=True)
            
            print("\n✓ 数据传输完成")
            
            # 6. 发送EOT结束传输
            print("结束传输...")
            self.ser.write(b'\x04')  # EOT
            
            # 等待NAK然后再发EOT
            if self._wait_for_char(b'\x15', timeout=5):  # NAK
                self.ser.write(b'\x04')  # 再次发送EOT
                if not self._wait_for_ack():
                    print("✗ EOT未确认")
                    return False
            
            print("✓ 固件上传成功!")
            
            # 7. 询问是否跳转
            choice = input("\n是否立即跳转到应用程序? (y/n): ").strip().lower()
            if choice == 'y':
                print("发送跳转命令...")
                self.ser.write(b'J')
                time.sleep(1)
            
            return True
            
        except Exception as e:
            print(f"✗ 传输出错: {e}")
            return False
        finally:
            self.close()


def main():
    if len(sys.argv) != 2:
        print("用法: python3 stm32_ymodem_simple.py <bin_file>")
        print("示例: python3 stm32_ymodem_simple.py app.bin")
        sys.exit(1)
    
    bin_file = sys.argv[1]
    
    print("=" * 50)
    print("STM32 Ymodem固件上传工具 - 精简版")
    print("=" * 50)
    print(f"固件文件: {bin_file}")
    print(f"目标区域: APP2 (备份区)")
    print(f"串口: {SERIAL_PORT}")
    print(f"波特率: {BAUDRATE}")
    print("=" * 50)
    
    uploader = YmodemUploader()
    
    if uploader.upload_firmware(bin_file):
        print("\n" + "=" * 50)
        print("✓ 固件上传完成!")
        print("系统将在重启后自动复制APP2到APP1并运行")
        print("=" * 50)
        sys.exit(0)
    else:
        print("\n" + "=" * 50)
        print("✗ 固件上传失败!")
        print("=" * 50)
        sys.exit(1)


if __name__ == '__main__':
    main()