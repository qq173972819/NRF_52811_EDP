/****************************************************************************************************/
#ifndef __APP_MPU6500_H__
#define __APP_MPU6500_H__
/****************************************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>   
#include <zephyr/logging/log.h>

/* 定义使用的芯片型号 */
enum _MPU_CHIP_ID
{
    ID_MPU6000,  //ID = 0x68
    ID_MPU6050,  //ID = 0x68
    ID_MPU6500,  //ID = 0x70
    ID_MPU6555,  //ID = 0x7C
    ID_MPU9150,  //ID = 0x68
    ID_MPU9250,  //ID = 0x71
    ID_MPU9255,  //ID = 0x73
};
#define MPU_CHIPID   ID_MPU6500

/****************************************************************************************************/
/* 引脚定义 */
#define MPU_PIN_CS   23  /* P0.23 - CS/AD0引脚，用于选择I2C地址 */
#define MPU_PIN_SCL  22  /* P0.22 - I2C时钟线 */
#define MPU_PIN_SDA  5   /* P0.05 - I2C数据线 */
#define MPU_PIN_INT  20  /* P0.20 - 中断引脚 */

/* I2C地址：通过CS/AD0引脚选择 */
#define MPU_I2C_ADDR_LOW    0x68  /* AD0=LOW: 0x68 */
#define MPU_I2C_ADDR_HIGH   0x69  /* AD0=HIGH: 0x69 */

/****************************************************************************************************/
// 所有MPU系列共有的寄存器 
#define MPU_XG_OFFSET_H   (0x13)
#define MPU_XG_OFFSET_L   (0x14)
#define MPU_YG_OFFSET_H   (0x15)
#define MPU_YG_OFFSET_L   (0x16)
#define MPU_ZG_OFFSET_H   (0x17)
#define MPU_ZG_OFFSET_L   (0x18)
#define MPU_SMPLRT_DIV    (0x19)  // [7:0] ,采样频率分频器
//------------------------------------------------------------------------------
#define MPU_CONFIG        (0x1A)  //配置寄存器
    #define MPU_CONFIG_FIFO_MODE_BIT        (6)
    #define MPU_CONFIG_EXT_SYNC_SET_BIT     (3)  // [5:3]
    #define MPU_CONFIG_EXT_SYNC_SET_LENGTH  (3)
    #define MPU_CONFIG_DLPF_CFG_BIT         (0)  // [2:0]
    #define MPU_CONFIG_DLPF_CFG_LENGTH      (3)
//------------------------------------------------------------------------------
#define MPU_GYRO_CONFIG    (0x1B)   //陀螺仪配置寄存器
    #define MPU_GCONFIG_XG_ST_BIT         (7)  //X轴陀螺仪执行自检
    #define MPU_GCONFIG_YG_ST_BIT         (6)  //y轴陀螺仪执行自检
    #define MPU_GCONFIG_ZG_ST_BIT         (5)  //z轴陀螺仪执行自检
    #define MPU_GCONFIG_FS_SEL_BIT        (3)  //[4:3],设置陀螺仪输出的满量程范围
    #define MPU_GCONFIG_FS_SEL_BIT_MASK   (0x03)
    #define MPU_GCONFIG_FCHOICE_B         (0)  //[2:0]，保留
    #define MPU_GCONFIG_FCHOICE_B_MASK    (0x07)
//------------------------------------------------------------------------------
#define MPU_ACCEL_CONFIG    (0x1C) //加速度计配置寄存器
    #define MPU_ACONFIG_XA_ST_BIT      (7)   //X轴ACCEL执行自检
    #define MPU_ACONFIG_YA_ST_BIT      (6)   //y轴ACCEL执行自检
    #define MPU_ACONFIG_ZA_ST_BIT      (5)   //z轴ACCEL执行自检
    #define MPU_ACONFIG_FS_SEL_BIT     (3)   //[4:3],设置ACCEL输出的满量程范围
    #define MPU_ACONFIG_FS_SEL_MASK    (0x03)
    #define MPU_ACONFIG_HPF_BIT        (0)   //[2:0]保留
    #define MPU_ACONFIG_HPF_BIT_MASK   (0x07)
//------------------------------------------------------------------------------
#define MPU_FF_THR         (0x1D)
#define MPU_FF_DUR         (0x1E)
#define MPU_MOTION_THR     (0x1F)  // [7:0] 运动检测阀值设置寄存器
#define MPU_MOTION_DUR     (0x20)
#define MPU_ZRMOTION_THR   (0x21)
#define MPU_ZRMOTION_DUR   (0x22)
//------------------------------------------------------------------------------
#define MPU_FIFO_EN        (0x23) //FIFO使能寄存器
    #define MPU_FIFO_TEMP_EN_BIT   (7)
    #define MPU_FIFO_XGYRO_EN_BIT  (6)
    #define MPU_FIFO_YGYRO_EN_BIT  (5)
    #define MPU_FIFO_ZGYRO_EN_BIT  (4)
    #define MPU_FIFO_ACCEL_EN_BIT  (3)
    #define MPU_FIFO_SLV_2_EN_BIT  (2)
    #define MPU_FIFO_SLV_1_EN_BIT  (1)
    #define MPU_FIFO_SLV_0_EN_BIT  (0)
//------------------------------------------------------------------------------
#define MPU_I2C_MST_CTRL    (0x24)  //IIC主机控制寄存器
    #define MPU_I2CMST_CTRL_MULT_EN_BIT        (7)
    #define MPU_I2CMST_CTRL_WAIT_FOR_ES_BIT    (6)
    #define MPU_I2CMST_CTRL_SLV_3_FIFO_EN_BIT  (5)
    #define MPU_I2CMST_CTRL_P_NSR_BIT          (4)
    #define MPU_I2CMST_CTRL_CLOCK_BIT          (3)  // [3:0]
    #define MPU_I2CMST_CTRL_CLOCK_LENGTH       (4)
//------------------------------------------------------------------------------
#define MPU_I2C_SLV0_ADDR    (0x25) //IIC从机0器件地址寄存器
    #define MPU_I2C_SLV_RNW_BIT    (7)  // same for all I2C_SLV registers
    #define MPU_I2C_SLV_ID_BIT     (6)  // [6:0]
    #define MPU_I2C_SLV_ID_LENGTH  (7)
//------------------------------------------------------------------------------
#define MPU_I2C_SLV0_REG     (0x26)  // [7:0],IIC从机0数据地址寄存器
//------------------------------------------------------------------------------
#define MPU_I2C_SLV0_CTRL    (0x27)  //IIC从机0控制寄存器
    #define MPU_I2C_SLV_EN_BIT       (7)  // same for all I2C_SLV registers
    #define MPU_I2C_SLV_BYTE_SW_BIT  (6)
    #define MPU_I2C_SLV_REG_DIS_BIT  (5)
    #define MPU_I2C_SLV_GRP_BIT      (4)
    #define MPU_I2C_SLV_LEN_BIT      (3)  // [3:0]
    #define MPU_I2C_SLV_LEN_LENGTH   (4)
//------------------------------------------------------------------------------
#define MPU_I2C_SLV1_ADDR     (0x28)  //IIC从机1器件地址寄存器
#define MPU_I2C_SLV1_REG      (0x29)  //IIC从机1数据地址寄存器
#define MPU_I2C_SLV1_CTRL     (0x2A)  //IIC从机1控制寄存器
#define MPU_I2C_SLV2_ADDR     (0x2B)  //IIC从机2器件地址寄存器
#define MPU_I2C_SLV2_REG      (0x2C)  //IIC从机2数据地址寄存器
#define MPU_I2C_SLV2_CTRL     (0x2D)  //IIC从机2控制寄存器
#define MPU_I2C_SLV3_ADDR     (0x2E)  //IIC从机3器件地址寄存器
#define MPU_I2C_SLV3_REG      (0x2F)  //IIC从机3数据地址寄存器
#define MPU_I2C_SLV3_CTRL     (0x30)  //IIC从机3控制寄存器
#define MPU_I2C_SLV4_ADDR     (0x31)  //IIC从机4器件地址寄存器
#define MPU_I2C_SLV4_REG      (0x32)  //IIC从机4数据地址寄存器
#define MPU_I2C_SLV4_DO       (0x33)  //IIC从机4写数据寄存器
//------------------------------------------------------------------------------
#define MPU_I2C_SLV4_CTRL     (0x34)  //IIC从机4控制寄存器
    #define MPU_I2C_SLV4_EN_BIT            (7)
    #define MPU_I2C_SLV4_DONE_INT_BIT      (6)
    #define MPU_I2C_SLV4_REG_DIS_BIT       (5)
    #define MPU_I2C_SLV4_MST_DELAY_BIT     (4)  // [4:0]
    #define MPU_I2C_SLV4_MST_DELAY_LENGTH  (5)
//------------------------------------------------------------------------------
#define MPU_I2C_SLV4_DI        (0x35)   // [7:0], IIC从机4读数据寄存器
//------------------------------------------------------------------------------
#define MPU_I2C_MST_STATUS     (0x36)  //IIC主机状态寄存器
    #define MPU_I2CMST_STAT_PASS_THROUGH_BIT  (7)
    #define MPU_I2CMST_STAT_SLV4_DONE_BIT     (6)
    #define MPU_I2CMST_STAT_LOST_ARB_BIT      (5)
    #define MPU_I2CMST_STAT_SLV4_NACK_BIT     (4)
    #define MPU_I2CMST_STAT_SLV3_NACK_BIT     (3)
    #define MPU_I2CMST_STAT_SLV2_NACK_BIT     (2)
    #define MPU_I2CMST_STAT_SLV1_NACK_BIT     (1)
    #define MPU_I2CMST_STAT_SLV0_NACK_BIT     (0)
//------------------------------------------------------------------------------
#define MPU_INT_PIN_CONFIG       (0x37)   //中断引脚设置寄存器
    #define MPU_INT_CFG_LEVEL_BIT              (7) //中断引脚触发输出电平设置，0输出高电平/1输出低电平
    #define MPU_INT_CFG_OPEN_BIT               (6) //中断引脚输出推挽设置，0推挽输出/1开漏输出
    #define MPU_INT_CFG_LATCH_EN_BIT           (5) //输出类型设置，0输出50us脉冲/1保持高电平，直到中断被清除
    #define MPU_INT_CFG_ANYRD_2CLEAR_BIT       (4) //中断状态位清除设置，0只能通过读取INT_STATUS来清除/1任何读取作中都会清除
    #define MPU_INT_CFG_FSYNC_LEVEL_BIT        (3) //FSYNC引脚用作host的中断时设置，0 逻辑电平为高电平有效/ 1逻辑电平为低电平有效
    #define MPU_INT_CFG_FSYNC_INT_MODE_EN_BIT  (2) //FSYNC引脚使能设置，0禁用/1使能FSYNC引脚用作host的中断
    #define MPU_INT_CFG_I2C_BYPASS_EN_BIT      (1) //host直接访问MPU辅助I2C接口设置，0禁止直接访问/ 1且寄存器0x6A的I2C_MST_EN位=0时，host可直接访问MPU辅助I2C接口
    #define MPU_INT_CFG_CLOCKOUT_EN_BIT        (0)
//------------------------------------------------------------------------------
#define MPU_INT_ENABLE            (0x38) //中断源配置寄存器
    #define MPU_INT_ENABLE_FREEFALL_BIT       (7) 
    #define MPU_INT_ENABLE_MOTION_BIT         (6) //Motion detection是否生成中断，0禁止运动检测中断/ 1使能运动检测中断
    #define MPU_INT_ENABLE_ZEROMOT_BIT        (5) 
    #define MPU_INT_ENABLE_FIFO_OFLOW_BIT     (4) //FIFO BUFF溢出中断，0禁止溢出中断/1使能溢出中断
    #define MPU_INT_ENABLE_I2C_MST_FSYNC_BIT  (3) //I2C接口是否产生中断，0 禁止/ 1使能
    #define MPU_INT_ENABLE_PLL_RDY_BIT        (2)
    #define MPU_INT_ENABLE_DMP_RDY_BIT        (1)
    #define MPU_INT_ENABLE_RAW_DATA_RDY_BIT   (0) //Data Ready中断，0禁用/1使能数据准备好中断 
//------------------------------------------------------------------------------
#define MPU_DMP_INT_STATUS    (0x39)
    #define MPU_DMP_INT_STATUS_0  (0)
    #define MPU_DMP_INT_STATUS_1  (1)
    #define MPU_DMP_INT_STATUS_2  (2)
    #define MPU_DMP_INT_STATUS_3  (3)
    #define MPU_DMP_INT_STATUS_4  (4)
    #define MPU_DMP_INT_STATUS_5  (5)
//------------------------------------------------------------------------------
#define MPU_INT_STATUS                   (0x3A) //中断状态寄存器，自读，读取后自动清零
    #define MPU_INT_STATUS_FREEFALL_BIT      (7)
    #define MPU_INT_STATUS_MOTION_BIT        (6) //移到侦测中断
    #define MPU_INT_STATUS_ZEROMOT_BIT       (5)
    #define MPU_INT_STATUS_FIFO_OFLOW_BIT    (4) //FIFO缓冲溢出中断
    #define MPU_INT_STATUS_I2C_MST_BIT       (3)
    #define MPU_INT_STATUS_PLL_RDY_BIT       (2)
    #define MPU_INT_STATUS_DMP_RDY_BIT       (1)
    #define MPU_INT_STATUS_RAW_DATA_RDY_BIT  (0) //Data Ready 中断
//------------------------------------------------------------------------------
#define MPU_ACCEL_XOUT_H      (0x3B)  // [15:0], 加速度值,X轴高8位寄存器
#define MPU_ACCEL_XOUT_L      (0x3C)  //         加速度值,X轴低8位寄存器
#define MPU_ACCEL_YOUT_H      (0x3D)  // [15:0]
#define MPU_ACCEL_YOUT_L      (0x3E)
#define MPU_ACCEL_ZOUT_H      (0x3F)  // [15:0]
#define MPU_ACCEL_ZOUT_L      (0x40)
#define MPU_TEMP_OUT_H        (0x41)  // [15:0]
#define MPU_TEMP_OUT_L        (0x42)
#define MPU_GYRO_XOUT_H       (0x43)  // [15:0]
#define MPU_GYRO_XOUT_L       (0x44)
#define MPU_GYRO_YOUT_H       (0x45)  // [15:0]
#define MPU_GYRO_YOUT_L       (0x46)
#define MPU_GYRO_ZOUT_H       (0x47)  // [15:0]
#define MPU_GYRO_ZOUT_L       (0x48)
#define MPU_EXT_SENS_DATA_00  (0x49)  // Stores data read from Slave 0, 1, 2, and 3
#define MPU_EXT_SENS_DATA_01  (0x4A)
#define MPU_EXT_SENS_DATA_02  (0x4B)
#define MPU_EXT_SENS_DATA_03  (0x4C)
#define MPU_EXT_SENS_DATA_04  (0x4D)
#define MPU_EXT_SENS_DATA_05  (0x4E)
#define MPU_EXT_SENS_DATA_06  (0x4F)
#define MPU_EXT_SENS_DATA_07  (0x50)
#define MPU_EXT_SENS_DATA_08  (0x51)
#define MPU_EXT_SENS_DATA_09  (0x52)
#define MPU_EXT_SENS_DATA_10  (0x53)
#define MPU_EXT_SENS_DATA_11  (0x54)
#define MPU_EXT_SENS_DATA_12  (0x55)
#define MPU_EXT_SENS_DATA_13  (0x56)
#define MPU_EXT_SENS_DATA_14  (0x57)
#define MPU_EXT_SENS_DATA_15  (0x58)
#define MPU_EXT_SENS_DATA_16  (0x59)
#define MPU_EXT_SENS_DATA_17  (0x5A)
#define MPU_EXT_SENS_DATA_18  (0x5B)
#define MPU_EXT_SENS_DATA_19  (0x5C)
#define MPU_EXT_SENS_DATA_20  (0x5D)
#define MPU_EXT_SENS_DATA_21  (0x5E)
#define MPU_EXT_SENS_DATA_22  (0x5F)
#define MPU_EXT_SENS_DATA_23  (0x60)
#define MPU_I2C_SLV0_DO       (0x63)  //IIC从机0数据寄存器
#define MPU_I2C_SLV1_DO       (0x64)  //IIC从机1数据寄存器
#define MPU_I2C_SLV2_DO       (0x65)  //IIC从机2数据寄存器
#define MPU_I2C_SLV3_DO       (0x66)  //IIC从机3数据寄存器
//------------------------------------------------------------------------------
#define MPU_I2C_MST_DELAY_CRTL        (0x67) //IIC主机延时管理寄存器
    #define MPU_I2CMST_DLY_ES_SHADOW_BIT  (7)
    #define MPU_I2CMST_DLY_SLV4_EN_BIT    (4)
    #define MPU_I2CMST_DLY_SLV3_EN_BIT    (3)
    #define MPU_I2CMST_DLY_SLV2_EN_BIT    (2)
    #define MPU_I2CMST_DLY_SLV1_EN_BIT    (1)
    #define MPU_I2CMST_DLY_SLV0_EN_BIT    (0)
//------------------------------------------------------------------------------
#define MPU_SIGNAL_PATH_RESET    (0x68)   //信号通道复位寄存器
    #define MPU_SPATH_GYRO_RST_BIT   (2)
    #define MPU_SPATH_ACCEL_RST_BIT  (1)
    #define MPU_SPATH_TEMP_RST_BIT   (0)
//------------------------------------------------------------------------------
#define MPU_USER_CTRL            (0x6A) //用户控制寄存器
    #define MPU_USERCTRL_DMP_EN_BIT          (7)
    #define MPU_USERCTRL_FIFO_EN_BIT         (6)
    #define MPU_USERCTRL_I2C_MST_EN_BIT      (5)
    #define MPU_USERCTRL_I2C_IF_DIS_BIT      (4)
    #define MPU_USERCTRL_DMP_RESET_BIT       (3)
    #define MPU_USERCTRL_FIFO_RESET_BIT      (2)
    #define MPU_USERCTRL_I2C_MST_RESET_BIT   (1)
    #define MPU_USERCTRL_SIG_COND_RESET_BIT  (0)
//------------------------------------------------------------------------------
#define MPU_PWR_MGMT1              (0x6B)  //电源管理寄存器1
    #define MPU_PWR1_DEVICE_RESET_BIT  (7)
    #define MPU_PWR1_SLEEP_BIT         (6)
    #define MPU_PWR1_CYCLE_BIT         (5)
    #define MPU_PWR1_GYRO_STANDBY_BIT  (4)
    #define MPU_PWR1_TEMP_DIS_BIT      (3)
    #define MPU_PWR1_CLKSEL_BIT        (0) //[2:0]
    #define MPU_PWR1_CLKSEL_MASK       (0x07)
//------------------------------------------------------------------------------
#define MPU_PWR_MGMT2                 (0x6C) //电源管理寄存器2 
    #define MPU_PWR2_LP_WAKE_CTRL_BIT     (6) //[7:6]
    #define MPU_PWR2_LP_WAKE_CTRL_LENGTH  (2)
    #define MPU_PWR2_STBY_XA_BIT          (5) //置1则进入standby状态，置0活动状态
    #define MPU_PWR2_STBY_YA_BIT          (4)
    #define MPU_PWR2_STBY_ZA_BIT          (3)
    #define MPU_PWR2_STBY_XG_BIT          (2)
    #define MPU_PWR2_STBY_YG_BIT          (1)
    #define MPU_PWR2_STBY_ZG_BIT          (0)
    #define MPU_PWR2_STBY_XYZA_BITS       (1 << MPU_PWR2_STBY_XA_BIT | 1 << MPU_PWR2_STBY_YA_BIT | 1 << MPU_PWR2_STBY_ZA_BIT)
    #define MPU_PWR2_STBY_XYZG_BITS       (1 << MPU_PWR2_STBY_XG_BIT | 1 << MPU_PWR2_STBY_YG_BIT | 1 << MPU_PWR2_STBY_ZG_BIT)
//------------------------------------------------------------------------------
#define MPU_BANK_SEL                   (0x6D)
    #define MPU_BANKSEL_PRFTCH_EN_BIT      (6)
    #define MPU_BANKSEL_CFG_USER_BANK_BIT  (5)
    #define MPU_BANKSEL_MEM_SEL_BIT        (4)
    #define MPU_BANKSEL_MEM_SEL_LENGTH     (5)
//------------------------------------------------------------------------------
#define MPU_MEM_START_ADDR  (0x6E)
#define MPU_MEM_R_W         (0x6F)
#define MPU_PRGM_START_H    (0x70)
#define MPU_PRGM_START_L    (0x71)
#define MPU_FIFO_COUNT_H    (0x72)  // [15:0],FIFO计数寄存器高八位
#define MPU_FIFO_COUNT_L    (0x73)
#define MPU_FIFO_R_W        (0x74)  //FIFO读写寄存器
#define MPU_WHO_AM_I        (0x75)  //器件ID寄存器

/****************************************************************************************************/
//以下为  MPU6500 特有的寄存器
#if MPU_CHIPID == ID_MPU6500
#define MPU_SELF_TEST_X_GYRO   (0x00)  // XG_ST_DATA[7:0]
#define MPU_SELF_TEST_Y_GYRO   (0x01)  // YG_ST_DATA[7:0]
#define MPU_SELF_TEST_Z_GYRO   (0x02)  // ZG_ST_DATA[7:0]
#define MPU_SELF_TEST_X_ACCEL  (0x0D)
#define MPU_SELF_TEST_Y_ACCEL  (0x0E)
#define MPU_SELF_TEST_Z_ACCEL  (0x0F)
//------------------------------------------------------------------------------
#define MPU_ACCEL_CONFIG2                 (0x1D)
    #define MPU_ACONFIG2_FIFO_SIZE_BIT        (7)  // [7:6]
    #define MPU_ACONFIG2_FIFO_SIZE_LENGTH     (2)
    #define MPU_ACONFIG2_ACCEL_FCHOICE_B_BIT  (3)
    #define MPU_ACONFIG2_A_DLPF_CFG_BIT       (2)  // [2:0]
    #define MPU_ACONFIG2_A_DLPF_CFG_LENGTH    (3)
//------------------------------------------------------------------------------
#define MPU_LP_ACCEL_ODR           (0x1E)
    #define MPU_LPA_ODR_CLKSEL_BIT     (3)  // [3:0]
    #define MPU_LPA_ODR_CLKSEL_LENGTH  (4)
//------------------------------------------------------------------------------
#define MPU_ACCEL_INTEL_CTRL      (0x69)  //运动检测控制寄存器
    #define MPU_ACCEL_INTEL_EN_BIT    (7)
    #define MPU_ACCEL_INTEL_MODE_BIT  (6)
//------------------------------------------------------------------------------
#define MPU_XA_OFFSET_H  (0x77)
#define MPU_XA_OFFSET_L  (0x78)
#define MPU_YA_OFFSET_H  (0x7A)
#define MPU_YA_OFFSET_L  (0x7B)
#define MPU_ZA_OFFSET_H  (0x7D)
#define MPU_ZA_OFFSET_L  (0x7E)
#endif

/****************************************************************************************************/
/*! Gyroscope full-scale range */
typedef enum {
    GYRO_FS_250DPS  = 0,  //!< +/- 250 º/s  -> 131 LSB/(º/s)
    GYRO_FS_500DPS  = 1,  //!< +/- 500 º/s  -> 65.5 LSB/(º/s)
    GYRO_FS_1000DPS = 2,  //!< +/- 1000 º/s -> 32.8 LSB/(º/s)
    GYRO_FS_2000DPS = 3   //!< +/- 2000 º/s -> 16.4 LSB/(º/s)
} mpu_gyro_fs_t;

/* Accel full-scale range */
typedef enum {
    ACCEL_FS_2G  = 0,  //!< +/- 2 g  -> 16.384 LSB/g
    ACCEL_FS_4G  = 1,  //!< +/- 4 g  -> 8.192 LSB/g
    ACCEL_FS_8G  = 2,  //!< +/- 8 g  -> 4.096 LSB/g
    ACCEL_FS_16G = 3   //!< +/- 16 g -> 2.048 LSB/g
} mpu_accel_fs_t;

//-------------------------------------------------------------------------------------------------//
/*  Digital low-pass filter (based on gyro bandwidth) */
typedef enum {
    DLPF_256HZ_NOLPF = 0,
    DLPF_188HZ       = 1,
    DLPF_98HZ        = 2,
    DLPF_42HZ        = 3,
    DLPF_20HZ        = 4,
    DLPF_10HZ        = 5,
    DLPF_5HZ         = 6,
#if MPU_CHIPID == ID_MPU6050
    DLPF_2100HZ_NOLPF = 7,
#endif
#if MPU_CHIPID == ID_MPU6500
    DLPF_3600HZ_NOLPF   = 7,
#endif
} mpu_dlpf_t;

//-------------------------------------------------------------------------------------------------//
/* Cycle模式下加速计唤醒率 */
typedef enum {
#if MPU_CHIPID == ID_MPU6000 || MPU_CHIPID == ID_MPU6050 || MPU_CHIPID == ID_MPU6500 || MPU_CHIPID == ID_MPU9150
    LP_ACCEL_RATE_1_25HZ = 0, //1.25Hz
    LP_ACCEL_RATE_5HZ    = 1, //5Hz
    LP_ACCEL_RATE_20HZ   = 2, //20Hz
    LP_ACCEL_RATE_40HZ   = 3, //40Hz
#endif
} mpu_lp_accel_rate_t;

/****************************************************************************************************/
/* MPU传感器结构体 */
typedef struct _mpu_sensor_t mpu_sensor_t;
typedef struct _mpu_sensor_t {
    struct i2c_dt_spec i2c;        /* I2C设备规格（含设备指针和地址） */

    int16_t val_ACC_x, val_ACC_y, val_ACC_z;  /* 三轴加速度 */
    int16_t val_GYR_x, val_GYR_y, val_GYR_z;  /* 三轴角速度 */
    int16_t val_temp;                          /* 温度 */
} mpu_sensor_t;

/****************************************************************************************************/
/**
 * @brief MPU传感器初始化
 * @param i2c I2C设备规格指针（含设备和地址）
 * @return 0表示成功，负数表示失败
 */
extern int mpuSensor_init(const struct i2c_dt_spec *i2c);

/**
 * @brief MPU传感器复位
 * @param pChip 传感器实例指针
 * @return 0表示成功，负数表示失败
 */
extern int mpuSensor_reset(mpu_sensor_t *pChip);

/**
 * @brief 获取MPU芯片ID
 * @param pChip 传感器实例指针
 * @return 芯片ID
 */
extern uint8_t mpuSensor_get_mpuID(mpu_sensor_t *pChip);

/**
 * @brief 设置采样率
 * @param pChip 传感器实例指针
 * @param rate 采样率(Hz)，范围4~1000
 * @return 0表示成功，负数表示失败
 */
extern int mpuSensor_set_rate(mpu_sensor_t *pChip, uint16_t rate);

/**
 * @brief 获取温度值
 * @param pChip 传感器实例指针
 * @param pTempVal 温度值指针
 * @return 0表示成功，负数表示失败
 */
extern int mpuSensor_get_temp(mpu_sensor_t *pChip, int16_t *pTempVal);

/**
 * @brief 获取陀螺仪数据
 * @param pChip 传感器实例指针
 * @param gyroVal 陀螺仪数据数组[3]
 * @return 0表示成功，负数表示失败
 */
extern int mpuSensor_get_gyro(mpu_sensor_t *pChip, int16_t gyroVal[3]);

/**
 * @brief 获取加速度数据
 * @param pChip 传感器实例指针
 * @param accelVal 加速度数据数组[3]
 * @return 0表示成功，负数表示失败
 */
extern int mpuSensor_get_accel(mpu_sensor_t *pChip, int16_t accelVal[3]);

/**
 * @brief 设置数字低通滤波器
 * @param pChip 传感器实例指针
 * @param dlpf_cfg 滤波器配置
 * @return 0表示成功，负数表示失败
 */
extern int mpuSensor_set_lpf(mpu_sensor_t *pChip, uint8_t dlpf_cfg);

/**
 * @brief 设置唤醒频率
 * @param pChip 传感器实例指针
 * @param freq 唤醒频率
 * @return 0表示成功，负数表示失败
 */
extern int mpuSensor_set_wakeUpFreq(mpu_sensor_t *pChip, uint8_t freq);

/**
 * @brief 复位FIFO
 * @param pChip 传感器实例指针
 * @return 0表示成功，负数表示失败
 */
extern int mpuSensor_resetfifo(mpu_sensor_t *pChip);

/**
 * @brief MPU数据处理回调（供中断调用）
 * @param 无
 * @return 无
 */
extern void OnQM_mpu_Proce(void);

/**
 * @brief 读取MPU传感器数据并打印
 * @param 无
 * @return 无
 */
extern void mpuSensor_read_and_print(void);

/****************************************************************************************************/
#ifdef __cplusplus
}
#endif
/****************************************************************************************************/
#endif /* __APP_MPU6500_H__ */
