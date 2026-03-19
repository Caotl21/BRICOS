/******************************************************************************
                        �豸��IM948ģ��֮��Ĵ���ͨ�ſ�
�汾: V1.05
��¼: 1������ ���ټƺ����������̿�����
      2������ �ų�У׼��ʼ����
      3������ �����������Զ�У����ʶ����
      4������ ���þ�ֹ����ģʽ�Ĵ���ʱ��
      5������ �����ϴ�Ȧ����֧������͸��
      6������ ����z�Ƕ�Ϊָ��ֵ
*******************************************************************************/
#ifndef _im948_CMD_h
#define _im948_CMD_h

#include <stdio.h>
#include <math.h>
#include "stm32f4xx.h" 

typedef signed char            S8;
typedef unsigned char          U8;
typedef signed short           S16;
typedef unsigned short         U16;
typedef signed long            S32;
typedef unsigned long          U32;
typedef float                  F32;


#define pow2(x) ((x)*(x)) // ��ƽ��

// ����ʱת������--------------
#define scaleAccel       0.00478515625f // ���ٶ� [-16g~+16g]    9.8*16/32768
#define scaleQuat        0.000030517578125f // ��Ԫ�� [-1~+1]         1/32768
#define scaleAngle       0.0054931640625f // �Ƕ�   [-180~+180]     180/32768
#define scaleAngleSpeed  0.06103515625f // ���ٶ� [-2000~+2000]    2000/32768
#define scaleMag         0.15106201171875f // �ų� [-4950~+4950]   4950/32768
#define scaleTemperature 0.01f // �¶�
#define scaleAirPressure 0.0002384185791f // ��ѹ [-2000~+2000]    2000/8388608
#define scaleHeight      0.0010728836f    // �߶� [-9000~+9000]    9000/8388608

#define CmdPacket_Begin  0x49   // ��ʼ��
#define CmdPacket_End    0x4D   // ������
#define CmdPacketMaxDatSizeRx 73  // ģ�鷢����   ���ݰ�����������󳤶�
#define CmdPacketMaxDatSizeTx 31  // ���͸�ģ��� ���ݰ�����������󳤶�


// �����ж����յ��������Ȼ��浽fifo��, Ȼ��������ѭ������д���



// ===============================������Ϣ����====================================
    //#define __Debug  // ʹ�õ��Կ����������Ϣ,��ʹ�õ�����Ϣ���α��伴��
    #ifdef __Debug
        #define Dbp(fmt, args...)  printf(fmt, ##args) // ����Ҫʹ�õ�����Ϣ, �û��Խ�Dbp����������
        extern void Dbp_U8_buf(char *sBeginInfo, char *sEndInfo, char *sFormat, const U8 *Buf, U32 Len);
    #else
        #define Dbp(fmt, args...)
        #define Dbp_U8_buf(sBeginInfo, sEndInfo, sFormat, Buf, Len)
    #endif


// =================================��ֲ�ӿ�======================================
    /**
     * ���ڲ������ݰ�, �û�ֻ��ѽ��յ���ÿ�ֽ����ݴ���ú�������
     * @param byte ������յ���ÿ�ֽ�����
     * @return U8 1=���յ��������ݰ�, 0δ��ȡ���������ݰ�
     */
    extern uint8_t Cmd_GetPkt(uint8_t byte, float *acc, float *gyro, float *quat);
    typedef void (*IM948_TxFunc_t)(uint8_t *pBuf, uint16_t Len);

    // 【新增】：对外暴露的注册接口
    void IM948_RegisterTxCallback(IM948_TxFunc_t tx_func);

    // �����յ���Ч���ݰ�����ص����뵽 Cmd_RxUnpack(U8 *buf, U8 DLen) ������û��ڸú����ﴦ�����ݼ��ɣ����ŷ���Ǹ�ֵ����һ�е�ȫ�ֱ���
    extern F32 AngleX,AngleY,AngleZ;// ��Cmd_RxUnpack�л�ȡ����ŷ�������ݸ��µ�ȫ�ֱ����Ա��û��Լ���ҵ���߼�ʹ��, ������Ҫ�������ݣ��ɲο��������Ӽ���
    extern F32 AccX_im948,AccY_im948,AccZ_im948, Accabs_im948; // ������
    extern F32 GyroX_im948, GyroY_im948, GyroZ_im948, Gyroabs_im948; // 陀螺仪 �
	extern F32 W_quat_im948, X_quat_im948, Y_quat_im948, Z_quat_im948;
    extern U8 IM948_GetNewData;// 1=�������µ����ݵ�ȫ�ֱ�������

    extern void im948_test(void); // ����ʾ�� �����Ǽ������Դ��ڷ����Ĳ���ָ�Ȼ���ģ����в���������Ҫ�������ѭ���Ｔ��

// ================================ģ��Ĳ���ָ��=================================
    extern U8 targetDeviceAddress; // ͨ�ŵ�ַ����Ϊ0-254ָ�����豸��ַ����Ϊ255��ָ���豸(���㲥), ����Ҫʹ��485������ʽͨ��ʱͨ���ò���ѡ��Ҫ�������豸���������Ǵ���1��1ͨ����Ϊ�㲥��ַ255����
    extern void Cmd_02(void);// ˯�ߴ�����
    extern void Cmd_03(void);// ���Ѵ�����
    extern void Cmd_18(void);// �ر����������ϱ�
    extern void Cmd_19(void);// �������������ϱ�
    extern void Cmd_11(void);// ��ȡ1�ζ��ĵĹ�������
    extern void Cmd_10(void);// ��ȡ�豸���Ժ�״̬
    /**
     * �����豸����
     * @param accStill    �ߵ�-��ֹ״̬���ٶȷ�ֵ ��λdm/s?
     * @param stillToZero �ߵ�-��ֹ�����ٶ�(��λcm/s) 0:������ 255:��������
     * @param moveToZero  �ߵ�-��̬�����ٶ�(��λcm/s) 0:������
     * @param isCompassOn 1=���ںϴų� 0=���ںϴų�
     * @param barometerFilter ��ѹ�Ƶ��˲��ȼ�[ȡֵ0-3],��ֵԽ��Խƽ�ȵ�ʵʱ��Խ��
     * @param reportHz ���������ϱ��Ĵ���֡��[ȡֵ0-250HZ], 0��ʾ0.5HZ
     * @param gyroFilter    �������˲�ϵ��[ȡֵ0-2],��ֵԽ��Խƽ�ȵ�ʵʱ��Խ��
     * @param accFilter     ���ټ��˲�ϵ��[ȡֵ0-4],��ֵԽ��Խƽ�ȵ�ʵʱ��Խ��
     * @param compassFilter �������˲�ϵ��[ȡֵ0-9],��ֵԽ��Խƽ�ȵ�ʵʱ��Խ��
     * @param Cmd_ReportTag ���ܶ��ı�ʶ
     */
    extern void Cmd_12(U8 accStill, U8 stillToZero, U8 moveToZero,  U8 isCompassOn, U8 barometerFilter, U8 reportHz, U8 gyroFilter, U8 accFilter, U8 compassFilter, U16 Cmd_ReportTag);
    extern void Cmd_13(void);// �ߵ���ά�ռ�λ������
    extern void Cmd_16(void);// �Ʋ�������
    extern void Cmd_14(void);// �ָ�����У׼����
    extern void Cmd_15(void);// ���浱ǰУ׼����Ϊ����У׼����
    extern void Cmd_07(void);// ���ټƼ���У׼ ģ�龲ֹ��ˮƽ��ʱ�����͸�ָ��յ��ظ���ȴ�5�뼴��
    /**
     * ���ټƸ߾���У׼
     * @param flag ��ģ��δ����У׼״̬ʱ��
     *                 ֵ0 ��ʾ����ʼһ��У׼���ɼ�1������
     *                 ֵ255 ��ʾѯ���豸�Ƿ�����У׼
     *             ��ģ������У׼��:
     *                 ֵ1 ��ʾҪ�ɼ���1������
     *                 ֵ255 ��ʾҪ�ɼ����1�����ݲ�����
     */
    extern void Cmd_17(U8 flag);

    extern void Cmd_32(void);// ��ʼ������У׼
    extern void Cmd_04(void);// ����������У׼
    extern void Cmd_05(void);// z��ǹ���
    /**
     * ���� Z��Ƕ�Ϊָ��ֵ
     * @param val Ҫ���õĽǶ�ֵ(��λ0.001��)�з���32λ��
     */
    extern void Cmd_28(S32 val);
    extern void Cmd_06(void);// xyz��������ϵ����
    extern void Cmd_08(void);// �ָ�Ĭ�ϵ���������ϵZ��ָ�򼰻ָ�Ĭ�ϵ���������ϵ
    /**
     * ����PCB��װ�������
     * @param accMatrix ���ټƷ������
     * @param comMatrix �����Ʒ������
     */
    extern void Cmd_20(S8 *accMatrix, S8 *comMatrix);
    extern void Cmd_21(void);// ��ȡPCB��װ�������
    /**
     * ���������㲥����
     *
     * @param bleName ��������(���֧��15���ַ�����,��֧������)
     */
    extern void Cmd_22(U8 *bleName);
    extern void Cmd_23(void);// ��ȡ�����㲥����
    /**
     * ���ùػ���ѹ�ͳ�����
     * @param PowerDownVoltageFlag �ػ���ѹѡ�� 0=3.4V(﮵����) 1=2.7V(�����ɵ����)
     * @param charge_full_mV  ����ֹ��ѹ 0:3962mv 1:4002mv 2:4044mv 3:4086mv 4:4130mv 5:4175mv 6:4222mv 7:4270mv 8:4308mv 9:4349mv 10:4391mv
     * @param charge_full_mA ����ֹ���� 0:2ma 1:5ma 2:7ma 3:10ma 4:15ma 5:20ma 6:25ma 7:30ma
     * @param charge_mA      ������ 0:20ma 1:30ma 2:40ma 3:50ma 4:60ma 5:70ma 6:80ma 7:90ma 8:100ma 9:110ma 10:120ma 11:140ma 12:160ma 13:180ma 14:200ma 15:220ma
     */
    extern void Cmd_24(U8 PowerDownVoltageFlag, U8 charge_full_mV, U8 charge_full_mA, U8 charge_mA);
    extern void Cmd_25(void);// ��ȡ �ػ���ѹ�ͳ�����
    extern void Cmd_26(void);// �Ͽ���������
    /**
     * �����û���GPIO����
     *
     * @param M 0=��������, 1=��������, 2=��������, 3=���0, 4=���1
     */
    extern void Cmd_27(U8 M);
    extern void Cmd_2A(void);// �豸����
    extern void Cmd_2B(void);// �豸�ػ�
    /**
     * ���� ���йػ�ʱ��
     *
     * @param idleToPowerOffTime ������û��ͨ���������ڹ㲥�У�������ʱ�ﵽ��ô���10������ػ�  0=���ػ�
     */
    extern void Cmd_2C(U8 idleToPowerOffTime);
    extern void Cmd_2D(void);// ��ȡ ���йػ�ʱ��
    /**
     * ���� ��ֹ������ʽ�������ƺͳ����� ��ʶ
     *
     * @param DisableBleSetNameAndCahrge 1=��ֹͨ�������������Ƽ������� 0=����(Ĭ��) ���ܿͻ��Ĳ�Ʒ�����ñ������������ģ���Ϊ1����
     */
    extern void Cmd_2E(U8 DisableBleSetNameAndCahrge);
    extern void Cmd_2F(void);// ��ȡ ��ֹ������ʽ�������ƺͳ����� ��ʶ
    /**
     * ���� ����ͨ�ŵ�ַ
     *
     * @param address �豸��ַֻ������Ϊ0-254
     */
    extern void Cmd_30(U8 address);
    extern void Cmd_31(void);// ��ȡ ����ͨ�ŵ�ַ
    /**
     * ���� ���ټƺ�����������
     *
     * @param AccRange  Ŀ����ٶ����� 0=2g 1=4g 2=8g 3=16g
     * @param GyroRange Ŀ������������ 0=256 1=512 2=1024 3=2048
     */
    extern void Cmd_33(U8 AccRange, U8 GyroRange);
    extern void Cmd_34(void);// ��ȡ ���ټƺ�����������
    /**
      * ���� �������Զ�У����ʶ
     *
     * @param GyroAutoFlag  1=�������Զ�У�������ȿ�  0=��
     */
    extern void Cmd_35(U8 GyroAutoFlag);
    extern void Cmd_36(void);// ��ȡ ���ټƺ�����������
    /**
      * ���� ��ֹ����ģʽ�Ĵ���ʱ��
     *
     * @param EcoTime_10s ��ֵ����0�������Զ�����ģʽ(��������˯�ߺ������ϱ�����ֹEcoTime_10s��10���Զ������˶����ģʽ����ͣ�����ϱ�)  0=�������Զ�����
     */
    extern void Cmd_37(U8 EcoTime_10s);
    extern void Cmd_38(void);// ��ȡ ��ֹ����ģʽ�Ĵ���ʱ��
    /**
     * ���� ��������ģʽ
     *
     * @param Flag ȡֵ2=����ʱ�������ò������������������ϱ���1=����ʱ�������ò��������ڲ������ϱ�, 0=�������Ҵ��ڲ������ϱ�
     */
    extern void Cmd_40(U8 Flag);
    // ��ȡ ��������ģʽ
    extern void Cmd_41(void);
    /**
       * ���� ��ǰ�߶�Ϊָ��ֵ
     *
     * @param val Ҫ���õĸ߶�ֵ ��λΪmm
     */
    extern void Cmd_42(S32 val);
    /**
       * ���� �Զ������߶ȱ�ʶ
     *
     * @param OnOff 0=�ر� 1=����
     */
    extern void Cmd_43(U8 OnOff);
    // ��ȡ �Զ������߶ȱ�ʶ
    extern void Cmd_44(void);
    /**
       * ���� ���ڲ�����
     *
     * @param BaudRate Ŀ�겨���� 0=9600 1=115200 2=230400 3=460800
     */
    extern void Cmd_47(U8 BaudRate);
    // ��ȡ ���ڲ�����
    extern void Cmd_48(void);
    // ��˸����ledָʾ��
    extern void Cmd_49(void);
    /**
       * ����͸��
     *
     * @param TxBuf Ҫ͸��������
     * @param TxLen �ֽ��� ����С��CmdPacketMaxDatSizeTx
     */
    extern void Cmd_50(U8 *TxBuf, U8 TxLen);
    /**
       * ���� �Ƿ��ϴ���Ȧ��������
     *
     * @param isReportCycle 0=����ŷ��������, 1=����Ȧ������ŷ���Ǵ��� ������Ȧ��
     */
    extern void Cmd_51(U8 isReportCycle);



#endif

