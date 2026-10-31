#ifndef __SYMBOL_H
#define __SYMBOL_H
#include <stdint.h>
//-----------------------------------------------------------------------------------//
// 定义一些常用的数据类型短关键字
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

typedef const int32_t sc32;
typedef const int16_t sc16;
typedef const int8_t sc8;

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef const uint32_t uc32;
typedef const uint16_t uc16;
typedef const uint8_t uc8;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
//----------------------------------------输出数据类型定义-------------------------------------------//

typedef enum
{
  _GetMqttConfig = 1, // 获取mqtt配置1
  _SetMqttConfig,     // 设置mqtt配置2
  _GetRunData,        // 获取运行数据3
  _SPIFORMAT,         // SPI格式化4
  _FLASHERASE,        // nvs格式化5
  _GetAllData,        // 获得所有6
  _SetAllData,        // 设置所有7
  _RestartEsp,        // 重启设备8
} _Report_ItemID;

//------------------------------------------时间-----------------------------------------//
typedef struct __packed __SYS_Time
{
  u16 u16Year; // 年  2
  u8 u8Month;  // 月  1
  u8 u8Day;    // 日  1
  u8 u8Hour;   // 时  1
  u8 u8Minute; // 分  1
  u8 u8Second; // 秒  1
} stTime, *LpstTime;
#define _stTime_Len sizeof(stTime) // 7

//------------------------------------------信标配置-----------------------------------------//
/**
 * @brief Beacon configuration stored in NVS
 *
 * card_id:      40-bit value (5 bytes big-endian) embedded into UUID bytes [8..12]
 *               Default: 258000000000 = 0x3C11FF9400
 * tx_power:     TX Power calibration byte (signed int8, e.g. -60 for 0xC4)
 *               Default: -60
 * tx_interval:  Advertising cycle period in ms (total = on + off)
 *               Advertising on time is fixed 100ms, sleep = tx_interval - 100
 *               Default: 2500
 */
typedef struct __packed __beacon_config
{
  uint64_t card_id;     /* 8 bytes */
  int8_t tx_power;      /* 1 byte  */
  uint8_t _pad[3];      /* padding for alignment */
  uint32_t tx_interval; /* 4 bytes */
} stBeacon_Cfg, *LPstBeacon_Cfg;
#define _stBeacon_Cfg_Len sizeof(stBeacon_Cfg) /* total: 16 bytes */

extern stBeacon_Cfg m_BeaconCfg; /* Global beacon configuration */

#endif

/* __SYMBOL_H */
