/******************************************************************************
                        设备与IM948模块之间的串口通信库
版本: V1.05
记录: 1、增加 加速计和陀螺仪量程可设置
      2、增加 磁场校准开始命令
      3、增加 设置陀螺仪自动校正标识命令
      4、增加 设置静止节能模式的触发时长
      5、增加 设置上传圈数和支持数据透传
      6、增加 设置z角度为指定值
*******************************************************************************/
#include "im948_CMD.h"
#include "Serial.h"

struct_UartFifo UartFifo;
U8 targetDeviceAddress=255; // 通信地址，设为0-254指定则设备地址，设为255则不指定设备(即广播), 当需要使用485总线形式通信时通过该参数选中要操作的设备，若仅仅是串口1对1通信设为广播地址255即可

U8 CalcSum1(U8 *Buf, int Len)
{
    U8 Sum = 0;
    while (Len-- > 0)
    {
        Sum += Buf[Len];
    }
    return Sum;
}
void *Memcpy(void *s1, const void *s2, unsigned int n)
{
    char *p1 = s1;
    const char *p2 = s2;
    if (n)
    {
        n++;
        while (--n > 0)
        {
            *p1++ = *p2++;
        }
    }
    return s1;
}
#ifdef __Debug
void Dbp_U8_buf(char *sBeginInfo, char *sEndInfo,
                char *sFormat,
                const U8 *Buf, U32 Len)
{
    int i;
    if (sBeginInfo)
    {
        Dbp("%s", sBeginInfo);
    }
    for (i = 0; i < Len; i++)
    {
        Dbp(sFormat, Buf[i]);
    }
    if (sEndInfo)
    {
        Dbp("%s", sEndInfo);
    }
}
#endif


static void Cmd_Write(U8 *pBuf, int Len);
static void Cmd_RxUnpack(U8 *buf, U8 DLen);
/**
 * 发送CMD命令
 *
 * @param pDat 要发送的数据体
 * @param DLen 数据体的长度
 *
 * @return int 0=成功, -1=失败
 */
int Cmd_PackAndTx(U8 *pDat, U8 DLen)
{
    U8 buf[50 + 5 + CmdPacketMaxDatSizeTx] =
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff}; // 发送包缓存 开头50字节是前导码，用于唤醒可能处于睡眠状态的模块

    if((DLen == 0) || (DLen > CmdPacketMaxDatSizeTx) || (pDat==NULL))
    {// 非法参数
        return -1;
    }

    buf[50] = CmdPacket_Begin; // 起始码
    buf[51] = targetDeviceAddress; // 目前设备地址码
    buf[52] = DLen;  // 长度
    Memcpy(&buf[53], pDat, DLen); // 数据体
    buf[53+DLen] = CalcSum1(&buf[51], DLen+2);// CS 从 地址码开始算到数据体结束
    buf[54+DLen] = CmdPacket_End; // 结束码

    Cmd_Write(buf, DLen+55);
    return 0;
}
/**
 * 用于捕获数据包, 用户只需把接收到的每字节数据传入该函数即可
 * @param byte 传入接收到的每字节数据
 * @return U8 1=接收到完整数据包, 0未获取到完整数据包
 */
U8 Cmd_GetPkt(U8 byte)
{
    static U8 CS=0; // 校验和
    static U8 i=0;
    static U8 RxIndex=0;

    static U8 buf[5+CmdPacketMaxDatSizeRx]; // 接收包缓存
    #define cmdBegin    buf[0]  // 起始码
    #define cmdAddress  buf[1]  // 通信地址
    #define cmdLen      buf[2]  // 长度
    #define cmdDat     &buf[3]  // 数据体
    #define cmdCS       buf[3+cmdLen] // 校验和
    #define cmdEnd      buf[4+cmdLen] // 结束码

    CS += byte; // 边收数据边计算校验码，校验码为地址码开始(包含地址码)到校验码之前的数据的和
    switch (RxIndex)
    {
    case 0: // 起始码
        if (byte == CmdPacket_Begin)
        {
            i = 0;
            buf[i++] = CmdPacket_Begin;
            CS = 0; // 下个字节开始计算校验码
            RxIndex = 1;
        }
        break;
    case 1: // 数据体的地址码
        buf[i++] = byte;
        if (byte == 255)
        { // 255是广播地址，模块作为从机，它的地址不可会出现255
            RxIndex = 0;
            break;
        }
        RxIndex++;
        break;
    case 2: // 数据体的长度
        buf[i++] = byte;
        if ((byte > CmdPacketMaxDatSizeRx) || (byte == 0))
        { // 长度无效
            RxIndex = 0;
            break;
        }
        RxIndex++;
        break;
    case 3: // 获取数据体的数据
        buf[i++] = byte;
        if (i >= cmdLen+3)
        { // 已收完数据体
            RxIndex++;
        }
        break;
    case 4: // 对比 效验码
        CS -= byte;
        if (CS == byte)
        {// 校验正确
            buf[i++] = byte;
            RxIndex++;
        }
        else
        {// 校验失败
            RxIndex = 0;
        }
        break;
    case 5: // 结束码
        RxIndex = 0;
        if (byte == CmdPacket_End)
        {// 捕获到完整包
            buf[i++] = byte;

            if ((targetDeviceAddress == cmdAddress) || (targetDeviceAddress == 255))
            {// 地址匹配，是目标设备发来的数据 才处理
                Dbp_U8_buf("rx: ", "\r\n",
                           "%02X ",
                           buf, i);
                Cmd_RxUnpack(&buf[3], i-5); // 处理数据包的数据体
                return 1;
            }
        }
        break;
    default:
        RxIndex = 0;
        break;
    }

    return 0;
}


// ================================模块的操作指令=================================

// 睡眠传感器
void Cmd_02(void)
{
    U8 buf[1] = {0x02};
    Dbp("\r\nsensor off--\r\n");
    Cmd_PackAndTx(buf, 1);
}
// 唤醒传感器
void Cmd_03(void)
{
    U8 buf[1] = {0x03};
    Dbp("\r\nsensor on--\r\n");
    Cmd_PackAndTx(buf, 1);
}
// 关闭数据主动上报
void Cmd_18(void)
{
    U8 buf[1] = {0x18};
    Dbp("\r\nauto report off--\r\n");
    Cmd_PackAndTx(buf, 1);
}
// 开启数据主动上报
void Cmd_19(void)
{
    U8 buf[1] = {0x19};
    Dbp("\r\nauto report on--\r\n");
    Cmd_PackAndTx(buf, 1);
}
// 获取1次订阅的功能数据
void Cmd_11(void)
{
    U8 buf[1] = {0x11};
    Dbp("\r\nget report--\r\n");
    Cmd_PackAndTx(buf, 1);
}
// 获取设备属性和状态
void Cmd_10(void)
{
    U8 buf[1] = {0x10};
    Dbp("\r\nget report--\r\n");
    Cmd_PackAndTx(buf, 1);
}
/**
 * 设置设备参数
 * @param accStill    惯导-静止状态加速度阀值 单位dm/s?
 * @param stillToZero 惯导-静止归零速度(单位cm/s) 0:不归零 255:立即归零
 * @param moveToZero  惯导-动态归零速度(单位cm/s) 0:不归零
 * @param isCompassOn 1=需融合磁场 0=不融合磁场
 * @param barometerFilter 气压计的滤波等级[取值0-3],数值越大越平稳但实时性越差
 * @param reportHz 数据主动上报的传输帧率[取值0-250HZ], 0表示0.5HZ
 * @param gyroFilter    陀螺仪滤波系数[取值0-2],数值越大越平稳但实时性越差
 * @param accFilter     加速计滤波系数[取值0-4],数值越大越平稳但实时性越差
 * @param compassFilter 磁力计滤波系数[取值0-9],数值越大越平稳但实时性越差
 * @param Cmd_ReportTag 功能订阅标识
 */
void Cmd_12(U8 accStill, U8 stillToZero, U8 moveToZero,  U8 isCompassOn, U8 barometerFilter, U8 reportHz, U8 gyroFilter, U8 accFilter, U8 compassFilter, U16 Cmd_ReportTag)
{
    U8 buf[11] = {0x12};
    buf[1] = accStill;
    buf[2] = stillToZero;
    buf[3] = moveToZero;
    buf[4] = ((barometerFilter&3)<<1) | (isCompassOn&1); // bit[2-1]: BMP280的滤波等级[取值0-3]   bit[0]: 1=已开启磁场 0=已关闭磁场
    buf[5] = reportHz;
    buf[6] = gyroFilter;
    buf[7] = accFilter;
    buf[8] = compassFilter;
    buf[9] = Cmd_ReportTag&0xff;
    buf[10] = (Cmd_ReportTag>>8)&0xff;
    Dbp("\r\nset parameters--\r\n");
    Cmd_PackAndTx(buf, 11);
}
// 惯导三维空间位置清零
void Cmd_13(void)
{
    U8 buf[1] = {0x13};
    Dbp("\r\nclear INS position--\r\n");
    Cmd_PackAndTx(buf, 1);
}
// 计步数清零
void Cmd_16(void)
{
    U8 buf[1] = {0x16};
    Dbp("\r\nclear steps--\r\n");
    Cmd_PackAndTx(buf, 1);
}
// 恢复出厂校准参数
void Cmd_14(void)
{
    U8 buf[1] = {0x14};
    Dbp("\r\nRestore calibration parameters from factory mode--\r\n");
    Cmd_PackAndTx(buf, 1);
}
// 保存当前校准参数为出厂校准参数
void Cmd_15(void)
{
    U8 buf[3] = {0x15, 0x88, 0x99};
    Dbp("\r\nSave calibration parameters to factory mode--\r\n");
    Cmd_PackAndTx(buf, 3);
}
// 加速计简易校准 模块静止在水平面时，发送该指令并收到回复后等待9秒即可
void Cmd_07(void)
{
    U8 buf[1] = {0x07};
    Dbp("\r\nacceleration calibration--\r\n");
    Cmd_PackAndTx(buf, 1);
}
/**
 * 加速计高精度校准
 * @param flag 若模块未处于校准状态时：
 *                 值0 表示请求开始一次校准并采集1个数据
 *                 值255 表示询问设备是否正在校准
 *             若模块正在校准中:
 *                 值1 表示要采集下1个数据
 *                 值255 表示要采集最后1个数据并结束
 */
void Cmd_17(U8 flag)
{
    U8 buf[2] = {0x17};
    buf[1] = flag;
    if (flag == 0)
    {// 请求开始1次校准
        Dbp("\r\ncalibration request start--\r\n");
    }
    else if (flag == 1)
    {// 请求采集下个数据
        Dbp("calibration request next point--\r\n");
    }
    else if (flag == 255)
    {// 请求采集最后一个数据并结束当前进行中的校准
        Dbp("calibration request stop--\r\n");
    }
    else
    {
        return;
    }
    Cmd_PackAndTx(buf, 2);
}
// 开始磁力计校准
void Cmd_32(void)
{
    U8 buf[1] = {0x32};
    Dbp("\r\ncompass calibrate begin--\r\n");
    Cmd_PackAndTx(buf, 1);
}
// 结束磁力计校准
void Cmd_04(void)
{
    U8 buf[1] = {0x04};
    Dbp("\r\ncompass calibrate end--\r\n");
    Cmd_PackAndTx(buf, 1);
}
// z轴角归零
void Cmd_05(void)
{
    U8 buf[1] = {0x05};
    Dbp("\r\nz-axes to zero--\r\n");
    Cmd_PackAndTx(buf, 1);
}
// xyz世界坐标系清零
void Cmd_06(void)
{
    U8 buf[1] = {0x06};
    Dbp("\r\nWorldXYZ-axes to zero--\r\n");
    Cmd_PackAndTx(buf, 1);
}
// 恢复默认的自身坐标系Z轴指向及恢复默认的世界坐标系
void Cmd_08(void)
{
    U8 buf[1] = {0x08};
    Dbp("\r\naxesZ WorldXYZ-axes to zero--\r\n");
    Cmd_PackAndTx(buf, 1);
}
/**
 * 设置PCB安装方向矩阵
 * @param accMatrix 加速计方向矩阵
 * @param comMatrix 磁力计方向矩阵
 */
void Cmd_20(S8 *accMatrix, S8 *comMatrix)
{
    U8 buf[19] = {0x20};
    Memcpy(&buf[1],  accMatrix, 9);
    Memcpy(&buf[10], comMatrix, 9);
    Dbp("\r\nz-axes to zero--\r\n");
    Cmd_PackAndTx(buf, 19);
}
// 读取PCB安装方向矩阵
void Cmd_21(void)
{
    U8 buf[1] = {0x21};
    Dbp("\r\nget PCB direction--\r\n");
    Cmd_PackAndTx(buf, 1);
}
/**
 * 设置蓝牙广播名称
 * @param bleName 蓝牙名称(最多支持15个字符长度,不支持中文)
 */
void Cmd_22(U8 *bleName)
{
    U8 buf[17] = {0x22};
    Memcpy(&buf[1],  bleName, 16);
    Dbp("\r\nset BLE name--\r\n");
    Cmd_PackAndTx(buf, 17);
}
// 读取蓝牙广播名称
void Cmd_23(void)
{
    U8 buf[1] = {0x23};
    Dbp("\r\nget BLE name--\r\n");
    Cmd_PackAndTx(buf, 1);
}
/**
 * 设置关机电压和充电参数
 * @param PowerDownVoltageFlag 关机电压选择 0=3.4V(锂电池用) 1=2.7V(其它干电池用)
 * @param charge_full_mV  充电截止电压 0:3962mv 1:4002mv 2:4044mv 3:4086mv 4:4130mv 5:4175mv 6:4222mv 7:4270mv 8:4308mv 9:4349mv 10:4391mv
 * @param charge_full_mA 充电截止电流 0:2ma 1:5ma 2:7ma 3:10ma 4:15ma 5:20ma 6:25ma 7:30ma
 * @param charge_mA      充电电流 0:20ma 1:30ma 2:40ma 3:50ma 4:60ma 5:70ma 6:80ma 7:90ma 8:100ma 9:110ma 10:120ma 11:140ma 12:160ma 13:180ma 14:200ma 15:220ma
 */
void Cmd_24(U8 PowerDownVoltageFlag, U8 charge_full_mV, U8 charge_full_mA, U8 charge_mA)
{
    U8 buf[5] = {0x24};
    buf[1] = (PowerDownVoltageFlag <= 1)? PowerDownVoltageFlag:1;
    buf[2] = (charge_full_mV <= 10)? charge_full_mV:10;
    buf[3] = (charge_full_mA <= 7)? charge_full_mA:7;
    buf[4] = (charge_mA <= 15)? charge_mA:15;
    Dbp("\r\nset PowerDownVoltage and charge parameters--\r\n");
    Cmd_PackAndTx(buf, 5);
}
// 读取 关机电压和充电参数
void Cmd_25(void)
{
    U8 buf[1] = {0x25};
    Dbp("\r\nget PowerDownVoltage and charge parameters--\r\n");
    Cmd_PackAndTx(buf, 1);
}
// 断开蓝牙连接 该指令不会有回复
void Cmd_26(void)
{
    U8 buf[1] = {0x26};
    Dbp("\r\nble disconnect--\r\n");
    Cmd_PackAndTx(buf, 1);
}
/**
 * 设置用户的GPIO引脚
 *
 * @param M 0=浮空输入, 1=上拉输入, 2=下拉输入, 3=输出0, 4=输出1
 */
void Cmd_27(U8 M)
{
    U8 buf[2] = {0x27};
    buf[1] = (M<<4)&0xf0;
    Dbp("\r\nset gpio--\r\n");
    Cmd_PackAndTx(buf, 2);
}

// 设备重启
void Cmd_2A(void)
{
    U8 buf[1] = {0x2A};
    Dbp("\r\nreset--\r\n");
    Cmd_PackAndTx(buf, 1);
}
// 设备关机
void Cmd_2B(void)
{
    U8 buf[1] = {0x2B};
    Dbp("\r\npower off--\r\n");
    Cmd_PackAndTx(buf, 1);
}

/**
 * 设置 空闲关机时长
 *
 * @param idleToPowerOffTime 当串口没有通信且蓝牙在广播中，连续计时达到这么多个10分钟则关机  0=不关机
 */
void Cmd_2C(U8 idleToPowerOffTime)
{
    U8 buf[2] = {0x2C};
    buf[1] = idleToPowerOffTime;
    Dbp("\r\nset idleToPowerOffTime--\r\n");
    Cmd_PackAndTx(buf, 2);
}
// 读取 空闲关机时长
void Cmd_2D(void)
{
    U8 buf[1] = {0x2D};
    Dbp("\r\nget idleToPowerOffTime--\r\n");
    Cmd_PackAndTx(buf, 1);
}

/**
 * 设置 禁止蓝牙方式更改名称和充电参数 标识
 *
 * @param DisableBleSetNameAndCahrge 1=禁止通过蓝牙更改名称及充电参数 0=允许(默认) 可能客户的产品不想让别人用蓝牙随便改，设为1即可
 */
void Cmd_2E(U8 DisableBleSetNameAndCahrge)
{
    U8 buf[2] = {0x2E};
    buf[1] = (DisableBleSetNameAndCahrge <= 1)? DisableBleSetNameAndCahrge:1;
    Dbp("\r\nset FlagForDisableBleSetNameAndCahrge--\r\n");
    Cmd_PackAndTx(buf, 2);
}
// 读取 禁止蓝牙方式更改名称和充电参数 标识
void Cmd_2F(void)
{
    U8 buf[1] = {0x2F};
    Dbp("\r\nget FlagForDisableBleSetNameAndCahrge--\r\n");
    Cmd_PackAndTx(buf, 1);
}

/**
 * 设置 串口通信地址
 *
 * @param address 设备地址只能设置为0-254
 */
void Cmd_30(U8 address)
{
    U8 buf[2] = {0x30};
    buf[1] = (address <= 254)? address:254;
    Dbp("\r\nset address--\r\n");
    Cmd_PackAndTx(buf, 2);
}
// 读取 串口通信地址
void Cmd_31(void)
{
    U8 buf[1] = {0x31};
    Dbp("\r\nget address--\r\n");
    Cmd_PackAndTx(buf, 1);
}

/**
 * 设置 加速计和陀螺仪量程
 *
 * @param AccRange  目标加速度量程 0=2g 1=4g 2=8g 3=16g
 * @param GyroRange 目标陀螺仪量程 0=256 1=512 2=1024 3=2048
 */
void Cmd_33(U8 AccRange, U8 GyroRange)
{
    U8 buf[3] = {0x33};
    buf[1] = (AccRange <= 3)? AccRange:3;
    buf[2] = (GyroRange <= 3)? GyroRange:3;
    Dbp("\r\nset accelRange and gyroRange--\r\n");
    Cmd_PackAndTx(buf, 3);
}
// 读取 加速计和陀螺仪量程
void Cmd_34(void)
{
    U8 buf[1] = {0x34};
    Dbp("\r\nget accelRange and gyroRange--\r\n");
    Cmd_PackAndTx(buf, 1);
}

/**
 * 设置 陀螺仪自动校正标识
 *
 * @param GyroAutoFlag  1=陀螺仪自动校正灵敏度开  0=关
 */
void Cmd_35(U8 GyroAutoFlag)
{
    U8 buf[2] = {0x35};
    buf[1] = (GyroAutoFlag > 0)? 1:0;
    Dbp("\r\nset GyroAutoFlag--\r\n");
    Cmd_PackAndTx(buf, 2);
}
// 读取 加速计和陀螺仪量程
void Cmd_36(void)
{
    U8 buf[1] = {0x36};
    Dbp("\r\nget GyroAutoFlag--\r\n");
    Cmd_PackAndTx(buf, 1);
}

/**
 * 设置 静止节能模式的触发时长
 *
 * @param EcoTime_10s 该值大于0，则开启自动节能模式(即传感器睡眠后不主动上报，或静止EcoTime_10s个10秒自动进入运动监测模式且暂停主动上报)  0=不启用自动节能
 */
void Cmd_37(U8 EcoTime_10s)
{
    U8 buf[2] = {0x37};
    buf[1] = EcoTime_10s;
    Dbp("\r\nset EcoTime_10s--\r\n");
    Cmd_PackAndTx(buf, 2);
}
// 读取 加速计和陀螺仪量程
void Cmd_38(void)
{
    U8 buf[1] = {0x38};
    Dbp("\r\nget EcoTime_10s--\r\n");
    Cmd_PackAndTx(buf, 1);
}

/**
 * 设置 Z轴角度为指定值
 * @param val 要设置的角度值(单位0.001°)有符号32位数
 */
void Cmd_28(S32 val)
{
    U8 buf[5] = {0x28};
    buf[1] = (U8)(val & 0xFF);
    buf[2] = (U8)((val >> 8) & 0xFF);
    buf[3] = (U8)((val >> 16) & 0xFF);
    buf[4] = (U8)((val >> 24) & 0xFF);
    Dbp("\r\nset AngleZ--\r\n");
    Cmd_PackAndTx(buf, 5);
}

/**
 * 设置 开机后工作模式
 *
 * @param Flag 取值2=开机时载入配置参数并开启串口主动上报，1=开机时载入配置参数但串口不主动上报, 0=不载入且串口不主动上报
 */
void Cmd_40(U8 Flag)
{
    U8 buf[2] = {0x40};
    buf[1] = Flag;
    Dbp("\r\nset WorkMode--\r\n");
    Cmd_PackAndTx(buf, 2);
}
// 读取 开机后工作模式
void Cmd_41(void)
{
    U8 buf[1] = {0x41};
    Dbp("\r\nget WorkMode--\r\n");
    Cmd_PackAndTx(buf, 1);
}

/**
 * 设置 当前高度为指定值
 *
 * @param val 要设置的高度值 单位为mm
 */
void Cmd_42(S32 val)
{
    U8 buf[5] = {0x42};
    buf[1] = (U8)(val & 0xFF);
    buf[2] = (U8)((val >> 8) & 0xFF);
    buf[3] = (U8)((val >> 16) & 0xFF);
    buf[4] = (U8)((val >> 24) & 0xFF);
    Dbp("\r\nset height--\r\n");
    Cmd_PackAndTx(buf, 5);
}
/**
 * 设置 自动补偿高度标识
 *
 * @param OnOff 0=关闭 1=开启
 */
void Cmd_43(U8 OnOff)
{
    U8 buf[2] = {0x43};
    buf[1] = OnOff;
    Dbp("\r\nset HeightAutoFlag--\r\n");
    Cmd_PackAndTx(buf, 2);
}
// 读取 自动补偿高度标识
void Cmd_44(void)
{
    U8 buf[1] = {0x44};
    Dbp("\r\nget HeightAutoFlag--\r\n");
    Cmd_PackAndTx(buf, 1);
}

/**
 * 设置 串口波特率
 *
 * @param BaudRate 目标波特率 0=9600 1=115200 2=230400 3=460800
 */
void Cmd_47(U8 BaudRate)
{
    U8 buf[2] = {0x47};
    buf[1] = BaudRate;
    Dbp("\r\nset BaudRate--\r\n");
    Cmd_PackAndTx(buf, 2);
}
// 读取 串口波特率
void Cmd_48(void)
{
    U8 buf[1] = {0x48};
    Dbp("\r\nget BaudRate--\r\n");
    Cmd_PackAndTx(buf, 1);
}

// 闪烁几下led指示灯
void Cmd_49(void)
{
    U8 buf[1] = {0x49};
    Dbp("\r\nLed blink--\r\n");
    Cmd_PackAndTx(buf, 1);
}

/**
 * 数据透传
 *
 * @param TxBuf 要透传的数据
 * @param TxLen 字节数 必须小于CmdPacketMaxDatSizeTx
 */
void Cmd_50(U8 *TxBuf, U8 TxLen)
{
    U8 buf[CmdPacketMaxDatSizeTx]={0x50};
    if (TxLen >= CmdPacketMaxDatSizeTx)
    {// 太长了，不传输
        Dbp("\r\nErr:Cmd_50--\r\n");
        return;
    }
    Memcpy(&buf[1], TxBuf, TxLen);
    Cmd_PackAndTx(buf, TxLen + 1);
}

/**
 * 设置 是否上传总圈数并清零
 *
 * @param isReportCycle 0=传输欧拉角数据, 1=用总圈数代替欧拉角传输 并清零圈数
 */
void Cmd_51(U8 isReportCycle)
{
    U8 buf[3] = {0x51};
    if (isReportCycle)
    {
        buf[1] = 0xAA; buf[2] = 0xBB;
        Dbp("\r\nis Report Cycle--\r\n");
    }
    else
    {
        buf[1] = 0; buf[2] = 0;
        Dbp("\r\nis Report Euler--\r\n");
    }
    Cmd_PackAndTx(buf, 3);
}

F32 AngleX,AngleY,AngleZ;// 从Cmd_RxUnpack中获取到的欧拉角数据更新到全局变量以便用户自己的业务逻辑使用, 若还需要其它数据，可参考进行增加即可
F32 AccX,AccY,AccZ, Accabs; // 加速度
F32 GyroX,GyroY,GyroZ, Gyroabs; // 陀螺仪
F32 MagX,MagY,MagZ, Magabs; // 磁力计

U8 isNewData_im948=0;// 1=更新了新的数据到全局变量里了
/**
 * 解析接收到报文的数据体并处理，用户根据项目需求，关注里面对应的内容即可--------------------
 * @param pDat 要解析的数据体
 * @param DLen 数据体的长度
 */
static void Cmd_RxUnpack(U8 *buf, U8 DLen)
{
    U16 ctl;
    U8 L;
    U8 tmpU8;
    U16 tmpU16;
    U32 tmpU32;
    F32 tmpX, tmpY, tmpZ, tmpAbs;

    switch (buf[0])
    {
    case 0x02: // 传感器 已睡眠 回复
        Dbp("\t sensor off success\r\n");
        break;
    case 0x03: // 传感器 已唤醒 回复
        Dbp("\t sensor on success\r\n");
        break;
    case 0x32: // 磁力计 开始校准 回复
        Dbp("\t compass calibrate begin\r\n");
        break;
    case 0x04: // 磁力计 结束校准 回复
        Dbp("\t compass calibrate end\r\n");
        break;
    case 0x05: // z轴角 已归零 回复
        Dbp("\t z-axes to zero success\r\n");
        break;
    case 0x06: // 请求 xyz世界坐标系清零 回复
        Dbp("\t WorldXYZ-axes to zero success\r\n");
        break;
    case 0x07: // 加速计简单校准正在进行，将在9秒后完成  回复
        Dbp("\t acceleration calibration, Hold still for 9 seconds\r\n");
        break;
    case 0x08: // 恢复默认的自身坐标系Z轴指向及恢复默认的世界坐标系  回复
        Dbp("\t axesZ WorldXYZ-axes to zero success\r\n");
        break;
    case 0x10: // 模块当前的属性和状态 回复
        Dbp("\t still limit: %u\r\n", buf[1]);   // 字节1 惯导-静止状态加速度阀值 单位dm/s?
        Dbp("\t still to zero: %u\r\n", buf[2]); // 字节2 惯导-静止归零速度(单位mm/s) 0:不归零 255:立即归零
        Dbp("\t move to zero: %u\r\n", buf[3]);  // 字节3 惯导-动态归零速度(单位mm/s) 0:不归零
        Dbp("\t compass: %s\r\n", ((buf[4]>>0) & 0x01)? "on":"off" );     // 字节4 bit[0]: 1=已开启磁场 0=已关闭磁场
        Dbp("\t barometer filter: %u\r\n", (buf[4]>>1) & 0x03);           // 字节4 bit[1-2]: 气压计的滤波等级[取值0-3],数值越大越平稳但实时性越差
        Dbp("\t IMU: %s\r\n", ((buf[4]>>3) & 0x01)? "on":"off" );         // 字节4 bit[3]: 1=传感器已开启  0=传感器已睡眠
        Dbp("\t auto report: %s\r\n", ((buf[4]>>4) & 0x01)? "on":"off" ); // 字节4 bit[4]: 1=已开启传感器数据主动上报 0=已关闭传感器数据主动上报
        Dbp("\t FPS: %u\r\n", buf[5]); // 字节5 数据主动上报的传输帧率[取值0-250HZ], 0表示0.5HZ
        Dbp("\t gyro filter: %u\r\n", buf[6]);    // 字节6 陀螺仪滤波系数[取值0-2],数值越大越平稳但实时性越差
        Dbp("\t acc filter: %u\r\n", buf[7]);     // 字节7 加速计滤波系数[取值0-4],数值越大越平稳但实时性越差
        Dbp("\t compass filter: %u\r\n", buf[8]); // 字节8 磁力计滤波系数[取值0-9],数值越大越平稳但实时性越差
        Dbp("\t subscribe tag: 0x%04X\r\n", (U16)(((U16)buf[10]<<8) | buf[9])); // 字节[10-9] 功能订阅标识
        Dbp("\t charged state: %u\r\n", buf[11]); // 字节11 充电状态指示 0=未接电源 1=充电中 2=已充满
        Dbp("\t battery level: %u%%\r\n", buf[12]); // 字节12 当前剩余电量[0-100%]
        Dbp("\t battery voltage: %u mv\r\n", (U16)(((U16)buf[14]<<8) | buf[13])); // 字节[14-13] 电池的当前电压mv
        Dbp("\t Mac: %02X:%02X:%02X:%02X:%02X:%02X\r\n", buf[15],buf[16],buf[17],buf[18],buf[19],buf[20]); // 字节[15-20] MAC地址
        Dbp("\t version: %s\r\n", &buf[21]); // 字节[21-26] 固件版本 字符串
        Dbp("\t product model: %s\r\n", &buf[27]); // 字节[26-32] 产品型号 字符串
        break;
    case 0x11: // 获取订阅的功能数据 回复或主动上报
        ctl = ((U16)buf[2] << 8) | buf[1];// 字节[2-1] 为功能订阅标识，指示当前订阅了哪些功能
        Dbp("\t subscribe tag: 0x%04X\r\n", ctl);
        Dbp("\t ms: %lu\r\n", (U32)(((U32)buf[6]<<24) | ((U32)buf[5]<<16) | ((U32)buf[4]<<8) | ((U32)buf[3]<<0))); // 字节[6-3] 为模块开机后的时间戳(单位ms)

        L =7; // 从第7字节开始根据 订阅标识tag来解析剩下的数据
        if ((ctl & 0x0001) != 0)
        {// 加速度xyz 去掉了重力 使用时需*scaleAccel m/s
            tmpX = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleAccel; L += 2; Dbp("\taX: %.3f\r\n", tmpX); // x加速度aX
            tmpY = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleAccel; L += 2; Dbp("\taY: %.3f\r\n", tmpY); // y加速度aY
            tmpZ = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleAccel; L += 2; Dbp("\taZ: %.3f\r\n", tmpZ); // z加速度aZ
            tmpAbs = sqrt(pow2(tmpX) + pow2(tmpY) + pow2(tmpZ)); Dbp("\ta_abs: %.3f\r\n", tmpAbs); // 3轴合成的绝对值
        }
        if ((ctl & 0x0002) != 0)
        {// 加速度xyz 包含了重力 使用时需*scaleAccel m/s
            tmpX = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleAccel; L += 2; Dbp("\tAX: %.3f\r\n", tmpX); // x加速度AX
            tmpY = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleAccel; L += 2; Dbp("\tAY: %.3f\r\n", tmpY); // y加速度AY
            tmpZ = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleAccel; L += 2; Dbp("\tAZ: %.3f\r\n", tmpZ); // z加速度AZ
            tmpAbs = sqrt(pow2(tmpX) + pow2(tmpY) + pow2(tmpZ)); Dbp("\tA_abs: %.3f\r\n", tmpAbs); // 3轴合成的绝对值
            AccX = tmpX; AccY = tmpY; AccZ = tmpZ; Accabs = tmpAbs;
        }
        if ((ctl & 0x0004) != 0)
        {// 角速度xyz 使用时需*scaleAngleSpeed °/s
            tmpX = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleAngleSpeed; L += 2; Dbp("\tGX: %.3f\r\n", tmpX); // x角速度GX
            tmpY = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleAngleSpeed; L += 2; Dbp("\tGY: %.3f\r\n", tmpY); // y角速度GY
            tmpZ = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleAngleSpeed; L += 2; Dbp("\tGZ: %.3f\r\n", tmpZ); // z角速度GZ
            tmpAbs = sqrt(pow2(tmpX) + pow2(tmpY) + pow2(tmpZ)); Dbp("\tG_abs: %.3f\r\n", tmpAbs); // 3轴合成的绝对值
            GyroX = tmpX; GyroY = tmpY; GyroZ = tmpZ; Gyroabs = tmpAbs;
        }
        if ((ctl & 0x0008) != 0)
        {// 磁场xyz 使用时需*scaleMag uT
            tmpX = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleMag; L += 2; Dbp("\tCX: %.3f\r\n", tmpX); // x磁场CX
            tmpY = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleMag; L += 2; Dbp("\tCY: %.3f\r\n", tmpY); // y磁场CY
            tmpZ = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleMag; L += 2; Dbp("\tCZ: %.3f\r\n", tmpZ); // z磁场CZ
            tmpAbs = sqrt(pow2(tmpX) + pow2(tmpY) + pow2(tmpZ)); Dbp("\tC_abs: %.3f\r\n", tmpAbs); // 3轴合成的绝对值
            MagX = tmpX; MagY = tmpY; MagZ = tmpZ; Magabs = tmpAbs;
        }
        if ((ctl & 0x0010) != 0)
        {// 温度 气压 高度
            tmpX = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleTemperature; L += 2; Dbp("\ttemperature: %.2f\r\n", tmpX); // 温度

            tmpU32 = (U32)(((U32)buf[L+2] << 16) | ((U32)buf[L+1] << 8) | (U32)buf[L]);
            tmpU32 = ((tmpU32 & 0x800000) == 0x800000)? (tmpU32 | 0xff000000) : tmpU32;// 若24位数的最高位为1则该数值为负数，需转为32位负数，直接补上ff即可
            tmpY = (S32)tmpU32 * scaleAirPressure; L += 3; Dbp("\tairPressure: %.3f\r\n", tmpY); // 气压

            tmpU32 = (U32)(((U32)buf[L+2] << 16) | ((U32)buf[L+1] << 8) | (U32)buf[L]);
            tmpU32 = ((tmpU32 & 0x800000) == 0x800000)? (tmpU32 | 0xff000000) : tmpU32;// 若24位数的最高位为1则该数值为负数，需转为32位负数，直接补上ff即可
            tmpZ = (S32)tmpU32 * scaleHeight; L += 3; Dbp("\theight: %.3f\r\n", tmpZ); // 高度
        }
        if ((ctl & 0x0020) != 0)
        {// 四元素 wxyz 使用时需*scaleQuat
            tmpAbs = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleQuat; L += 2; Dbp("\tw: %.3f\r\n", tmpAbs); // w
            tmpX =   (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleQuat; L += 2; Dbp("\tx: %.3f\r\n", tmpX); // x
            tmpY =   (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleQuat; L += 2; Dbp("\ty: %.3f\r\n", tmpY); // y
            tmpZ =   (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleQuat; L += 2; Dbp("\tz: %.3f\r\n", tmpZ); // z
        }
        if ((ctl & 0x0040) != 0)
        {// 欧拉角xyz 使用时需*scaleAngle
            tmpX = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleAngle; L += 2; Dbp("\tangleX: %.3f\r\n", tmpX); // x角度
            tmpY = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleAngle; L += 2; Dbp("\tangleY: %.3f\r\n", tmpY); // y角度
            tmpZ = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleAngle; L += 2; Dbp("\tangleZ: %.3f\r\n", tmpZ); // z角度
            AngleX = tmpX;
            AngleY = tmpY;
            AngleZ = tmpZ;
        }
        if ((ctl & 0x0080) != 0)
        {// xyz 空间位移 单位mm 转为 m
            tmpX = (S16)(((S16)buf[L+1]<<8) | buf[L]) / 1000.0f; L += 2; Dbp("\toffsetX: %.3f\r\n", tmpX); // x坐标
            tmpY = (S16)(((S16)buf[L+1]<<8) | buf[L]) / 1000.0f; L += 2; Dbp("\toffsetY: %.3f\r\n", tmpY); // y坐标
            tmpZ = (S16)(((S16)buf[L+1]<<8) | buf[L]) / 1000.0f; L += 2; Dbp("\toffsetZ: %.3f\r\n", tmpZ); // z坐标
        }
        if ((ctl & 0x0100) != 0)
        {// 活动检测数据
            tmpU32 = (U32)(((U32)buf[L+3]<<24) | ((U32)buf[L+2]<<16) | ((U32)buf[L+1]<<8) | ((U32)buf[L]<<0)); L += 4; Dbp("\tsteps: %u\r\n", tmpU32); // 计步数
            tmpU8 = buf[L]; L += 1;
            Dbp("\t walking: %s\r\n", (tmpU8 & 0x01)?  "yes" : "no"); // 是否在走路
            Dbp("\t running: %s\r\n", (tmpU8 & 0x02)?  "yes" : "no"); // 是否在跑步
            Dbp("\t biking: %s\r\n",  (tmpU8 & 0x04)?  "yes" : "no"); // 是否在骑车
            Dbp("\t driving: %s\r\n", (tmpU8 & 0x08)?  "yes" : "no"); // 是否在开车
        }
        if ((ctl & 0x0200) != 0)
        {// 加速度xyz 去掉了重力 使用时需*scaleAccel m/s
            tmpX = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleAccel; L += 2; Dbp("\tasX: %.3f\r\n", tmpX); // x加速度asX
            tmpY = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleAccel; L += 2; Dbp("\tasY: %.3f\r\n", tmpY); // y加速度asY
            tmpZ = (S16)(((S16)buf[L+1]<<8) | buf[L]) * scaleAccel; L += 2; Dbp("\tasZ: %.3f\r\n", tmpZ); // z加速度asZ
            tmpAbs = sqrt(pow2(tmpX) + pow2(tmpY) + pow2(tmpZ)); Dbp("\tas_abs: %.3f\r\n", tmpAbs); // 3轴合成的绝对值
        }
        if ((ctl & 0x0400) != 0)
        {// ADC的值
            tmpU16 = (U16)(((U16)buf[L+1]<<8) | ((U16)buf[L]<<0)); L += 2; Dbp("\tadc: %u\r\n", tmpU16); // 单位mv
        }
        if ((ctl & 0x0800) != 0)
        {// GPIO1的值
            tmpU8 = buf[L]; L += 1;
            Dbp("\t GPIO1  M:%X, N:%X\r\n", (tmpU8>>4)&0x0f, (tmpU8)&0x0f);
        }
        isNewData_im948 = 1; // 更新了新数据
        break;
    case 0x12: // 设置参数 回复
        Dbp("\t set parameters success\r\n");
        break;
    case 0x13: // 惯导三维空间位置清零 回复
        Dbp("\t clear INS position success\r\n");
        break;
    case 0x14: // 恢复出厂校准参数 回复
        Dbp("\t Restore calibration parameters from factory mode success\r\n");
        break;
    case 0x15: // 保存当前校准参数为出厂校准参数 回复
        Dbp("\t Save calibration parameters to factory mode success\r\n");
        break;
    case 0x16: // 计步数清零 回复
        Dbp("\t clear steps success\r\n");
        break;
    case 0x17: // 加速计高精度校准 回复
        if (buf[1] == 255)
        {// 字节1 值255 表示采集完成，正在结束校准(设备需继续保持静止等待10秒钟)
            Dbp("\t calibration success, please wait 10 seconds\r\n");
        }
        else if (buf[1] == 254)
        {// 字节1 值254 表示陀螺仪自检失败
            Dbp("\t calibration fail, gyro error\r\n");
        }
        else if (buf[1] == 253)
        {// 字节1 值253 表示加速计自检失败
            Dbp("\t calibration fail, accelerometer error\r\n");
        }
        else if (buf[1] == 252)
        {// 字节1 值252 表示磁力计自检失败
            Dbp("\t calibration fail, compass error\r\n");
        }
        else if (buf[1] == 251)
        {// 字节1 值251 表示设备未在校准中
            Dbp("\t calibration fail, Hasn't started\r\n");
        }
        else if (buf[1] != 0)
        {// 值[1-250] 表示当前已采集的次数
            Dbp("\t calibration, Points collected is %u\r\n", buf[1]);
        }
        else
        {// 值0 表示模块已经在校准中
            Dbp("\t calibration is running\r\n");
        }
        break;
    case 0x18: // 已关闭主动上报 回复
        Dbp("\t auto report off\r\n");
        break;
    case 0x19: // 已打开主动上报 回复
        Dbp("\t auto report on\r\n");
        break;
    case 0x20: // 设置PCB安装方向矩阵 回复
        Dbp("\t set PCB direction success\r\n");
        break;
    case 0x21: // 是请求 读取安装方向矩阵
        Dbp_U8_buf("\t get PCB direction: 0x[", "]\r\n",
                   "%02x ",
                   &buf[1], 9); // 字节[1-9]     为加速计安装方向矩阵
        Dbp_U8_buf("\t get PCB direction: 0x[", "]\r\n",
                   "%02x ",
                   &buf[10], 9); // 字节[10-18] 为磁力计安装方向矩阵
        break;
    case 0x22: // 是请求 设置蓝牙广播名称
        Dbp("\t set BLE name success\r\n");
        break;
    case 0x23: // 读取蓝牙广播名称 回复
        Dbp("\t get BLE name: %s\r\n", &buf[1]); // 字节[1-16] 为蓝牙广播名称字符串
        break;
    case 0x24: // 设置关机电压和充电参数 回复
        Dbp("\t set PowerDownVoltage and charge parameters success\r\n");
        break;
    case 0x25: // 读取关机电压和充电参数 回复
        Dbp("\t PowerDownVoltageFlag: %u\r\n", buf[1]); // 字节1 关机电压选择标志 0表示3.4V, 1表示2.7V
        Dbp("\t charge_full_mV: %u\r\n", buf[2]); // 字节2 充电截止电压 0:3962mv 1:4002mv 2:4044mv 3:4086mv 4:4130mv 5:4175mv 6:4222mv 7:4270mv 8:4308mv 9:4349mv 10:4391mv
        Dbp("\t charge_full_mA: %u ma\r\n", buf[3]); // 字节3 充电截止电流 0:2ma 1:5ma 2:7ma 3:10ma 4:15ma 5:20ma 6:25ma 7:30ma
        Dbp("\t charge_mA: %u ma\r\n", buf[4]); // 字节3 充电电流 0:20ma 1:30ma 2:40ma 3:50ma 4:60ma 5:70ma 6:80ma 7:90ma 8:100ma 9:110ma 10:120ma 11:140ma 12:160ma 13:180ma 14:200ma 15:220ma
        break;
    case 0x27: // 设置用户的GPIO引脚 回复
        Dbp("\t set gpio success\r\n");
        break;
    case 0x28: // 设置Z轴角度为指定值 回复
        Dbp("\t set AngleZ success\r\n");
        break;
    case 0x2A: // 重启设备 回复
        Dbp("\t will reset\r\n");
        break;
    case 0x2B: // 设备关机 回复
        Dbp("\t will power off\r\n");
        break;
    case 0x2C: // 设置空闲关机时长 回复
        Dbp("\t set idleToPowerOffTime success\r\n");
        break;
    case 0x2D: // 读取空闲关机时长 回复
        Dbp("\t idleToPowerOffTime:%u minutes\r\n", buf[1]*10);
        break;
    case 0x2E: // 设置禁止蓝牙方式更改名称和充电参数标识 回复
        Dbp("\t set FlagForDisableBleSetNameAndCahrge success\r\n");
        break;
    case 0x2F: // 读取禁止蓝牙方式更改名称和充电参数标识 回复
        Dbp("\t FlagForDisableBleSetNameAndCahrge:%u\r\n", buf[1]);
        break;
    case 0x30: // 设置串口通信地址 回复
        Dbp("\t set address success\r\n");
        break;
    case 0x31: // 读取串口通信地址 回复
        Dbp("\t address:%u\r\n", buf[1]);
        break;
    case 0x33: // 设置加速计和陀螺仪量程 回复
        Dbp("\t set accelRange and gyroRange success\r\n");
        break;
    case 0x34: // 读取加速计和陀螺仪量程 回复
        Dbp("\t accelRange:%u gyroRange:%u\r\n", buf[1], buf[2]);
        break;
    case 0x35: // 设置陀螺仪自动校正标识 回复
        Dbp("\t set GyroAutoFlag success\r\n");
        break;
    case 0x36: // 读取陀螺仪自动校正标识 回复
        Dbp("\t GyroAutoFlag:%u\r\n", buf[1]);
        break;
    case 0x37: // 设置静止节能模式的触发时长 回复
        Dbp("\t set EcoTime success\r\n");
        break;
    case 0x38: // 读取静止节能模式的触发时长 回复
        Dbp("\t EcoTime:%u\r\n", buf[1]);
        break;
    case 0x40: // 设置开机后工作模式 回复
        Dbp("\t set WorkMode success\r\n");
        break;
    case 0x41: // 读取开机后工作模式 回复
        Dbp("\t WorkMode:%u\r\n", buf[1]);
        break;		
    case 0x42: // 设置高度为指定值 回复
        Dbp("\t set Height success\r\n");
        break;
    case 0x43: // 设置自动补偿高度标识 回复
        Dbp("\t set HeightAutoFlag success\r\n");
        break;
    case 0x44: // 读取自动补偿高度标识 回复
        Dbp("\t HeightAutoFlag:%u\r\n", buf[1]);
        break;
    case 0x47: // 设置串口波特率 回复
        Dbp("\t set BaudRate success\r\n");
        break;
    case 0x48: // 读取串口波特率 回复
        Dbp("\t BaudRate:%u\r\n", buf[1]);
        break;
    case 0x50: // 是透传过来的数据 回复  把透传数据以十六进制打印出来
        Dbp_U8_buf("DTU: ", "\r\n",
           "%02X ",
           &buf[1], DLen-1);
        break;
    case 0x51: // 设置用圈数代替欧拉角传输 回复
        Dbp("\t set Cycle success\r\n");
        break;

    default:
        break;
    }
}

/**
 * 发送数据  需要用户实现里面的串口发送数据方法-------------------------------------
 * @param pBuf 要发送的内容指针
 * @param Len 要发送的字节数
 */
static void Cmd_Write(U8 *pBuf, int Len)
{
    // 通过UART_Write函数发送通信数据流，由用户针对底层硬件实现UART_Write函数把buf指针指向的Len字节数据发送出去即可
    extern int UART_Write(U8 n,  const U8 *buf, int Len);
    UART_Write(1,  pBuf, Len);

    Dbp_U8_buf("tx: ", "\r\n",
               "%02X ",
               pBuf, Len);
}












// ======================================测试示例==============================================
U8 im948_ctl = 0; // 用户调试口发来的1字节操作指令 调试口收到的数据赋值给im948_ctl 主循环里调用 im948_test() 进行演示
void im948_test(void)
{
    U8 ctl = im948_ctl;
    if (im948_ctl)
    {
        im948_ctl = 0;
    }

    switch (ctl)
    {
    case '1':// 1 睡眠传感器
        Cmd_02();
        break;
    case '2':// 2 唤醒传感器
        Cmd_03();
        break;

    case '3':// 3 关闭数据主动上报
        Cmd_18();
        break;
    case '4':// 4 开启数据主动上报
        Cmd_19();
        break;

    case '5':// 5 获取1次订阅的功能数据
        Cmd_11();
        break;

    case '6':// 6 获取设备属性和状态
        Cmd_10();
        break;
    case '7':// 7 设置设备参数(内容1)
        /**
         * 设置设备参数
         * @param accStill    惯导-静止状态加速度阀值 单位dm/s?
         * @param stillToZero 惯导-静止归零速度(单位cm/s) 0:不归零 255:立即归零
         * @param moveToZero  惯导-动态归零速度(单位cm/s) 0:不归零
         * @param isCompassOn 1=需开启磁场 0=需关闭磁场
         * @param barometerFilter 气压计的滤波等级[取值0-3],数值越大越平稳但实时性越差
         * @param reportHz 数据主动上报的传输帧率[取值0-250HZ], 0表示0.5HZ
         * @param gyroFilter    陀螺仪滤波系数[取值0-2],数值越大越平稳但实时性越差
         * @param accFilter     加速计滤波系数[取值0-4],数值越大越平稳但实时性越差
         * @param compassFilter 磁力计滤波系数[取值0-9],数值越大越平稳但实时性越差
         * @param Cmd_ReportTag 功能订阅标识
         */
        Cmd_12(5, 255, 0,  1, 3, 2, 2, 4, 9, 0xFFFF);
        break;
    case '8':// 8 设置设备参数(内容2)
        Cmd_12(8,   6, 5,  0, 1,30, 1, 2, 7, 0x0002);
        break;

    case '9':// 9 惯导三维空间位置清零
        Cmd_13();
        break;
    case 'a':// a 计步数清零
        Cmd_16();
        break;

    case 'b':// b 恢复出厂校准参数
        Cmd_14();
        break;
    case 'c':// c 保存当前校准参数为出厂校准参数
        Cmd_15();
        break;

    case 'd':// d 加速计简易校准
        Cmd_07();
        break;

    case 'e':// e 加速计高精度校准 开始
        /**
         * 加速计高精度校准
         * @param flag 若模块未处于校准状态时：
         *                 值0 表示请求开始一次校准并采集1个数据
         *                 值255 表示询问设备是否正在校准
         *             若模块正在校准中:
         *                 值1 表示要采集下1个数据
         *                 值255 表示要采集最后1个数据并结束
         */
        Cmd_17(0);
        break;
    case 'f':// f 加速计高精度校准 采集1个点 请最少采集6个静止面的点
        /**
         * 加速计高精度校准
         * @param flag 若模块未处于校准状态时：
         *                 值0 表示请求开始一次校准并采集1个数据
         *                 值255 表示询问设备是否正在校准
         *             若模块正在校准中:
         *                 值1 表示要采集下1个数据
         *                 值255 表示要采集最后1个数据并结束
         */
        Cmd_17(1);
        break;
    case 'g':// g 加速计高精度校准 采集最后1个数据并结束 或询问设备是否正在校准
        /**
         * 加速计高精度校准
         * @param flag 若模块未处于校准状态时：
         *                 值0 表示请求开始一次校准并采集1个数据
         *                 值255 表示询问设备是否正在校准
         *             若模块正在校准中:
         *                 值1 表示要采集下1个数据
         *                 值255 表示要采集最后1个数据并结束
         */
        Cmd_17(255);
        break;
    case 'H':// H 开始磁力计校准
        Cmd_32();
    case 'h':// h 结束磁力计校准
        Cmd_04();
        break;
    case 'i':// i z轴角归零
        Cmd_05();
        break;
    case 'j':// j xyz世界坐标系清零
        Cmd_06();
        break;
    case 'k':// k 恢复默认的自身坐标系Z轴指向及恢复默认的世界坐标系
        Cmd_08();
        break;

    case 'l':// l 设置PCB安装方向矩阵 为模块的丝印标识方向
        {
            S8 accMatrix[9] =
            {// 加速计与模块标识方向一致
                1, 0, 0,
                0, 1, 0,
                0, 0, 1
            };
            S8 comMatrix[9] =
            {// 磁力计与模块标识方向一致
                1, 0, 0,
                0,-1, 0,
                0, 0,-1
            };
            Cmd_20(accMatrix, comMatrix);
        }
        break;
    case 'm':// m 设置PCB安装方向矩阵 为模块的丝印标识方向绕x轴正转90度 竖着放
        {
            S8 accMatrix[9] =
            {// 竖放 即加速计绕模块的x轴正转90度  ok
                1, 0, 0,
                0, 0,-1,
                0, 1, 0
            };
            S8 comMatrix[9] =
            {// 竖放 即磁力计绕模块的x轴正转90度  ok
                1, 0, 0,
                0, 0, 1,
                0,-1, 0
            };
            Cmd_20(accMatrix, comMatrix);
        }
        break;
    case 'n': // n 读取PCB安装方向矩阵
        Cmd_21();
        break;

    case 'o': // o 设置蓝牙广播名称为 im948
        Cmd_22("im948");
        break;
    case 'p': // p 设置蓝牙广播名称为 helloBle
        Cmd_22("helloBle");
        break;
    case 'q': // q 读取蓝牙广播名称
        Cmd_23();
        break;

    case 'r': // r 设置为关机电压2.7V  充电截止电压4.22V  充电截止电流10ma 充电电流50ma  模块出厂默认就是这个配置
        Cmd_24(  1,                   6,                 3,              3);
        break;
    case 's': // s 设置为关机电压3.4V  充电截止电压4.22V  充电截止电流15ma  充电电流200ma
        Cmd_24(  0,                   6,                 4,              14);
        break;
    case 't': // t 读取 AD1引脚电压检测范围、电池类型、关机电压
        Cmd_25();
        break;

    case 'u': // u 断开蓝牙连接
        Cmd_26();
        break;

    case 'v': // v 设置用户的GPIO引脚 设为上拉输入
        Cmd_27(1);
        break;
    case 'w': // w 设置用户的GPIO引脚 设为下拉输入
        Cmd_27(2);
        break;


    case 'x': // x 重启设备
        Cmd_2A();
        break;
    case 'y': // y 设备关机
        Cmd_2B();
        break;

    case 'z': // z 设置空闲关机时长
        Cmd_2C(0); // 空闲不自动关机
        break;
    case 'A': // A 设置空闲关机时长
        Cmd_2C(144); // 连续空闲1天自动关机(这也是出厂默认值)
        break;
    case 'B': // B 读取空闲关机时长
        Cmd_2D();
        break;

    case 'C': // C 设置禁止蓝牙方式更改名称和充电参数标识
        Cmd_2E(0); // 设为允许(这也是出厂默认值)
        break;
    case 'D': // D 设置禁止蓝牙方式更改名称和充电参数标识
        Cmd_2E(1); // 设为禁止
        break;
    case 'E': // E 读取禁止蓝牙方式更改名称和充电参数标识
        Cmd_2F();
        break;

    case 'F': // F 设置串口通信地址 只能设置为0-254
        Cmd_30(0); // 设为0(这也是出厂默认值)
        break;
    case 'G': // G 设置串口通信地址
        Cmd_30(1); // 设为1
        break;
    case 'I': // I 读取串口通信地址
        Cmd_31();
        break;

    case 'J': // J 设置加速计和陀螺仪量程
       /**
        * 设置 加速计和陀螺仪量程
        * @param AccRange  目标加速度量程 0=2g 1=4g 2=8g 3=16g
        * @param GyroRange 目标陀螺仪量程 0=256 1=512 2=1024 3=2048
        */
        Cmd_33(3, 3); // 设为加速计16g, 陀螺仪2048dps
        break;
    case 'K': // K 设置加速计和陀螺仪量程
        Cmd_33(1, 2); // 设为加速计4g, 陀螺仪1024dps
        break;
    case 'L': // L 读取加速计和陀螺仪量程
        Cmd_34();
        break;

    case 'M': // M 设置陀螺仪自动校正标识 1=陀螺仪自动校正灵敏度开  0=关
        Cmd_35(1); // 设为1开启(这也是出厂默认值)
        break;
    case 'N': // N 设置陀螺仪自动校正标识
        Cmd_35(0); // 设为0关闭
        break;
    case 'O': // O 读取陀螺仪自动校正标识
        Cmd_36();
        break;

    case 'P': // P 设置静止节能模式的触发时长 EcoTime_10s大于0，则开启自动节能模式(即传感器睡眠后不主动上报，或静止EcoTime_10s个10秒自动进入运动监测模式且暂停主动上报)  0=不启用自动节能
        Cmd_37(0); // 设为0关闭(这也是出厂默认值)
        break;
    case 'Q': // Q 设置静止节能模式的触发时长
        Cmd_37(6*5); // 设为5分钟
        break;
    case 'R': // R 读取静止节能模式的触发时长
        Cmd_38();
        break;

    }
}

void IM948_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
	
	printf("--- IM948 test start FirmwareVer ---\r\n");
    int i = 40000000; while (i--);// 延时一下让传感器上电准备完毕，传感器上电后需要初始化完毕后才会接收指令的
    printf("shangdian_ok\r\n");
    // 唤醒传感器，并配置好传感器工作参数，然后开启主动上报---------------
    Cmd_03();// 1 唤醒传感器
    
    /**
       * 设置设备参数
     * @param accStill    惯导-静止状态加速度阀值 单位dm/s?
     * @param stillToZero 惯导-静止归零速度(单位cm/s) 0:不归零 255:立即归零
     * @param moveToZero  惯导-动态归零速度(单位cm/s) 0:不归零
     * @param isCompassOn 1=需开启磁场 0=需关闭磁场
     * @param barometerFilter 气压计的滤波等级[取值0-3],数值越大越平稳但实时性越差
     * @param reportHz 数据主动上报的传输帧率[取值0-250HZ], 0表示0.5HZ
     * @param gyroFilter    陀螺仪滤波系数[取值0-2],数值越大越平稳但实时性越差
     * @param accFilter     加速计滤波系数[取值0-4],数值越大越平稳但实时性越差
     * @param compassFilter 磁力计滤波系数[取值0-9],数值越大越平稳但实时性越差
     * @param Cmd_ReportTag 功能订阅标识
     */
	//Cmd_12(accStill, stillToZero, moveToZero,  isCompassOn, barometerFilter, reportHz, gyroFilter, accFilter, compassFilter, Cmd_ReportTag );
    Cmd_12(5, 255, 0,  0, 3, 5, 2, 4, 9, 0x7F);// 2 设置设备参数(内容1)
    Cmd_19();// 3 开启数据主动上报
    i = 1000000; while (i--);// 延时大概100ms，待前面的指令执行完毕
    Cmd_40(2);//设置开机后工作模式  参数2=开机时载入配置参数并开启串口主动上报，1=开机时载入配置参数但串口不主动上报, 0=不载入且串口不主动上报
	printf("Init_Finish\r\n");
}

void IM948_process(void)
{
    while (UartFifo.Cnt > 0)
        {// 从fifo获取串口发来的数据
			U8 rxByte;
            rxByte = UartFifo.RxBuf[UartFifo.Out];
            //printf("rxByte:%02X\r\n", rxByte);
            if (++UartFifo.Out >= FifoSize)
            {
                UartFifo.Out = 0;
            }
            __disable_irq();// 关中断
            --UartFifo.Cnt;
            __enable_irq();// 开中断

            // 移植 每收到1字节数据都填入该函数，当抓取到有效的数据包就会回调进入 Cmd_RxUnpack(U8 *buf, U8 DLen) 函数处理
            if (Cmd_GetPkt(rxByte)){break;}
        }
}
