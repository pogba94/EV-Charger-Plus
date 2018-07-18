/*----------------------------------------------------------------------------
 * Includes
-----------------------------------------------------------------------------*/
#include "flashLayout.h"
#include "UserConfig.h"
#include "lib_crc16.h"
#include "ota.h"

void update_code(void);
/*----------------------------------------------------------------------------
 * Function definition
-----------------------------------------------------------------------------*/
/*!
	@birfe OTA init function
	@input None
	@output None
*/
void init_ota(void)
{
    char* cdata = (char*)VERSION_STR_ADDRESS;
    char* codePartition = (char*) CODE_START_ADDRESS;
    char* OTACodePartition = (char*) OTA_CODE_START_ADDRESS;
    int i = 0;
    int codecrc16,otacodecrc16,codechecksum;

    codecrc16 = calculate_crc16(codePartition, CODE_SIZE);
    codechecksum = (cdata[0]<<8)|cdata[1];

    if((codechecksum==0xFFFF)||(codechecksum==0x0)||(codecrc16 != codechecksum)) {
        pc.printf("ota version unavaible, recover ota partition!\r\n");
        for(i=0; i<CODE_SECTOR_NUM; i++) {
            erase_sector(OTA_CODE_START_ADDRESS+i*SECTOR_SIZE);
            program_flash(OTA_CODE_START_ADDRESS+i*SECTOR_SIZE,codePartition+i*SECTOR_SIZE, SECTOR_SIZE);
        }
        otacodecrc16 = calculate_crc16(OTACodePartition, CODE_SIZE);
        if(otacodecrc16 == codecrc16) {
            memset(tempBuffer,0x0,VERSION_STR_LEN);
            tempBuffer[0] = tempBuffer[2] = (codecrc16 >> 8) & 0xff;
            tempBuffer[1] = tempBuffer[3] = codecrc16 & 0xff;
            sprintf(tempBuffer+4,"%s",VERSION_INFO);
            erase_sector(VERSION_STR_ADDRESS);
            program_flash(VERSION_STR_ADDRESS,tempBuffer, SECTOR_SIZE);
        }
    } else {
        pc.printf("Current OTA checksum: %2X%2X  %2X%2X OTA version:  %s\r\n",cdata[0],cdata[1],cdata[2],cdata[3],cdata+4);
			  pc.printf("Current version:%s\r\n",VERSION_INFO);
			  if(strcmp(VERSION_INFO,cdata+4)) { // if version info changed, update code flash
            pc.printf("start to update fw...\r\n");//never run here
            update_code();  //never run here
        } else {
            pc.printf("the FW is already the newest!\r\n");
        }
    }

}
/*!
	@birfe Software reset for updating
	@input None
	@output None
*/
void update_code(void)
{
    pc.printf("update, system will be reset now, please wait...\r\n");
    NVIC_SystemReset();
}