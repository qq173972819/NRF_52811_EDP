/****************************************************************************************************/
#ifndef __APPSYS_INCLUDES_H__
#define __APPSYS_INCLUDES_H__
/****************************************************************************************************/

#ifndef __APP_SYMBOL_H__
#include "Symbol.h"
#endif

#ifndef __SYMBOL_H__
#include "Symbol.h"
#endif

#if _EN_WATCHDOG
#ifndef __APP_WATCHDOG_H__
#include "APP/app_watchdog/app_watchdog.h"
#endif
#endif


#if _EN_POWER
#ifndef __APP_POWER_H__
#include "APP/app_power/app_power.h"
#endif
#endif

#if _EN_NVS_STORAGE
#ifndef __APP_NVS_H__
#include "APP/app_nvs/app_nvs.h"
#endif
#endif

/****************************************************************************************************/
#endif /* __APPSYS_INCLUDES_H__*/
// end file
