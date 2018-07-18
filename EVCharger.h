#ifndef EVCHARGER_H
#define EVCHARGER_H

/*----------------------------------------------------------------------------
 * Includes
-----------------------------------------------------------------------------*/
#include "UserConfig.h"
/*----------------------------------------------------------------------------
 * Type definition
-----------------------------------------------------------------------------*/
typedef enum {
    RESP_OK = 100, 
	  RESP_PARAM_ERROR = 101,
	  RESP_TIMEOUT = 102,
    RESP_ILLEGAL = 103,
		RESP_ITEM_ERROR = 104,
} RespCode;

typedef enum {
    invalidID = 0,
    setChargingStart,   
    setChargingEnd,
	  setUpdateVersion,
	  setCalibrateTime,
    getChargerStatus,
    getChargingInfo,
	  getCurVersion, 
	  getDate,  
    notifyNewDevice,
    notifyChargerStatus,
    notifyChargingInfo,
    notifyEndCharging,
    notifyOTAResult,
	  notifyUpdateVersion,
    unknownMsgID
} MSGID;

typedef enum {
    invalidType,
    req,
    resp,
} MSGType;

typedef enum {
    offline = 0,
    connected = 1,
    charging = 2,
    booked =3,
	  idle = 4,
    errorCode = 0x4000,
} ChargerStatus;


typedef enum{
	END_OVER_ENERGY = 0,
	END_OVER_TIME = 1,
	END_FULL_CHARGING = 2,
	END_IN_ADVANCE = 10,
	END_IN_ADVANCE_FULL = 11,
	END_WAIT_TIMEOUT = 12,
	END_OFFLINE = 20,
	END_CONNECT_EXCEPTION = 21,
	END_METER_EXCEPTION = 22,
	END_CURRENT_ERROR = 23,
	END_VOLTAGE_ERROR = 24,
	END_NONE = -1,
}ChargingEndTypes;

typedef struct {
    char  meterNumber[12];  //"15500544" 
    float energy; // unit: KWH
    float voltage;  // unit: V
    float current;  // unit: A
    float power;    // unit: W
    int   setDuration; //total charging time set by Wechat UI
    int   duration; // current total charging time
    int   status; //0->offline 1->connected 2-> charging 3-> booked 4->idle and other
    int  	connect; //0-> disconnect 1-> connect but not enable charging 2-> connect and charging enabled
	  int  	userId;
	  int  	chargingType;//20170719,type=0--> money,type=1--> time, type=2--> full
		float setEnergy; //20170719,unit:kwh,format:%0.1f
} ChargerInfo;


typedef struct{
    bool serverConnectedFlag;  // device can't connect to server
    bool meterCrashedFlag;    // meter can't read out voltage/current/energy
    bool chargingVoltageErrorFlag;  // voltage: 176~264V
    bool chargingCurrentErrorFlag;  // current: 0 ~ 16A
    bool chargingTimeOverFlag; 
    bool chargingEnableFlag;  // Auto enable charging,  PWM 6V  detected 
	  bool chargingEnergyOverFlag; //20170719
} ChargerException;

typedef struct _OTAInfo {
    char latestVersionFromServer[VERSION_STR_LEN];
	  char latestVersionSNFromServer[VERSION_SN_LEN];
    int currentFWSector;
    int FWSectorNum;
    int lastSectorSize;
    int FWSizeFromServer;
    int FWCheckSumFromServer;
	  int oldVersionId;
    int newVersionId; 
} OTAInfo;

typedef enum _connectStatus{
    NOT_CONNECTED = 0,
    CONNECTED_9V = 1,
    CONNECTED_6V = 2,    
}connectStatus;

typedef enum _chargingTypes{
   CHARGING_TYPE_MONEY = 0,
	 CHARGING_TYPE_TIME = 1,
	 CHARGING_TYPE_FULL = 2,
	 CHARGING_TYPE_NONE = -1,
}chargingTypes;

#ifdef NETWORK_COUNT_ENABLE
typedef struct _networkCount{
	uint32_t sendCnt;   //numbers of massages sended
	uint32_t recCnt;    //numbers of massages recieved
	uint32_t reSendCnt; //numbers of massages resended
}NetworkCount;
#endif

/*----------------------------------------------------------------------------
 * Declaration
-----------------------------------------------------------------------------*/
extern ChargerInfo chargerInfo;
extern ChargerException chargerException;
extern OTAInfo otaInfo;
extern MSGID notifySendID;
extern int notifySendCounter;
extern int chargingEndType;

extern int volatile gChargingState;
extern volatile bool startS2SwitchOnOffCounterFlag;
extern volatile int S2SwitchOnOffCounter;
extern volatile bool pauseChargingFlag;
extern volatile int pauseChargingCounter;

extern volatile bool chargingExceptionFlag;  //the flag would be set when exception happened while charging
extern volatile bool rechargingFlag;    // the flag would be set when exception disappeared while charging
extern volatile bool startContinueChargingFlag;//when the flag is set,the charger restart to charge
extern volatile uint32_t chargingErrorTimer;
extern volatile uint32_t rechargingTimer;
extern volatile uint8_t rechargingWaitCounter;

extern volatile bool checkChargingFinishFlag;
extern volatile bool updateCodeFlag;
extern volatile bool otaSuccessFlag;
extern volatile bool startS2SwitchWaitCounterFlag;
extern volatile int waitS2SwitchOnCounter;
#ifdef RTC_ENABLE
	extern char timeStampMs[32];
#endif
#ifdef NETWORK_COUNT_ENABLE
	extern volatile NetworkCount networkCount;
#endif

void parse_recv_msg(char *text);
void parse_bincode(char *text);
void init_charger(void);
void notify_msg_handle(MSGID msgid);
void cmd_msg_resp_handle(MSGID msgid);
void start_charging(void);
void stop_charging(void);
void init_charger_info(void);
void charging_exception_handle(void);
int get_charging_end_type(int chargerStatus);

#if defined(EEPROM_ENABLE)
void save_charger_info_to_eeprom(void);
void load_charger_info_from_eeprom(void);
uint8_t write_to_eeprom(uint8_t addr,char* data,uint8_t size);
void read_from_eeprom(uint8_t addr,char* data,uint8_t size);
#else
void load_charger_info_from_flash(void);
void save_charger_info_to_flash(void);
#endif
void pre_start_charging(void);
void pre_stop_charging(void);
void pause_charging(bool onoff);

/*----------------------------------------------------------------------------
 * Macro definiton
-----------------------------------------------------------------------------*/
#ifdef LED_INFO_ENABLE
#define WARNING_LED_ON 		(red = 1)
#define WARNING_LED_OFF 	(red = 0)
#define CHARGING_LED_ON 	(green = 1)
#define CHARGING_LED_OFF 	(green = 0)
#define CONNECT_LED_ON 		(blue = 1)
#define CONNECT_LED_OFF 	(blue = 0)
#define SERVER_CONNECT_LED_ON  (server = 1)   
#define SERVER_CONNECT_LED_OFF (server = 0)		
#endif

#define RELAY_ON	1  
#define RELAY_OFF 0		

#define CHARGER_STATUS_MASK                 0x03   //2 LSBs
#define METER_CRASHED_FLAG(x)              ((x) << 4)
#define CHARGING_VOLTAGE_ERROR_FLAG(x)     ((x) << 5)
#define CHARGING_CURRENT_ERROR_FLAG(x)     ((x) << 6)
#define CHARGING_DISABLE_FLAG(x)           ((x) << 7)
#define CHARGING_TIME_OVER_FLAG(x)         ((x) << 8)
#define CHARGING_ENERGY_OVER_FLAG(x)       ((x) << 9)

#define SOCKET_OTA_HEADER  "OTABIN"

#ifdef  EEPROM_ENABLE
	#define  EEPROM_ADDRESS_WRITE      0xA0         
	#define  EEPROM_ADDRESS_READ       0xA1
	#define  EEPROM_PAGE_SIZE           16
#endif

#ifdef RTC_ENABLE
	#define SETCalibrateTime        "setCalibrateTime"
  #define GETDate                 "getDate"    
#endif
#define SETChargingStart        "setChargingStart"  
#define SETChargingEnd          "setChargingEnd"     
#define SETUpdateVersion        "setUpdateVersion"  
#define GETChargerStatus        "getChargerStatus"
#define GETChargingInfo         "getChargingInfo"
#define GETCurVersion           "getCurVersion"      

#define NOTIFYNewDevice         "notifyNewDevice"
#define NOTIFYChargerStatus     "notifyChargerStatus"
#define NOTIFYChargingInfo      "notifyChargingInfo"
#define NOTIFYEndCharging       "notifyEndCharging"
#define NOTIFYUpdateVersion     "notifyUpdateVersion"
#define NOTIFYOTAResult         "notifyOTAResult"       
#define NOTIFYHeartPackage      "notifyHeartPackage"   

#define CMD_RESP_setChargingStart   "{\"respType\":\"setChargingStart\",\"data\":{\"respCode\":%d,\"msgId\":\"%s\"}}"
#define CMD_RESP_setChargingEnd     "{\"respType\":\"setChargingEnd\",\"data\":{\"respCode\":%d,\"msgId\":\"%s\",\"type\":%d,\"energy\":%0.1f,\"duration\":%d,\"status\":%d,\"connect\":%d,\"setDuration\":%d,\"setEnergy\":%0.1f}}" 
#ifdef RTC_ENABLE
	#define CMD_RESP_setCalibrateTime   "{\"respType\":\"setCalibrateTime\",\"data\":{\"respCode\":%d,\"msgId\":\"%s\"}}"
	#define CMD_RESP_getDate            "{\"respType\":\"getDate\",\"data\":{\"respCode\":%d,\"msgId\":\"%s\",\"date\":\"%s\"}}"
#endif
#define CMD_RESP_setUpdateVersion   "{\"respType\":\"setUpdateVersion\",\"data\":{\"respCode\":%d,\"msgId\":\"%s\"}}"  
#define CMD_RESP_getChargerStatus   "{\"respType\":\"getChargerStatus\",\"data\":{\"respCode\":%d,\"msgId\":\"%s\",\"status\":%d,\"connect\":%d}}"
#define CMD_RESP_getChargingInfo    "{\"respType\":\"getChargingInfo\",\"data\":{\"respCode\":%d,\"msgId\":\"%s\",\"energy\":%0.1f,\"voltage\":%0.2f,\"current\":%0.2f,\"power\":%0.2f,\"duration\":%d,\"status\":%d,\"connect\":%d,\"setDuration\":%d,\"setEnergy\":%0.1f}}"
#define CMD_RESP_getCurVersion      "{\"respType\":\"getCurVersion\",\"data\":{\"respCode\":%d,\"msgId\":\"%s\",\"versionNumber\":\"%s\"}}"
#define NOTIFY_REQ_NewDevice        "{\"reqType\":\"notifyNewDevice\",\"data\":{\"mac\":\"%s\",\"isReconnect\":%d}}"
#define NOTIFY_REQ_ChargerStatus    "{\"reqType\":\"notifyChargerStatus\",\"data\":{\"status\":%d,\"connect\":%d}}"
#define NOTIFY_REQ_ChargingInfo     "{\"reqType\":\"notifyChargingInfo\",\"data\":{\"type\":%d,\"energy\":%0.1f,\"voltage\":%0.2f,\"current\":%0.3f,\"power\":%0.1f,\"duration\":%d,\"status\":%d,\"connect\":%d,\"setDuration\":%d,\"setEnergy\":%0.1f}}"
#define NOTIFY_REQ_EndCharging      "{\"reqType\":\"notifyEndCharging\",\"data\":{\"userId\":%d,\"energy\":%0.1f,\"duration\":%d,\"status\":%d,\"connect\":%d,\"setDuration\":%d,\"setEnergy\":%0.1f,\"endType\":%d}}"
#define NOTIFY_REQ_UpdateVersion    "{\"reqType\":\"notifyUpdateVersion\",\"data\":{\"versionSN\":\"%s\",\"blockOffset\":%d,\"blockSize\":%d}}"
#define NOTIFY_REQ_OTAResult        "{\"reqType\":\"notifyOTAResult\",\"data\":{\"result\":%d,\"oldVersionId\":%d,\"newVersionId\":%d}}"
#define NOTIFY_REQ_HeartPackage     "{\"reqType\":\"notifyHeartPackage\",\"data\":{\"ledStatus\":%d}}" 

#endif
