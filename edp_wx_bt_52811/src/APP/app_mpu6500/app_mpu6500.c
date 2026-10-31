/****************************************************************************************************/
/**
 * @file app_mpu6500.c
 * @brief MPU6500传感器驱动
 *
 */
/****************************************************************************************************/

#include "app_mpu6500.h"

LOG_MODULE_REGISTER(mpu6500, LOG_LEVEL_INF);

/****************************************************************************************************/
/* 全局变量 */
static mpu_sensor_t s_mpuSensor;

/****************************************************************************************************/
/* 内部函数声明 */
static int mpuSensor_reset_signalPath(mpu_sensor_t *pChip);
static int mpuSensor_setSleep(mpu_sensor_t *pChip, bool isSleep);
static bool mpuSensor_getSleep(mpu_sensor_t *pChip);
static int mpuSensor_set_gyro_fssel(mpu_sensor_t *pChip, uint8_t fs_sel);
static int mpuSensor_set_accel_fssel(mpu_sensor_t *pChip, uint8_t fs_sel);
static int mpuSensor_set_accel_MotThr(mpu_sensor_t *pChip, uint8_t mot_thr);
static int mpuSensor_set_fifoEN(mpu_sensor_t *pChip, bool cfg);
static int mpuSensor_set_int_Pincfg(mpu_sensor_t *pChip, uint8_t cfg);
static int mpuSensor_Set_int_EN(mpu_sensor_t *pChip, uint8_t cfg);
static uint8_t mpuSensor_read_int_status(mpu_sensor_t *pChip);
static uint16_t mpuSensor_get_fifoCount(mpu_sensor_t *pChip);
static int mpuSensor_set_enMastI2C(mpu_sensor_t *pChip, bool cfg);
static int mpuSensor_set_clkSel(mpu_sensor_t *pChip, uint8_t cfg);
static int mpuSensor_set_accel_isON(mpu_sensor_t *pChip, bool isON);
static int mpuSensor_set_gyro_isON(mpu_sensor_t *pChip, bool isON);
static int mpuSensor_set_Temp_isON(mpu_sensor_t *pChip, bool isON);

static void _mpuSensor_init(mpu_sensor_t *pChip);

/****************************************************************************************************/
/**
 * @brief MPU传感器初始化
 *
 * @param i2c I2C设备规格指针（含设备和地址）
 * @return 0表示成功，负数表示失败
 */
int mpuSensor_init(const struct i2c_dt_spec *i2c)
{
    if (i2c == NULL)
    {
        LOG_ERR("I2C spec is NULL");
        return -EINVAL;
    }

    if (!device_is_ready(i2c->bus))
    {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    s_mpuSensor.i2c = *i2c;

    _mpuSensor_init(&s_mpuSensor);

    return 0;
}

/****************************************************************************************************/
/**
 * @brief MPU传感器内部初始化
 *
 * @param pChip 传感器实例指针
 */
static void _mpuSensor_init(mpu_sensor_t *pChip)
{
    uint8_t chip_id = 0;

    /* 复位传感器 */
    mpuSensor_reset(pChip);

    /* 唤醒传感器 */
    mpuSensor_setSleep(pChip, false);

    /* 设置陀螺仪满量程范围：±250dps */
    mpuSensor_set_gyro_fssel(pChip, GYRO_FS_250DPS);

    /* 设置加速度满量程范围：±4g */
    mpuSensor_set_accel_fssel(pChip, ACCEL_FS_4G);

    /* 设置采样率：20Hz */
    mpuSensor_set_rate(pChip, 20);

    /* 读取并打印芯片ID */
    chip_id = mpuSensor_get_mpuID(pChip);
    LOG_INF("MPU6500 ID=0x%02X", chip_id);

    if (chip_id != 0x70)
    {
        LOG_WRN("Unexpected MPU chip ID: 0x%02X (expected 0x70)", chip_id);
    }
    else
    {
        LOG_INF("MPU6500 initialized successfully");
    }
}

/****************************************************************************************************/
/**
 * @brief MPU传感器复位
 *
 * @param pChip 传感器实例指针
 * @return 0表示成功，负数表示失败
 */
int mpuSensor_reset(mpu_sensor_t *pChip)
{
    uint8_t data = (1 << MPU_PWR1_DEVICE_RESET_BIT);
    int ret = i2c_reg_write_byte_dt(&pChip->i2c, MPU_PWR_MGMT1, data);

    if (ret < 0)
    {
        LOG_ERR("MPU reset failed: %d", ret);
        return ret;
    }

    /* 等待复位完成 */
    k_msleep(100);

    return 0;
}

/****************************************************************************************************/
/**
 * @brief 重置陀螺仪、加速度计和温度传感器的信号路径
 *
 * @param pChip 传感器实例指针
 * @return 0表示成功，负数表示失败
 */
static int mpuSensor_reset_signalPath(mpu_sensor_t *pChip)
{
    uint8_t data = (1 << MPU_SPATH_GYRO_RST_BIT) |
                   (1 << MPU_SPATH_ACCEL_RST_BIT) |
                   (1 << MPU_SPATH_TEMP_RST_BIT);
    int ret = i2c_reg_write_byte_dt(&pChip->i2c, MPU_SIGNAL_PATH_RESET, data);

    if (ret < 0)
    {
        LOG_ERR("MPU signal path reset failed: %d", ret);
        return ret;
    }

    k_msleep(100);

    return 0;
}

/****************************************************************************************************/
/**
 * @brief 设置传感器休眠状态
 *
 * @param pChip 传感器实例指针
 * @param isSleep true进入休眠，false退出休眠
 * @return 0表示成功，负数表示失败
 */
static int mpuSensor_setSleep(mpu_sensor_t *pChip, bool isSleep)
{
    uint8_t val = isSleep ? BIT(MPU_PWR1_SLEEP_BIT) : 0;

    return i2c_reg_update_byte_dt(&pChip->i2c, MPU_PWR_MGMT1,
                                  BIT(MPU_PWR1_SLEEP_BIT), val);
}

/****************************************************************************************************/
/**
 * @brief 获取传感器休眠状态
 *
 * @param pChip 传感器实例指针
 * @return true处于休眠，false处于运行
 */
static bool mpuSensor_getSleep(mpu_sensor_t *pChip)
{
    uint8_t data = 0;

    i2c_reg_read_byte_dt(&pChip->i2c, MPU_PWR_MGMT1, &data);

    return (data & BIT(MPU_PWR1_SLEEP_BIT)) ? true : false;
}

/****************************************************************************************************/
/**
 * @brief 设置采样率
 *
 * @param pChip 传感器实例指针
 * @param rate 采样率(Hz)，范围4~1000
 * @return 0表示成功，负数表示失败
 */
int mpuSensor_set_rate(mpu_sensor_t *pChip, uint16_t rate)
{
    uint8_t data = 0;
    int ret;

    if (rate > 1000)
    {
        rate = 1000;
    }
    if (rate < 4)
    {
        rate = 4;
    }

    data = (1000 / rate) - 1;

    ret = i2c_reg_write_byte_dt(&pChip->i2c, MPU_SMPLRT_DIV, data);
    if (ret < 0)
    {
        return ret;
    }

    /* 设置LPF为采样率的1/2 */
    return mpuSensor_set_lpf(pChip, data / 2);
}

/****************************************************************************************************/
/**
 * @brief 设置是否启用温度传感器
 *
 * @param pChip 传感器实例指针
 * @param isON true启用，false禁用
 * @return 0表示成功，负数表示失败
 */
static int mpuSensor_set_Temp_isON(mpu_sensor_t *pChip, bool isON)
{
    uint8_t val = isON ? 0 : BIT(MPU_PWR1_TEMP_DIS_BIT);

    return i2c_reg_update_byte_dt(&pChip->i2c, MPU_PWR_MGMT1,
                                  BIT(MPU_PWR1_TEMP_DIS_BIT), val);
}

/****************************************************************************************************/
/**
 * @brief 设置陀螺仪满量程范围
 *
 * @param pChip 传感器实例指针
 * @param fs_sel 0:±250dps, 1:±500dps, 2:±1000dps, 3:±2000dps
 * @return 0表示成功，负数表示失败
 */
static int mpuSensor_set_gyro_fssel(mpu_sensor_t *pChip, uint8_t fs_sel)
{
    uint8_t val = (fs_sel << MPU_GCONFIG_FS_SEL_BIT);

    return i2c_reg_update_byte_dt(&pChip->i2c, MPU_GYRO_CONFIG,
                                  MPU_GCONFIG_FS_SEL_BIT_MASK << MPU_GCONFIG_FS_SEL_BIT, val);
}

/****************************************************************************************************/
/**
 * @brief 设置加速度满量程范围
 *
 * @param pChip 传感器实例指针
 * @param fs_sel 0:±2g, 1:±4g, 2:±8g, 3:±16g
 * @return 0表示成功，负数表示失败
 */
static int mpuSensor_set_accel_fssel(mpu_sensor_t *pChip, uint8_t fs_sel)
{
    uint8_t val = (fs_sel << MPU_ACONFIG_FS_SEL_BIT);

    return i2c_reg_update_byte_dt(&pChip->i2c, MPU_ACCEL_CONFIG,
                                  MPU_ACONFIG_FS_SEL_MASK << MPU_ACONFIG_FS_SEL_BIT, val);
}

/****************************************************************************************************/
/**
 * @brief 设置运动检测阈值
 *
 * @param pChip 传感器实例指针
 * @param mot_thr 阈值(0~255)
 * @return 0表示成功，负数表示失败
 */
static int mpuSensor_set_accel_MotThr(mpu_sensor_t *pChip, uint8_t mot_thr)
{
    return i2c_reg_write_byte_dt(&pChip->i2c, MPU_MOTION_THR, mot_thr);
}

/****************************************************************************************************/
/**
 * @brief 设置中断引脚配置
 *
 * @param pChip 传感器实例指针
 * @param cfg 配置值
 * @return 0表示成功，负数表示失败
 */
static int mpuSensor_set_int_Pincfg(mpu_sensor_t *pChip, uint8_t cfg)
{
    return i2c_reg_write_byte_dt(&pChip->i2c, MPU_INT_PIN_CONFIG, cfg);
}

/****************************************************************************************************/
/**
 * @brief 设置中断使能
 *
 * @param pChip 传感器实例指针
 * @param cfg 中断配置值
 * @return 0表示成功，负数表示失败
 */
static int mpuSensor_Set_int_EN(mpu_sensor_t *pChip, uint8_t cfg)
{
    return i2c_reg_write_byte_dt(&pChip->i2c, MPU_INT_ENABLE, cfg);
}

/****************************************************************************************************/
/**
 * @brief 读取中断状态
 *
 * @param pChip 传感器实例指针
 * @return 中断状态值
 */
static uint8_t mpuSensor_read_int_status(mpu_sensor_t *pChip)
{
    uint8_t data = 0;
    i2c_reg_read_byte_dt(&pChip->i2c, MPU_INT_STATUS, &data);
    return data;
}

/****************************************************************************************************/
/**
 * @brief 设置FIFO使能
 *
 * @param pChip 传感器实例指针
 * @param cfg true使能，false禁用
 * @return 0表示成功，负数表示失败
 */
static int mpuSensor_set_fifoEN(mpu_sensor_t *pChip, bool cfg)
{
    uint8_t val = cfg ? BIT(MPU_USERCTRL_FIFO_EN_BIT) : 0;

    return i2c_reg_update_byte_dt(&pChip->i2c, MPU_USER_CTRL,
                                  BIT(MPU_USERCTRL_FIFO_EN_BIT), val);
}

/****************************************************************************************************/
/**
 * @brief 复位FIFO
 *
 * @param pChip 传感器实例指针
 * @return 0表示成功，负数表示失败
 */
int mpuSensor_resetfifo(mpu_sensor_t *pChip)
{
    return i2c_reg_update_byte_dt(&pChip->i2c, MPU_USER_CTRL,
                                  BIT(MPU_USERCTRL_FIFO_RESET_BIT),
                                  BIT(MPU_USERCTRL_FIFO_RESET_BIT));
}

/****************************************************************************************************/
/**
 * @brief 获取FIFO中数据数量
 *
 * @param pChip 传感器实例指针
 * @return FIFO中数据字节数
 */
static uint16_t mpuSensor_get_fifoCount(mpu_sensor_t *pChip)
{
    uint8_t data[2] = {0};

    i2c_burst_read_dt(&pChip->i2c, MPU_FIFO_COUNT_H, data, 2);

    return (uint16_t)((data[0] << 8) | data[1]);
}

/****************************************************************************************************/
/**
 * @brief 设置低功耗模式唤醒频率
 *
 * @param pChip 传感器实例指针
 * @param freq 唤醒频率(0~3)
 * @return 0表示成功，负数表示失败
 */
int mpuSensor_set_wakeUpFreq(mpu_sensor_t *pChip, uint8_t freq)
{
    uint8_t data = 0;
    int ret;

    /* 设置进入Cycle模式 */
    ret = i2c_reg_read_byte_dt(&pChip->i2c, MPU_PWR_MGMT1, &data);
    if (ret < 0)
    {
        return ret;
    }

    data &= ~BIT(MPU_PWR1_SLEEP_BIT);   /* 退出SLEEP状态 */
    data |= BIT(MPU_PWR1_CYCLE_BIT);    /* 进入Cycle模式 */
    data |= BIT(MPU_PWR1_TEMP_DIS_BIT); /* 关闭温度探测 */

    ret = i2c_reg_write_byte_dt(&pChip->i2c, MPU_PWR_MGMT1, data);
    if (ret < 0)
    {
        return ret;
    }

    /* 设置唤醒频率 */
    data = (freq << MPU_PWR2_LP_WAKE_CTRL_BIT);

    return i2c_reg_write_byte_dt(&pChip->i2c, MPU_PWR_MGMT2, data);
}

/****************************************************************************************************/
/**
 * @brief 设置加速度计使能
 *
 * @param pChip 传感器实例指针
 * @param isON true使能，false禁用
 * @return 0表示成功，负数表示失败
 */
static int mpuSensor_set_accel_isON(mpu_sensor_t *pChip, bool isON)
{
    uint8_t val = isON ? 0 : MPU_PWR2_STBY_XYZA_BITS;

    return i2c_reg_update_byte_dt(&pChip->i2c, MPU_PWR_MGMT2,
                                  MPU_PWR2_STBY_XYZA_BITS, val);
}

/****************************************************************************************************/
/**
 * @brief 设置陀螺仪使能
 *
 * @param pChip 传感器实例指针
 * @param isON true使能，false禁用
 * @return 0表示成功，负数表示失败
 */
static int mpuSensor_set_gyro_isON(mpu_sensor_t *pChip, bool isON)
{
    uint8_t val = isON ? 0 : MPU_PWR2_STBY_XYZG_BITS;

    return i2c_reg_update_byte_dt(&pChip->i2c, MPU_PWR_MGMT2,
                                  MPU_PWR2_STBY_XYZG_BITS, val);
}

/****************************************************************************************************/
/**
 * @brief 获取芯片ID
 *
 * @param pChip 传感器实例指针
 * @return 芯片ID (MPU6500应为0x70)
 */
uint8_t mpuSensor_get_mpuID(mpu_sensor_t *pChip)
{
    uint8_t data = 0;
    i2c_reg_read_byte_dt(&pChip->i2c, MPU_WHO_AM_I, &data);
    return data;
}

/****************************************************************************************************/
/**
 * @brief 设置数字低通滤波器
 *
 * @param pChip 传感器实例指针
 * @param dlpf_cfg 滤波器配置(0~6)
 * @return 0表示成功，负数表示失败
 */
int mpuSensor_set_lpf(mpu_sensor_t *pChip, uint8_t dlpf_cfg)
{
    if (dlpf_cfg > 6)
    {
        dlpf_cfg = 6;
    }

    uint8_t val = (dlpf_cfg << MPU_CONFIG_DLPF_CFG_BIT);

    return i2c_reg_write_byte_dt(&pChip->i2c, MPU_CONFIG, val);
}

/****************************************************************************************************/
/**
 * @brief 设置时钟源
 *
 * @param pChip 传感器实例指针
 * @param cfg 时钟源配置(0~3)
 *            0: 内部8MHz晶振
 *            1: X轴陀螺仪PLL
 *            2: Y轴陀螺仪PLL
 *            3: Z轴陀螺仪PLL
 * @return 0表示成功，负数表示失败
 */
static int mpuSensor_set_clkSel(mpu_sensor_t *pChip, uint8_t cfg)
{
    uint8_t val = (cfg << MPU_PWR1_CLKSEL_BIT);

    return i2c_reg_update_byte_dt(&pChip->i2c, MPU_PWR_MGMT1,
                                  MPU_PWR1_CLKSEL_MASK << MPU_PWR1_CLKSEL_BIT, val);
}

/****************************************************************************************************/
/**
 * @brief 设置I2C主模式使能
 *
 * @param pChip 传感器实例指针
 * @param cfg true使能，false禁用
 * @return 0表示成功，负数表示失败
 */
static int mpuSensor_set_enMastI2C(mpu_sensor_t *pChip, bool cfg)
{
    uint8_t val = cfg ? BIT(MPU_USERCTRL_I2C_MST_EN_BIT) : 0;

    return i2c_reg_update_byte_dt(&pChip->i2c, MPU_USER_CTRL,
                                  BIT(MPU_USERCTRL_I2C_MST_EN_BIT), val);
}

/****************************************************************************************************/
/**
 * @brief 读取三轴加速度数据
 *
 * @param pChip 传感器实例指针
 * @param accelVal 加速度数据数组[3] (x, y, z)
 * @return 0表示成功，负数表示失败
 */
int mpuSensor_get_accel(mpu_sensor_t *pChip, int16_t accelVal[3])
{
    uint8_t data[6];
    int ret;

    ret = i2c_burst_read_dt(&pChip->i2c, MPU_ACCEL_XOUT_H, data, 6);
    if (ret < 0)
    {
        return ret;
    }

    accelVal[0] = (int16_t)((data[0] << 8) | data[1]);
    accelVal[1] = (int16_t)((data[2] << 8) | data[3]);
    accelVal[2] = (int16_t)((data[4] << 8) | data[5]);

    return 0;
}

/****************************************************************************************************/
/**
 * @brief 读取三轴陀螺仪数据
 *
 * @param pChip 传感器实例指针
 * @param gyroVal 陀螺仪数据数组[3] (x, y, z)
 * @return 0表示成功，负数表示失败
 */
int mpuSensor_get_gyro(mpu_sensor_t *pChip, int16_t gyroVal[3])
{
    uint8_t data[6];
    int ret;

    ret = i2c_burst_read_dt(&pChip->i2c, MPU_GYRO_XOUT_H, data, 6);
    if (ret < 0)
    {
        return ret;
    }

    gyroVal[0] = (int16_t)((data[0] << 8) | data[1]);
    gyroVal[1] = (int16_t)((data[2] << 8) | data[3]);
    gyroVal[2] = (int16_t)((data[4] << 8) | data[5]);

    return 0;
}

/****************************************************************************************************/
/**
 * @brief 读取温度数据
 *
 * @param pChip 传感器实例指针
 * @param pTempVal 温度值指针
 * @return 0表示成功，负数表示失败
 *
 * 温度计算公式: Temperature = (TEMP_OUT / 340.0) + 21.0 (°C)
 */
int mpuSensor_get_temp(mpu_sensor_t *pChip, int16_t *pTempVal)
{
    uint8_t data[2];
    int ret;

    ret = i2c_burst_read_dt(&pChip->i2c, MPU_TEMP_OUT_H, data, 2);
    if (ret < 0)
    {
        return ret;
    }

    *pTempVal = (int16_t)((data[0] << 8) | data[1]);

    return 0;
}

/****************************************************************************************************/
/**
 * @brief MPU数据处理回调（供外部中断调用）
 *
 * 该函数在中断回调中调用，读取传感器数据并打印
 */
void OnQM_mpu_Proce(void)
{
    int16_t nAccel[3] = {0};
    int16_t nGyro[3] = {0};
    int16_t nTemp = 0;
    float accel_x, accel_y, accel_z;
    float gyro_x, gyro_y, gyro_z;
    float temp_c;

    /* 关闭加速度计和陀螺仪 */
    mpuSensor_set_accel_isON(&s_mpuSensor, false);
    mpuSensor_set_gyro_isON(&s_mpuSensor, false);

    /* 读取加速度、陀螺仪、温度数据 */
    mpuSensor_get_accel(&s_mpuSensor, nAccel);
    mpuSensor_get_gyro(&s_mpuSensor, nGyro);
    mpuSensor_get_temp(&s_mpuSensor, &nTemp);

    /* 进入休眠，停止测量 */
    mpuSensor_setSleep(&s_mpuSensor, true);

    /* 转换为物理量并打印 */
    /* 加速度: ±4g量程, 32768 LSB/g, 转换为m/s² */
    accel_x = (float)nAccel[0] * 4.0f * 9.8f / 32768.0f;
    accel_y = (float)nAccel[1] * 4.0f * 9.8f / 32768.0f;
    accel_z = (float)nAccel[2] * 4.0f * 9.8f / 32768.0f;

    /* 陀螺仪: ±250dps量程, 32768 LSB/250dps */
    gyro_x = (float)nGyro[0] * 250.0f / 32768.0f;
    gyro_y = (float)nGyro[1] * 250.0f / 32768.0f;
    gyro_z = (float)nGyro[2] * 250.0f / 32768.0f;

    /* 温度: Temp = (raw / 340.0) + 21.0 */
    temp_c = (float)nTemp / 340.0f + 21.0f;

    LOG_INF("A[x=%.2f y=%.2f z=%.2f] G[x=%.2f y=%.2f z=%.2f] T[%.2f]",
             (double)accel_x,  (double)accel_y,  (double)accel_z,
             (double)gyro_x,  (double)gyro_y,  (double)gyro_z,
             (double)temp_c);
}

/****************************************************************************************************/
/**
 * @brief 读取传感器数据并打印（用于测试）
 */
void mpuSensor_read_and_print(void)
{
    int16_t nAccel[3] = {0};
    int16_t nGyro[3] = {0};
    int16_t nTemp = 0;
    float accel_x, accel_y, accel_z;
    float gyro_x, gyro_y, gyro_z;
    float temp_c;
    // 读取三轴加速度
    mpuSensor_get_accel(&s_mpuSensor, nAccel);
    // 读取陀螺仪数据
    mpuSensor_get_gyro(&s_mpuSensor, nGyro);
    // 读取温度数据
    mpuSensor_get_temp(&s_mpuSensor, &nTemp);

    accel_x = (float)nAccel[0] * 4.0f * 9.8f / 32768.0f;
    accel_y = (float)nAccel[1] * 4.0f * 9.8f / 32768.0f;
    accel_z = (float)nAccel[2] * 4.0f * 9.8f / 32768.0f;

    gyro_x = (float)nGyro[0] * 250.0f / 32768.0f;
    gyro_y = (float)nGyro[1] * 250.0f / 32768.0f;
    gyro_z = (float)nGyro[2] * 250.0f / 32768.0f;

    temp_c = (float)nTemp / 340.0f + 21.0f;

    LOG_INF("RAW: Ax=%d Ay=%d Az=%d Gx=%d Gy=%d Gz=%d T=%d",
            nAccel[0], nAccel[1], nAccel[2],
            nGyro[0], nGyro[1], nGyro[2],
            nTemp);

    LOG_INF("A[x=%.2f y=%.2f z=%.2f] G[x=%.2f y=%.2f z=%.2f] T[%.2f]",
             (double)accel_x,  (double)accel_y,  (double)accel_z,
             (double)gyro_x,  (double)gyro_y,  (double)gyro_z,
             (double)temp_c);
}
