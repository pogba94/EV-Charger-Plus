#ifndef OTACHARGER_H
#define OTACHARGER_H

#include "UserConfig.h"

#ifdef CHARGING_CURRENT_16A
	#define VERSION_INFO  "WPI-EVChargerPlus_16A_K64F_20180411_V0.0.1"
#endif

#ifdef CHARGING_CURRENT_32A
	#define VERSION_INFO  "WPI-EVChargerPlus_32A_K64F_20180411_V0.0.1"
#endif

void init_ota(void);
void update_code(void);

#endif