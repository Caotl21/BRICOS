#!/bin/bash
################################################################################
# STM32 Ymodem固件上传脚本 (Bash版本)
# 
# 使用方法:
#   ./upload_ymodem.sh <bin_file> <app_num>
#
# 示例:
#   ./upload_ymodem.sh app1.bin 1
#   ./upload_ymodem.sh app2.bin 2
#
# 依赖:
#   sudo apt install lrzsz minicom
################################################################################

# 配置
SERIAL_PORT="/dev/ttyTHS1"
BAUDRATE="115200"

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 检查参数
if [ $# -lt 2 ]; then
    echo "用法: $0 <bin_file> <app_num>"
    echo "示例: $0 app1.bin 1"
    exit 1
fi

BIN_FILE=$1
APP_NUM=$2

# 检查文件是否存在
if [ ! -f "$BIN_FILE" ]; then
    echo -e "${RED}错误: 文件 $BIN_FILE 不存在!${NC}"
    exit 1
fi

# 检查APP编号
if [ "$APP_NUM" != "1" ] && [ "$APP_NUM" != "2" ]; then
    echo -e "${RED}错误: APP编号必须是 1 或 2${NC}"
    exit 1
fi

# 检查sz命令
if ! command -v sz &> /dev/null; then
    echo -e "${RED}错误: 未找到sz命令!${NC}"
    echo "请安装: sudo apt install lrzsz"
    exit 1
fi

# 检查串口
if [ ! -e "$SERIAL_PORT" ]; then
    echo -e "${RED}错误: 串口 $SERIAL_PORT 不存在!${NC}"
    echo "可用串口:"
    ls /dev/ttyTHS* /dev/ttyUSB* 2>/dev/null
    exit 1
fi

# 显示信息
echo "========================================"
echo "STM32 Ymodem固件上传工具"
echo "========================================"
echo "固件文件: $BIN_FILE"
echo "文件大小: $(stat -c%s "$BIN_FILE") 字节"
echo "目标区域: APP$APP_NUM"
echo "串口: $SERIAL_PORT"
echo "波特率: $BAUDRATE"
echo "========================================"

# 配置串口
echo -e "${YELLOW}配置串口...${NC}"
stty -F $SERIAL_PORT $BAUDRATE cs8 -cstopb -parenb raw

# 进入Bootloader模式
echo -e "${YELLOW}尝试进入Bootloader模式...${NC}"
echo -n "B" > $SERIAL_PORT
sleep 1

# 读取响应
timeout 2 cat $SERIAL_PORT &
sleep 1
pkill -P $$ cat 2>/dev/null

# 发送下载命令
echo -e "${YELLOW}发送下载命令: APP$APP_NUM${NC}"
echo -n "$APP_NUM" > $SERIAL_PORT
sleep 1

# 读取响应
timeout 2 cat $SERIAL_PORT &
sleep 1
pkill -P $$ cat 2>/dev/null

# 使用sz上传文件
echo -e "${YELLOW}开始上传文件 (Ymodem协议)...${NC}"
echo "请等待..."

sz --ymodem "$BIN_FILE" < $SERIAL_PORT > $SERIAL_PORT

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ 文件上传成功!${NC}"
    
    # 等待处理完成
    sleep 2
    
    # 读取响应
    timeout 2 cat $SERIAL_PORT &
    sleep 1
    pkill -P $$ cat 2>/dev/null
    
    # 询问是否跳转
    echo ""
    read -p "是否立即跳转到应用程序? (y/n): " choice
    
    if [ "$choice" = "y" ] || [ "$choice" = "Y" ]; then
        echo -e "${YELLOW}发送跳转命令...${NC}"
        echo -n "J" > $SERIAL_PORT
        sleep 1
        
        # 读取响应
        timeout 2 cat $SERIAL_PORT &
        sleep 1
        pkill -P $$ cat 2>/dev/null
    fi
    
    echo ""
    echo "========================================"
    echo -e "${GREEN}✓ 固件上传完成!${NC}"
    echo "========================================"
    exit 0
else
    echo -e "${RED}✗ 文件上传失败!${NC}"
    echo "========================================"
    exit 1
fi

