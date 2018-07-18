/**
	---------------------------------------------------------------------------
	* Describtion:            Firmware for AC EV Charger Plus
	* Company:                WPI-ATU
	* Author:                 Orange
	* Version:                V1.0
	* Date:                   2018-04-11
	* Function List:
			1. rs485 Communication
			2. NB-IOT Connectivity
	    3. Wechat control
			4. Follow the GB/T 18487.1-2015 Standard
			5. Use Mbed OS 5.7
	* History:
	---------------------------------------------------------------------------
**/

/*----------------------------------------------------------------------------
 * Includes
-----------------------------------------------------------------------------*/
#include "EVCharger.h"
#include "cJSON.h"
#include "lib_crc16.h"
#include "UserConfig.h"
#include "ota.h"
#include "me3612.h"
/*----------------------------------------------------------------------------
 * Global Variable
-----------------------------------------------------------------------------*/
MSGType msgType = invalidType ; 
MSGID msgID = invalidID;
MSGID notifySendID = invalidID;
int notifySendCounter = 0;
char msgIdstr[10];
volatile int respcode ;
char jsonBuffer[256];
ChargerInfo chargerInfo;
ChargerException chargerException;
OTAInfo otaInfo;
float curChargingEnergy = 0;
int curChargingDuration = 0;
int setChargingDuration = 0;
float setChargingEnergy = 0;
int curChargingType = CHARGING_TYPE_NONE;
int chargingEndType = END_NONE;
/*----------------------------------------------------------------------------
 * Function Definition
-----------------------------------------------------------------------------*/
/*!
	@birfe Get endType of charging
	@input charger Status
	@output endType
*/
int get_charging_end_type(int chargerStatus)
{
	int type;
	if(chargerStatus & METER_CRASHED_FLAG(1))
		type = END_METER_EXCEPTION;
	else if(chargerStatus & CHARGING_VOLTAGE_ERROR_FLAG(1))
		type = END_VOLTAGE_ERROR;
	else if(chargerStatus & CHARGING_CURRENT_ERROR_FLAG(1))
		type = END_CURRENT_ERROR;
	else if(chargerStatus & CHARGING_DISABLE_FLAG(1))
		type = END_CONNECT_EXCEPTION;
	else if(chargerStatus & CHARGING_TIME_OVER_FLAG(1))
		type = END_OVER_TIME;
	else if(chargerStatus & CHARGING_ENERGY_OVER_FLAG(1))
		type = END_OVER_ENERGY;
	else
		type = END_NONE;
	return type;
}
/*!
	@birfe init chargerException structure
	@input None
	@output None
*/
void init_charger(void)
{
    chargerException.serverConnectedFlag = false;
    chargerException.meterCrashedFlag = true;  //false -> true as default
    chargerException.chargingVoltageErrorFlag = false;
    chargerException.chargingCurrentErrorFlag = false;
    chargerException.chargingTimeOverFlag = false;
    chargerException.chargingEnableFlag = false;
	
	  #if defined(EEPROM_ENABLE)
			load_charger_info_from_eeprom();
	  #else
			load_charger_info_from_flash();
	  #endif
}
/*!
	@birfe Enter preStart stage,Enable PWM
	@input None
	@output None
*/
void pre_start_charging(void)
{
	  chargingEndType = END_NONE;
		gChargingState = EV_CONNECTED_PRE_START_CHARGING;
	  #ifdef CHARGING_CURRENT_32A
		enable_cp_pwm(PWM_DUTY_CURRENT_32A);
	  pc.printf("enable PWM,PWM duty:%f wait for S2 CLOSED\r\n",PWM_DUTY_CURRENT_32A);
	  #endif
	
    #ifdef CHARGING_CURRENT_16A
    enable_cp_pwm(PWM_DUTY_CURRENT_16A);
    pc.printf("enable PWM,PWM duty:%f wait for S2 CLOSED\r\n",PWM_DUTY_CURRENT_16A);
	  #endif
}
/*!
	@birfe Enter preStop stage,Disabel PWM
	@input None
	@output None
*/
void pre_stop_charging(void)
{
		gChargingState = EV_CONNECTED_PRE_STOP_CHARGING;
		disable_cp_pwm();
		pc.printf("pre stop charging, disable PWM!\r\n");
		startS2SwitchOnOffCounterFlag = true;
		S2SwitchOnOffCounter = 0;
}
/*!
	@birfe Pause charging,Relay off
	@input 1->enable pause charging,0->disable pause charging
	@output None
*/
void pause_charging(bool onoff)
{
		if(onoff == true){
			pauseChargingFlag = true;
			switchRelayOnoff = RELAY_OFF;  // ericyang 20170111
		}else{
			pauseChargingFlag = false;
			switchRelayOnoff = RELAY_ON;  // ericyang 20170111	
		}
}

/*!
	@birfe Enter charging stage,init parameters and relay on
	@input None
	@output None
*/
void start_charging(void)
{
	 if(startContinueChargingFlag == false){ //init relative parameters
			startEnergyReadFromMeter = totalEnergyReadFromMeter;
			chargerInfo.energy = 0; 
			chargerInfo.duration = 0;
		}else{
			startContinueChargingFlag = false;  //continue charging
		}
		chargerInfo.status = charging;
		eventHandle.checkMeterFlag = false;
		switchRelayOnoff = RELAY_ON;
}
/*!
	@birfe Stop charging,init relative parameters and relay off
	@input None
	@output None
*/
void stop_charging(void)
{
	gChargingState = EV_IDLE;
	switchRelayOnoff = RELAY_OFF;
	#ifdef LED_INFO_ENABLE
		CHARGING_LED_OFF;
	#endif
  pc.printf("stop charging!\r\n");
}

/*!
	@birfe Init chargerInfo
	@input None
	@output None
*/
void init_charger_info(void)
{
	curChargingDuration = chargerInfo.duration;
	curChargingEnergy = chargerInfo.energy;
	curChargingType = chargerInfo.chargingType;
	setChargingDuration = chargerInfo.setDuration;
  setChargingEnergy = chargerInfo.setEnergy;
	chargerInfo.setDuration = 0;
	chargerInfo.duration = 0;
	chargerInfo.energy = totalEnergyReadFromMeter;
	startEnergyReadFromMeter = 0;
	chargerInfo.setEnergy = 0;
	chargerInfo.chargingType = CHARGING_TYPE_NONE;
}

/*!
	@birfe Parse text to JSON 
	@input pointer to string
	@output None
*/
void parse_recv_msg(char *text)
{
    cJSON *json;
    json=cJSON_Parse(text);
    if (!json) {
        pc.printf("not json string, start to parse another way!\r\n");
        parse_bincode(text);
    } else {
        pc.printf("start parse json!\r\n");
        cJSON *data = cJSON_GetObjectItem(json,"data");
        respcode = 0 ;
        if(cJSON_GetObjectItem(json,"reqType") != NULL) {
            msgType = req;
            sprintf(jsonBuffer,"%s",cJSON_GetObjectItem(json,"reqType")->valuestring);
            pc.printf("reqType: %s\r\n",jsonBuffer);
        } else if(cJSON_GetObjectItem(json,"respType") != NULL) {
            msgType = resp;
            sprintf(jsonBuffer,"%s",cJSON_GetObjectItem(json,"respType")->valuestring);
            pc.printf("respType: %s\r\n",jsonBuffer);
        } else {
            msgType = invalidType;
        }
        if(msgType == req) {
            if(strcmp(jsonBuffer,SETChargingStart) == NULL) {
                msgID = setChargingStart;
							  if(cJSON_HasObjectItem(data,"type")==1 && cJSON_HasObjectItem(data,"time")==1 
									&& cJSON_HasObjectItem(data,"energy")==1 && cJSON_HasObjectItem(data,"userId")==1){
										sprintf(msgIdstr,cJSON_GetObjectItem(data,"msgId")->valuestring);
										pc.printf("recv msg %s = %d, msgid = %s\r\n",SETChargingStart, chargerInfo.setDuration,msgIdstr);
                    if(chargerInfo.status == connected){
											chargerInfo.chargingType = cJSON_GetObjectItem(data,"type")->valueint;
											chargerInfo.userId = cJSON_GetObjectItem(data,"userId")->valueint;
											if(chargerInfo.chargingType == CHARGING_TYPE_MONEY || chargerInfo.chargingType == CHARGING_TYPE_TIME){
												chargerInfo.setEnergy = cJSON_GetObjectItem(data,"energy")->valuedouble;
												chargerInfo.setDuration = cJSON_GetObjectItem(data,"time")->valueint;
												if((chargerInfo.chargingType == CHARGING_TYPE_MONEY  && chargerInfo.setEnergy > 0) ||
													  (chargerInfo.chargingType == CHARGING_TYPE_TIME && chargerInfo.setDuration >0)){
												    pc.printf("charging type:%d!\tsetEnergy=%f\tsetDuration=%d\r\n"
															,chargerInfo.chargingType,chargerInfo.setEnergy,chargerInfo.setDuration);
														pre_start_charging();
													respcode = RESP_OK;
													startS2SwitchWaitCounterFlag = true;
													waitS2SwitchOnCounter = 0;
													cmd_msg_resp_handle(msgID);
												}else{
												 respcode = RESP_PARAM_ERROR;
												 pc.printf("charging parameters error!\r\n");
												 cmd_msg_resp_handle(msgID);   //Parameter error, respond immediately
												}
											}else{
												respcode = RESP_PARAM_ERROR;
												pc.printf("charging type error!\r\n");
												cmd_msg_resp_handle(msgID);   //Parameter error, respond immediately
											}
									}else{
										respcode = RESP_ILLEGAL;
										pc.printf("illegal request!\r\n");
										cmd_msg_resp_handle(msgID);
									}
								}else{
									respcode = RESP_ITEM_ERROR;
									pc.printf("json item error!\r\n");
									cmd_msg_resp_handle(msgID);
								}
            } else  if(strcmp(jsonBuffer,SETChargingEnd) == NULL) {
                msgID = setChargingEnd;
                sprintf(msgIdstr,cJSON_GetObjectItem(data,"msgId")->valuestring);
                pc.printf("recv msg %s , msgid = %s\r\n",SETChargingEnd,msgIdstr);
							if((chargerInfo.status & CHARGER_STATUS_MASK)==charging){
                respcode = RESP_OK;
								pre_stop_charging();
							  chargingEndType = END_IN_ADVANCE;
							}else{
								respcode = RESP_ILLEGAL;
								pc.printf("not charging!Cannot be stopped!\r\n");
							}
							cmd_msg_resp_handle(msgID);
            }
						else  if(strcmp(jsonBuffer,SETUpdateVersion) == NULL){
							msgID = setUpdateVersion;						
							sprintf(msgIdstr,cJSON_GetObjectItem(data,"msgId")->valuestring);
							sprintf(otaInfo.latestVersionFromServer,cJSON_GetObjectItem(data,"versionNumber")->valuestring);
							sprintf(otaInfo.latestVersionSNFromServer,cJSON_GetObjectItem(data,"versionSN")->valuestring);
							pc.printf("latest versionSN from server:%s\r\n",otaInfo.latestVersionSNFromServer);
							otaInfo.FWSizeFromServer = cJSON_GetObjectItem(data,"versionSize")->valueint;
              otaInfo.FWCheckSumFromServer = cJSON_GetObjectItem(data,"checkSum")->valueint;
							otaInfo.oldVersionId = cJSON_GetObjectItem(data,"oldVersionId")->valueint;
					    otaInfo.newVersionId = cJSON_GetObjectItem(data,"newVersionId")->valueint;
							respcode = RESP_OK;
							
							pc.printf("recv msg %s , msgid = %s\r\n",SETUpdateVersion,msgIdstr);
							pc.printf("lastest version : %s , size : %d Bytes , checkSum : %x\r\n",
							          otaInfo.latestVersionFromServer,otaInfo.FWSizeFromServer,otaInfo.FWCheckSumFromServer);
							pc.printf("oldVersionId:%d\tnewVersionId:%d\r\n",otaInfo.oldVersionId,otaInfo.newVersionId);  //orangecai 20170414
							if(otaInfo.FWSizeFromServer % SECTOR_SIZE == 0) {
                    otaInfo.FWSectorNum = otaInfo.FWSizeFromServer / SECTOR_SIZE;
                    otaInfo.lastSectorSize = SECTOR_SIZE;
                } else {
                    otaInfo.FWSectorNum = otaInfo.FWSizeFromServer / SECTOR_SIZE + 1;
                    otaInfo.lastSectorSize = otaInfo.FWSizeFromServer % SECTOR_SIZE;
                }
								
							if((otaInfo.FWSectorNum > CODE_SECTOR_NUM) || (otaInfo.FWSectorNum < MIN_CODE_SECTOR_NUM)) {
                    pc.printf("fw size %d is too large or too small, please check...\r\n",otaInfo.FWSizeFromServer);
                } else {
                    if(strcmp(VERSION_INFO,otaInfo.latestVersionFromServer)) { // fw version is not the same, update flash ota code;
                        eventHandle.getLatestFWFromServerFlag = true; //  get fw code from server;
                        otaInfo.currentFWSector = 0;
                        pc.printf("has new FW version On Server,start to update!\r\n");
                    } else {
                        pc.printf("the fw version is the newest!\r\n");
                    }
                }
							cmd_msg_resp_handle(msgID);
						}
						#ifdef RTC_ENABLE
						else if(strcmp(jsonBuffer,SETCalibrateTime) == NULL){
							  uint32_t timeSeconds;
								msgID = setCalibrateTime;
								sprintf(msgIdstr,cJSON_GetObjectItem(data,"msgId")->valuestring);
							  timeSeconds = (uint32_t)cJSON_GetObjectItem(data,"timestamp")->valueint;
							  pc.printf("recv msg %s , msgid = %s\r\n",SETCalibrateTime,msgIdstr);
							  pc.printf("timeSeconds = %d\r\n",timeSeconds);
							  respcode = 100;
							  rtc_init(timeSeconds);  //calibrate rtc
							  cmd_msg_resp_handle(msgID);
						}
						#endif
						else if(strcmp(jsonBuffer,GETChargerStatus) == NULL) {
                msgID = getChargerStatus;
                sprintf(msgIdstr,cJSON_GetObjectItem(data,"msgId")->valuestring);
                pc.printf("recv msg %s , msgid = %s\r\n",GETChargerStatus, msgIdstr);
                respcode = RESP_OK;
                cmd_msg_resp_handle(msgID);
            }else if(strcmp(jsonBuffer,GETChargingInfo) == NULL) {
                msgID = getChargingInfo;
                sprintf(msgIdstr,cJSON_GetObjectItem(data,"msgId")->valuestring);
                pc.printf("recv msg %s , msgid = %s\r\n",GETChargingInfo, msgIdstr);
							  if((chargerInfo.status & CHARGER_STATUS_MASK)==charging)
									respcode = RESP_OK;
								else
									respcode = RESP_ILLEGAL;
                cmd_msg_resp_handle(msgID);
            }else if(strcmp(jsonBuffer,GETCurVersion) == NULL){
								msgID = getCurVersion;
							  sprintf(msgIdstr,cJSON_GetObjectItem(data,"msgId")->valuestring);
							  pc.printf("recv msg %s , msgid = %s\r\n",GETCurVersion,msgIdstr);
							  respcode = RESP_OK;
							  cmd_msg_resp_handle(msgID);
						}
						#ifdef RTC_ENABLE
						else if(strcmp(jsonBuffer,GETDate) == NULL){
								msgID = getDate;
							  sprintf(msgIdstr,cJSON_GetObjectItem(data,"msgId")->valuestring);
							  pc.printf("recv msg %s , msgid = %s\r\n",GETDate,msgIdstr);
							  respcode = RESP_OK;
							  cmd_msg_resp_handle(msgID);
						}
						#endif
        } else if(msgType == resp) {
            respcode = cJSON_GetObjectItem(data,"respCode")->valueint;
            if(strcmp(jsonBuffer,NOTIFYNewDevice) == NULL) {
                msgID = notifyNewDevice;
                if(cJSON_HasObjectItem(data,"meterNumber") == 1) {
                    sprintf(chargerInfo.meterNumber,cJSON_GetObjectItem(data,"meterNumber")->valuestring);
                    pc.printf("recv msg %s , meterNumber = %s\r\n",NOTIFYNewDevice, chargerInfo.meterNumber);
									  #if defined(EEPROM_ENABLE)
											save_charger_info_to_eeprom();
								    #else
											save_charger_info_to_flash();
                    #endif									
									
                } else {
                    #ifdef DISABLE_NETWORK_CONMUNICATION_FUNC
											eventHandle.stopCommunicationFlag = true;
                    #endif
                    pc.printf("NotifyNewDevice has no meterNumber param, stop Conmunication!\r\n");
                }
            }else  if(strcmp(jsonBuffer,NOTIFYUpdateVersion) == NULL) {
                msgID = notifyUpdateVersion;
            } else  if(strcmp(jsonBuffer,NOTIFYChargerStatus) == NULL) {
                msgID = notifyChargerStatus;
            } else  if(strcmp(jsonBuffer,NOTIFYChargingInfo) == NULL) {
                msgID = notifyChargingInfo;
            } else  if(strcmp(jsonBuffer,NOTIFYEndCharging) == NULL) {
                msgID = notifyEndCharging;
						}	else if(strcmp(jsonBuffer,NOTIFYOTAResult) == NULL){  // orangeCai 20170328
							  msgID = notifyOTAResult;
							} else {
                msgID = invalidID;
            }
            if((notifySendID == msgID)&&(respcode == RESP_OK)) {
                pc.printf("send msg %s successful!\r\n",jsonBuffer);
							  if(notifySendID == notifyOTAResult && otaSuccessFlag == true){
									updateCodeFlag = true;  //start to update code when charger is not charging
									otaSuccessFlag = false;
								}
                notifySendID = invalidID;
            } else {
            }
        } else {
        }
        cJSON_Delete(json);
    }
}

/*
   HEADER----------BLOCK OFFSET-------BLOCK SIZE-------CHECKSUM-------Bin data
  "OTABIN"----0x00 0x00 0x00 0x00------0x10 0x00-------0xFF 0xFF------
*/
/*!
	@birfe Parse binary data
	@input pointer to string
	@output None
*/
#define BLOCKOFFSET_POS 6
#define BLOCKSIZE_POS 10
#define CHECKSUM_POS 12
#define BINDATA_POS 14
void parse_bincode(char *text)
{
    char* buf;
    int blockOffset = 0;
    int blockSize = 0;
    int checksum = 0;

    int crc16;
	  int reWrite = 0;
    int currentSectorSize;

    if(strncmp(text, SOCKET_OTA_HEADER, strlen(SOCKET_OTA_HEADER))== NULL) {
        blockOffset = ((text[BLOCKOFFSET_POS] << 24) | (text[BLOCKOFFSET_POS+1] << 16) |(text[BLOCKOFFSET_POS+2] << 8)|text[BLOCKOFFSET_POS+3]);
        blockSize = ((text[BLOCKSIZE_POS] << 8) | text[BLOCKSIZE_POS+1]);
        checksum = ((text[CHECKSUM_POS] << 8) | text[CHECKSUM_POS+1]);
        pc.printf("read from server: blockoffset = %d blocksize = %d checksum = %x\r\n",blockOffset,blockSize,checksum);

        buf = (char*)malloc(blockSize);
        if(buf == NULL) {
            pc.printf("can't malloc enough memory!\r\n");
        } else {
            if(notifySendID == notifyUpdateVersion) {
                notifySendID = invalidID;
            }
            memcpy(buf,text+BINDATA_POS,blockSize);
            crc16 = calculate_crc16(buf, blockSize);
            pc.printf("crc16 = %x checkSum = %x\r\n",crc16,checksum);

            if(otaInfo.currentFWSector + 1 < otaInfo.FWSectorNum)
                currentSectorSize = SECTOR_SIZE;
            else
                currentSectorSize = otaInfo.lastSectorSize;

            if((otaInfo.currentFWSector*SECTOR_SIZE == blockOffset)
                    && (currentSectorSize == blockSize)
                    && (crc16 == checksum)) {
                // update current sector to flash ota partition;
								IAPCode iapCode;
                pc.printf("write sector %d %d bytes to ota partition\r\n",otaInfo.currentFWSector,blockSize);
											
                erase_sector(OTA_CODE_START_ADDRESS+otaInfo.currentFWSector*SECTOR_SIZE);
								iapCode = program_flash(OTA_CODE_START_ADDRESS+otaInfo.currentFWSector*SECTOR_SIZE,buf, blockSize);
								crc16 = calculate_crc16((char*)(OTA_CODE_START_ADDRESS+otaInfo.currentFWSector*SECTOR_SIZE),blockSize);
								
								while(crc16 != checksum && reWrite < 10){   //try to rewrite data when verify failed
									pc.printf("rewrite to section %d!\r\n",otaInfo.currentFWSector);
									erase_sector(OTA_CODE_START_ADDRESS+otaInfo.currentFWSector*SECTOR_SIZE);
									iapCode = program_flash(OTA_CODE_START_ADDRESS+otaInfo.currentFWSector*SECTOR_SIZE,buf, blockSize);
									pc.printf("iapCode=%d\r\n",iapCode);
									crc16 = calculate_crc16((char*)(OTA_CODE_START_ADDRESS+otaInfo.currentFWSector*SECTOR_SIZE),blockSize);
									reWrite++;
									wait(0.1);
								}
								pc.printf("rewrite times = %d\r\n",reWrite);
								if(crc16 == checksum){
									pc.printf("write and verify section:%d successfully\r\n",otaInfo.currentFWSector);
									otaInfo.currentFWSector++;
								}else{
									pc.printf("write section:%d failed!\r\n",otaInfo.currentFWSector);
									eventHandle.getLatestFWFromServerFlag = false;   //stop update!
									eventHandle.updataVersionFailFlag = true;
									init_ota();     //recover ota partition!
								}
            }
            free(buf);
        }
    }
}
/*!
	@birfe Send massage handler
	@input msgID
	@output None
*/
void notify_msg_handle(MSGID msgid)
{
    int i,len= 0;
    int crc16;
    #ifdef DISABLE_NETWORK_CONMUNICATION_FUNC
    if(eventHandle.stopCommunicationFlag == true)
        return;
    #endif
    if((msgid == invalidID)||(msgid >= unknownMsgID))
        return;

    memset(socketInfo.outBuffer,0,sizeof(socketInfo.outBuffer));

    if(msgid == notifyNewDevice) {
        if(strcmp(chargerInfo.meterNumber, NULL) == NULL) {  
            sprintf(socketInfo.outBuffer,NOTIFY_REQ_NewDevice,EVChargerModelNO,false);
        } else {
            sprintf(socketInfo.outBuffer,NOTIFY_REQ_NewDevice,EVChargerModelNO,true);
        }
    }
		else if(msgid == notifyUpdateVersion) {

        pc.printf("fwSectorNum=%d,otaInfo.currentFWSector=%d\r\n",otaInfo.FWSectorNum,otaInfo.currentFWSector);
        if(otaInfo.currentFWSector < otaInfo.FWSectorNum) {
            if(otaInfo.currentFWSector + 1 == otaInfo.FWSectorNum) {
              sprintf(socketInfo.outBuffer,NOTIFY_REQ_UpdateVersion,otaInfo.latestVersionSNFromServer,otaInfo.currentFWSector * SECTOR_SIZE,otaInfo.lastSectorSize);
							pc.printf("otaInfo.lastFWSectorSize=%d\r\n",otaInfo.lastSectorSize);
							pc.printf("otaInfo.latestVersionSNFromServer:%s!\r\n",otaInfo.latestVersionSNFromServer);
                //eventHandle.getLatestFWFromServerFlag = false;  // be careful!
            } else {
              sprintf(socketInfo.outBuffer,NOTIFY_REQ_UpdateVersion,otaInfo.latestVersionSNFromServer,otaInfo.currentFWSector * SECTOR_SIZE,SECTOR_SIZE); 
							pc.printf("otaInfo.currentFWSectorSize=%d\r\n",SECTOR_SIZE);
							pc.printf("otaInfo.latestVersionSNFromServer:%s!\r\n",otaInfo.latestVersionSNFromServer);
            }
        } else {//receive all sectors successful, then write version info to flash
            pc.printf("erase reset ota partition!\r\n");
            if(otaInfo.FWSectorNum < CODE_SECTOR_NUM) {
                for(i=otaInfo.FWSectorNum; i<CODE_SECTOR_NUM; i++) {
                    erase_sector(OTA_CODE_START_ADDRESS+i*SECTOR_SIZE);
                }
            }

            eventHandle.getLatestFWFromServerFlag = false;  // be careful!
            crc16 = calculate_crc16((char*) OTA_CODE_START_ADDRESS, otaInfo.FWSizeFromServer);
            pc.printf("code crc: %X  checksum %X\r\n ",crc16,otaInfo.FWCheckSumFromServer);
            if(otaInfo.FWCheckSumFromServer == crc16) {
							if(strncmp("Test",otaInfo.latestVersionFromServer+14,4)!=NULL){
                char* cdata = (char*)VERSION_STR_ADDRESS;
                crc16 = calculate_crc16((char*) OTA_CODE_START_ADDRESS, CODE_SIZE);
                pc.printf("get right checksum of bin code, reset new checksum:%4X\r\n",crc16);
                memset(tempBuffer,0x0,VERSION_STR_LEN);
                tempBuffer[0] = cdata[0];
                tempBuffer[1] = cdata[1];
                tempBuffer[2] = (crc16 >> 8) & 0xff;
                tempBuffer[3] = crc16 & 0xff;
                sprintf(tempBuffer+4,"%s",otaInfo.latestVersionFromServer);
                erase_sector(VERSION_STR_ADDRESS);
                program_flash(VERSION_STR_ADDRESS,tempBuffer, SECTOR_SIZE);
                eventHandle.updateVersionDoneFlag = true;
                otaSuccessFlag = true;								
							}else{
								pc.printf("This version is only for testing flash!no update!\r\n");
							}
            }
						else
							pc.printf("ota checksum error!\r\n");
            return ;
        }
    } else if(msgid == notifyChargerStatus) {
        sprintf(socketInfo.outBuffer,NOTIFY_REQ_ChargerStatus,chargerInfo.status,chargerInfo.connect);
    } else if(msgid == notifyChargingInfo) {
        sprintf(socketInfo.outBuffer,NOTIFY_REQ_ChargingInfo,chargerInfo.chargingType,chargerInfo.energy,chargerInfo.voltage,
			  chargerInfo.current,chargerInfo.power,chargerInfo.duration/60,chargerInfo.status,chargerInfo.connect,chargerInfo.setDuration,chargerInfo.setEnergy);
    } else if(msgid == notifyEndCharging) {
			  sprintf(socketInfo.outBuffer,NOTIFY_REQ_EndCharging,chargerInfo.userId,curChargingEnergy,curChargingDuration/60,chargerInfo.status,chargerInfo.connect,setChargingDuration,setChargingEnergy,chargingEndType);
    } else if(msgid == notifyOTAResult){
				sprintf(socketInfo.outBuffer,NOTIFY_REQ_OTAResult,otaSuccessFlag,otaInfo.oldVersionId,otaInfo.newVersionId);  
		} else {
    }
    len = strlen(socketInfo.outBuffer);
    pc.printf("notify_msg_handle,send %d bytes: %s\r\n",len,socketInfo.outBuffer);
		at_send(AT_DEFAULT_SOCK_ID,socketInfo.outBuffer,len);
    notifySendID = msgid;
    notifySendCounter = systemTimer;
		#ifdef NETWORK_COUNT_ENABLE
		pc.printf("numbers of sended massage:%d\r\n",++networkCount.sendCnt);
		#endif 
}

/*!
	@birfe command respond handler
	@input MsgID
	@output None
*/
void cmd_msg_resp_handle(MSGID msgid)
{
    int len;
    if((msgid == invalidID)||(msgid >= unknownMsgID))
        return;
    memset(socketInfo.outBuffer,0,sizeof(socketInfo.outBuffer));
    if(msgid == setChargingStart) {
			sprintf(socketInfo.outBuffer,CMD_RESP_setChargingStart,respcode,msgIdstr);
    } else if(msgid == setChargingEnd) {  
			if(chargerInfo.status == charging)
				sprintf(socketInfo.outBuffer,CMD_RESP_setChargingEnd,respcode,msgIdstr,chargerInfo.chargingType,chargerInfo.energy,  // ericyang 20161216
				chargerInfo.duration/60,chargerInfo.status,chargerInfo.connect,setChargingDuration,setChargingEnergy);
			else
				sprintf(socketInfo.outBuffer,CMD_RESP_setChargingEnd,respcode,msgIdstr,curChargingType,curChargingEnergy,  // ericyang 20161216
				curChargingDuration/60,chargerInfo.status,chargerInfo.connect,setChargingDuration,setChargingEnergy);
		}else if(msgid == setUpdateVersion){
	    	sprintf(socketInfo.outBuffer,CMD_RESP_setUpdateVersion,respcode,msgIdstr);
		}
		#ifdef RTC_ENABLE
		 else if(msgid == setCalibrateTime){
				sprintf(socketInfo.outBuffer,CMD_RESP_setCalibrateTime,respcode,msgIdstr);
		}else if(msgid == getDate){
				get_time_stamp_ms(timeStampMs);
			  sprintf(socketInfo.outBuffer,CMD_RESP_getDate,respcode,msgIdstr,timeStampMs);
		}
		#endif
		else if(msgid == getChargerStatus) {
        sprintf(socketInfo.outBuffer,CMD_RESP_getChargerStatus,respcode,msgIdstr,chargerInfo.status,chargerInfo.connect);
    } else if(msgid == getChargingInfo) {
        sprintf(socketInfo.outBuffer,CMD_RESP_getChargingInfo,respcode,msgIdstr,chargerInfo.energy,chargerInfo.voltage,
        chargerInfo.current,chargerInfo.power,chargerInfo.duration/60,chargerInfo.status,chargerInfo.connect,chargerInfo.setDuration,chargerInfo.setEnergy);
    }else if(msgid == getCurVersion){
				sprintf(socketInfo.outBuffer,CMD_RESP_getCurVersion,respcode,msgIdstr,VERSION_INFO);
		}
    len = strlen(socketInfo.outBuffer);
		at_send(AT_DEFAULT_SOCK_ID,socketInfo.outBuffer,len);
		pc.printf("cmd_msg_resp_handle,send %d bytes: %s\r\n",len,socketInfo.outBuffer);
		#ifdef NETWORK_COUNT_ENABLE
		pc.printf("numbers of sended massage:%d\r\n",++networkCount.sendCnt);
		#endif 
}

#if defined(EEPROM_ENABLE)
/*!
	@birfe EEPROM test function
	@input None
	@output None
*/
void eepromTest(void)
{
	char data[4];
	data[0] = 0x00;    //byte address
	data[1] = 'b';    //data
	
	pc.printf("begin eeprom test\r\n");
	
	i2c1.write(EEPROM_ADDRESS_WRITE,data,2);
	pc.printf("write data:%c to eeprom\r\n",data[1]);
	
	wait(0.01);
	
	//read operation
	i2c1.write(EEPROM_ADDRESS_WRITE,data,1);
	i2c1.read(EEPROM_ADDRESS_READ,data,1);
	
	pc.printf("data read from eeprom:%c\r\n",data[0]);	
}
/*!
	@birfe Write data to EEPROM
	@input start address:0~255
				 pointer to data expected to be writed
				 sizes of data to be writed
	@output None
*/
uint8_t write_to_eeprom(uint8_t addr,char* data,uint8_t size)
{
	uint8_t ret = 0;
	uint16_t crc1,crc2;
	uint8_t pageNum,firstPageSize,lastPageSize;
	uint8_t ptr = 0;   //point to position of data to be writen
	
	if(addr + size > 256)
	{
		pc.printf("not enough storage!\r\n");
		return ret;
	}
	
	if(addr%EEPROM_PAGE_SIZE == 0){
		firstPageSize = EEPROM_PAGE_SIZE;
  }else{
		firstPageSize = EEPROM_PAGE_SIZE - addr%EEPROM_PAGE_SIZE;
	}
	
	if(size <= firstPageSize){
		pageNum = 1;
    lastPageSize = firstPageSize;		
	}
	else{
		if((size-firstPageSize)%EEPROM_PAGE_SIZE == 0){
			pageNum = (size-firstPageSize)/EEPROM_PAGE_SIZE + 1;
			lastPageSize = EEPROM_PAGE_SIZE;
		}else{
			pageNum = (size-firstPageSize)/EEPROM_PAGE_SIZE + 2;
			lastPageSize = (size-firstPageSize)%EEPROM_PAGE_SIZE;
		}
	}

	/* write data to first page */
	dataBuffer[0] = addr;
	for(int i=0;i<firstPageSize;i++,ptr++)
		dataBuffer[i+1] = data[ptr];
  i2c1.write(EEPROM_ADDRESS_WRITE,dataBuffer,firstPageSize+1);
	wait(0.003);
	
	for(int i=0;i<pageNum-2;i++){
		dataBuffer[0] = addr + firstPageSize + i * EEPROM_PAGE_SIZE;

		for(int j=0;j<EEPROM_PAGE_SIZE;j++,ptr++)
		  dataBuffer[j+1] = data[ptr];
		i2c1.write(EEPROM_ADDRESS_WRITE,dataBuffer,EEPROM_PAGE_SIZE+1);
		wait(0.003);
	}
	/* write data to last page */
  if(pageNum >1){
		dataBuffer[0] = addr + firstPageSize + (pageNum-2) * EEPROM_PAGE_SIZE;

		for(int i=0;i<lastPageSize;i++,ptr++)
			dataBuffer[i+1] = data[ptr];
		i2c1.write(EEPROM_ADDRESS_WRITE,dataBuffer,lastPageSize+1);
	}
	
  crc1 = calculate_crc16(data,size);
	wait(0.003);

	/* read back the data and verify */
	dataBuffer[0] = addr;
	i2c1.write(EEPROM_ADDRESS_WRITE,dataBuffer,1);
	i2c1.read(EEPROM_ADDRESS_READ,dataBuffer,size);
	crc2 = calculate_crc16(dataBuffer,size);
	if(crc1 == crc2)
	{
		ret = 1;
	}else{
	}
	return ret;
}
/*!
	@birfe Read data from EEPROM
	@input start address:0~255
				 pointer to data expected to be writed
				 sizes of data to be writed
	@output None
*/
void read_from_eeprom(uint8_t addr,char* data,uint8_t size)
{
	char tmp = addr;
	i2c1.write(EEPROM_ADDRESS_WRITE,&tmp,1);
	i2c1.read(EEPROM_ADDRESS_READ,data,size);
}

/*!
	@birfe Save chargerInfo to EEPROM
	@input None
	@output None
*/
void save_charger_info_to_eeprom(void)
{
	uint16_t crc16;
	crc16 = calculate_crc16((char*)(&chargerInfo),sizeof(chargerInfo));
	tempBuffer[0] = crc16 >> 8;
	tempBuffer[1] = crc16 & 0xFF;
	memcpy(tempBuffer+2,(char*)(&chargerInfo),sizeof(chargerInfo));
	if(write_to_eeprom((uint8_t)CHARGER_INFO_ADDRESS,tempBuffer,sizeof(chargerInfo)+2))
		pc.printf("save chargerInfo successfully\r\n");
	else
		pc.printf("fail to save chargerInfo\r\n");
}
/*!
	@birfe Set default chargerInfo to EEPROM
	@input None
	@output None
*/
void set_default_charger_info_to_eeprom(void)
{
	memset(chargerInfo.meterNumber,0x0, sizeof(chargerInfo.meterNumber));
  chargerInfo.energy = 0.0;
  chargerInfo.voltage = 0.0 ;
  chargerInfo.current = 0.000 ;
  chargerInfo.power = 0.0 ;
  chargerInfo.setDuration = 0;
	chargerInfo.setEnergy = 0.0;
  chargerInfo.duration = 0;
  chargerInfo.status = idle;//charging;//0;
  chargerInfo.connect = NOT_CONNECTED;//true;//false;
	chargerInfo.userId = 0;
	chargerInfo.chargingType = -1;
  save_charger_info_to_eeprom();
}
/*!
	@birfe Load chargerInfo from EEPROM
	@input None
	@output None
*/
void load_charger_info_from_eeprom(void)
{
	uint16_t crc16,checksum;
	char* cdata = (char*)malloc(sizeof(chargerInfo)+2);
	if(NULL == cdata){
		pc.printf("not enough memory!\r\n");
		return;
	}
	read_from_eeprom((uint8_t)CHARGER_INFO_ADDRESS,cdata,sizeof(chargerInfo)+2);
	crc16 = calculate_crc16(cdata+2,sizeof(chargerInfo));
	checksum = (cdata[0]<<8) | cdata[1];
	if(crc16 == checksum){
		pc.printf(" chargerinfo check ok! reload parm to ram!\r\n");
    memcpy(&chargerInfo, cdata+2, sizeof(ChargerInfo));
    pc.printf("meterNumber:%s\r\n",chargerInfo.meterNumber);
	}else{
		pc.printf("eeprom param check fail, set default info to eeprom now!\r\n");
    set_default_charger_info_to_eeprom();
    return;
	}
	pc.printf("energy:%0.1f\r\n",chargerInfo.energy);
  pc.printf("voltage:%0.2f\r\n",chargerInfo.voltage);
  pc.printf("current:%0.3f\r\n",chargerInfo.current);
  pc.printf("power:%0.3f\r\n",chargerInfo.power);
  pc.printf("setDuration:%d\r\n",chargerInfo.setDuration);
  pc.printf("duration:%ds\r\n",chargerInfo.duration);
  pc.printf("status:%d\r\n",chargerInfo.status);
  pc.printf("connect:%d\r\n",chargerInfo.connect);
	pc.printf("setEnergy:%0.1f\r\n",chargerInfo.setEnergy);
	pc.printf("type:%d\r\n",chargerInfo.chargingType);
	pc.printf("userId:%d\r\n",chargerInfo.userId);
}
#else
/*!
	@birfe Set default chargerInfo to flash
	@input None
	@output None
*/
void set_default_charger_info_to_flash(void)
{
    memset(chargerInfo.meterNumber,0x0, sizeof(chargerInfo.meterNumber));
    chargerInfo.energy = 0.0;
    chargerInfo.voltage = 0.0 ;
    chargerInfo.current = 0.000 ;
    chargerInfo.power = 0.0 ;
    chargerInfo.setDuration = 0;
    chargerInfo.duration = 0;
    chargerInfo.status = idle;
    chargerInfo.connect = NOT_CONNECTED;
	  chargerInfo.setEnergy = 0;
	  chargerInfo.chargingType = CHARGING_TYPE_NONE;
	  chargerInfo.userId = 0;
    save_charger_info_to_flash();
}
/*!
	@birfe Load chargerInfo from flash
	@input None
	@output None
*/
void load_charger_info_from_flash(void)
{
    char *cdata = (char*)PARAM_START_ADDRESS;
    int crc16,checksum;

    checksum = (cdata[0]<<8)|cdata[1];
    crc16 = calculate_crc16(cdata+2, sizeof(ChargerInfo));
    if(checksum == crc16) {
        pc.printf("flash param check ok! reload parm to ram!\r\n");
        memcpy(&chargerInfo, cdata+2, sizeof(ChargerInfo));
        pc.printf("meterNumber:%s\r\n",chargerInfo.meterNumber);
    } else {
        pc.printf("flash param check fail, set default info to flash now!\r\n");
        set_default_charger_info_to_flash();
        return;
    }
    pc.printf("energy:%0.1f\r\n",chargerInfo.energy);
    pc.printf("voltage:%0.2f\r\n",chargerInfo.voltage);
    pc.printf("current:%0.3f\r\n",chargerInfo.current);
    pc.printf("power:%0.3f\r\n",chargerInfo.power);
    pc.printf("setDuration:%d\r\n",chargerInfo.setDuration);
    pc.printf("duration:%ds\r\n",chargerInfo.duration);
    pc.printf("status:%d\r\n",chargerInfo.status);
    pc.printf("connect:%d\r\n",chargerInfo.connect);
		pc.printf("setEnergy:%0.1f\r\n",chargerInfo.setEnergy);
		pc.printf("type:%d\r\n",chargerInfo.chargingType);
		pc.printf("userId:%d\r\n",chargerInfo.userId);
}
/*!
	@birfe Save chargerInfo to flash
	@input None
	@output None
*/
void save_charger_info_to_flash(void)
{
    int crc16;
    crc16 = calculate_crc16((char*)&chargerInfo, sizeof(ChargerInfo));
    tempBuffer[0] = crc16 >> 8;
    tempBuffer[1] = crc16 & 0xff;
    memcpy(tempBuffer+2,(char*)&chargerInfo,sizeof(ChargerInfo));
    erase_sector(PARAM_START_ADDRESS);
    program_flash(PARAM_START_ADDRESS, tempBuffer, sizeof(ChargerInfo)+2);
}
#endif //end of defined(EEPROM_ENABLE)

/*!
	@birfe Get charger exception status
	@input None
	@output charger exception status
*/
int get_charger_exception(void)
{
    int ret = 0 ;
    ret =   METER_CRASHED_FLAG(chargerException.meterCrashedFlag ? 1 : 0)
            |CHARGING_VOLTAGE_ERROR_FLAG(chargerException.chargingVoltageErrorFlag ? 1 : 0)
            |CHARGING_CURRENT_ERROR_FLAG(chargerException.chargingCurrentErrorFlag ? 1 : 0)
            |CHARGING_DISABLE_FLAG(chargerException.chargingEnableFlag ? 0 : 1)
            |CHARGING_TIME_OVER_FLAG(chargerException.chargingTimeOverFlag ? 1 : 0)
						|CHARGING_ENERGY_OVER_FLAG(chargerException.chargingEnergyOverFlag ? 1: 0);
    return ret;
}
/*!
	@birfe Charger exception handler,update charger status and handle exception while charging
	@input None
	@output None
*/
void charging_exception_handle(void){
	static int chargerExceptionStatus = 0;
	if(chargerExceptionStatus != get_charger_exception()){ //charger exception status change
		chargerExceptionStatus = get_charger_exception();
		if(chargerExceptionStatus != 0){
			if(chargerInfo.status == charging){ //chargerExceptionStatus != 0 && chargerInfo.status == charging
				stop_charging();
	      disable_cp_pwm();
				chargerException.chargingTimeOverFlag = false;
				chargerException.chargingEnergyOverFlag = false;

				if(chargerExceptionStatus == CHARGING_TIME_OVER_FLAG(1)){ //over time
					chargerInfo.status = errorCode | chargerExceptionStatus ;
					init_charger_info();
					eventHandle.stopChargingFlag = true;
					chargingEndType = END_OVER_TIME;
					pc.printf("overtime,stop charging!\r\n");
				}else if(chargerExceptionStatus == CHARGING_ENERGY_OVER_FLAG(1)){//over energy
					chargerInfo.status = errorCode | chargerExceptionStatus;
					init_charger_info();
					eventHandle.stopChargingFlag = true;
					chargingEndType = END_OVER_ENERGY;
					pc.printf("over energy,stop charging!\r\n");
				}else{
					chargerInfo.status = errorCode | chargerExceptionStatus | charging; //status:exception happenned while charging
					chargingExceptionFlag = true;  //set the charging exception flag
				}
				
			}else{  //chargerExceptionStatus != 0 && chargerInfo.status != charging
				if((chargerInfo.status & CHARGER_STATUS_MASK) == charging)
					chargerInfo.status = errorCode | chargerExceptionStatus | charging;
				else if(chargerExceptionStatus == CHARGING_DISABLE_FLAG(1))
					chargerInfo.status = idle;
				else
					chargerInfo.status = errorCode | chargerExceptionStatus;
			}
		}else{ //chargerExceptionStatus == 0
			chargerInfo.status = connected;
		}
//		pc.printf("chargerInfo.status = %x \r\n",chargerInfo.status );
		eventHandle.updateChargerStatusFlag = true;  //update chargerInfo
		#if defined(EEPROM_ENABLE)
		save_charger_info_to_eeprom();
		#else
		save_charger_info_to_flash();
		#endif
	} //end of charger exception status change

/* Network exception handle */
	if(chargerException.serverConnectedFlag == false && chargerInfo.status == charging){
		stop_charging();
		init_charger_info();

    disable_cp_pwm();
    chargerException.chargingTimeOverFlag = false;
		chargerException.chargingEnergyOverFlag = false;
    chargerInfo.status = connected;
    eventHandle.stopChargingFlag = true;  //notify End charging msg when reconnect to server
		chargingEndType = END_OFFLINE;
		#if defined(EEPROM_ENABLE)
		save_charger_info_to_eeprom();
		#else
    save_charger_info_to_flash();
		#endif
	}
	if(startContinueChargingFlag == true){//continue charging
		pre_start_charging();
	  pc.printf("Continue charging!\r\n");
	}
	
	#ifdef LED_INFO_ENABLE
	if((chargerException.meterCrashedFlag == true)||
		(chargerException.chargingCurrentErrorFlag == true) ||
		(chargerException.chargingVoltageErrorFlag == true))
	{
			WARNING_LED_ON;
	}
	else
	{
			WARNING_LED_OFF;
	}
	#endif
}