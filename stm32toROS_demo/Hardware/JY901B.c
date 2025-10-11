#include "JY901B_UART.h"
#include "wit_c_sdk.h"

void JY901B_Update(float *fAcc, float *fGyro, float *fAngle, char s_cDataUpdate){
    if(s_cDataUpdate)			//模块自动上传数据，进行更新，通过SensorDataUpdate进行
		{
			   for(i = 0; i < 3; i++)
			   {
			    	fAcc[i] = sReg[AX+i] / 32768.0f * 16.0f;
				    fGyro[i] = sReg[GX+i] / 32768.0f * 2000.0f;
					fAngle[i] = sReg[Roll+i] / 32768.0f * 180.0f;
			   }
			   if(s_cDataUpdate & ACC_UPDATE)
			   {
				    //printf("acc:%.3f %.3f %.3f\r\n", fAcc[0], fAcc[1], fAcc[2]);
				    s_cDataUpdate &= ~ACC_UPDATE;
			   }
			   if(s_cDataUpdate & GYRO_UPDATE)
			   {
					//printf("gyro:%.3f %.3f %.3f\r\n", fGyro[0], fGyro[1], fGyro[2]);
					s_cDataUpdate &= ~GYRO_UPDATE;
			   }
			   if(s_cDataUpdate & ANGLE_UPDATE)
			   {
					//printf("angle:%.3f %.3f %.3f\r\n", fAngle[0], fAngle[1], fAngle[2]);
					s_cDataUpdate &= ~ANGLE_UPDATE;
			   }
			   if(s_cDataUpdate & MAG_UPDATE)
			   {
					//printf("mag:%d %d %d\r\n", sReg[HX], sReg[HY], sReg[HZ]);
					s_cDataUpdate &= ~MAG_UPDATE;
			   }
		}
}