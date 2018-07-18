#ifndef FLASHLAYOUT_H
#define FLASHLAYOUT_H
#include "FreescaleIAP.h"
#include "UserConfig.h"
/*--------------------------------------------------------------------------------------------------------
    Flash Layout
    bootloader(32K)     0x00000~0x07FFF
    application(128K)   0x08000~0x27FFF
    OTA (128K)          0x28000~0x47FFF
    Checksum (4B)       0x48000~0x48003   code checksum & ota code checksum: xx xx xx xx 
    OTA CODE VER(60B)   0x48004~0x4803F
    Param (4K)          0xFF000~0xFFFFF
		ServerIP (4K)				0xFE000~0xFF000   IP1|IP2|IP2|IP3|PORT1|PORT2|CHECKSUM1|CHECKSUM2 Total 8 bytes
---------------------------------------------------------------------------------------------------------*/
#define CHIP_MK64FX512                //running in mk64fx512

#define BOOTLOADER_START_ADDRESS    0
#define BOOTLOADER_SIZE             0x8000   // 32K  
#define CODE_SECTOR_NUM             32   // 32*4K Bytes    8*4K for bootloader total 160KB
#define MIN_CODE_SECTOR_NUM         16 // 16*4K Bytes minimal code size,  code size always > 90 KB
#define CODE_START_ADDRESS          (BOOTLOADER_SIZE)//0
#define CODE_SIZE                   (CODE_SECTOR_NUM * SECTOR_SIZE)
#define OTA_CODE_START_ADDRESS      (CODE_START_ADDRESS + CODE_SIZE)

#define VERSION_STR_ADDRESS         (OTA_CODE_START_ADDRESS + CODE_SIZE)
#define VERSION_STR_LEN             64  // version string 32 bytes  For ex: "WPI-DEMO VERSION 1.0"
#define VERSION_SN_LEN              64    //32 bytes string

#if defined(CHIP_MK64FX512)
	#define PARAM_START_ADDRESS         (0x80000-0x1000)   //  last 4k flash for store param config,in mk64fx512
#else
	#define PARAM_START_ADDRESS         (0x100000-0x1000)   // last 4k flash for store param config,in mk64fn1m0
#endif

#if  defined(EEPROM_ENABLE)
	#define CHARGER_INFO_ADDRESS        (0x00)
#else
	#define CHARGER_INFO_ADDRESS        PARAM_START_ADDRESS
#endif

#define SERVER_IP_ADDRESS           (PARAM_START_ADDRESS - 0x1000)

#endif
