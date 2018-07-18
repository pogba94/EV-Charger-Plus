#ifndef USERCONFIG_H
#define USERCONFIG_H

/*----------------------------------------------------------------------------
 * Includes
-----------------------------------------------------------------------------*/
#include "mbed.h"
#include "rtos.h" 
#include "rtc_api.h" 

#include "EthernetInterface.h"
#include "flashLayout.h"


/*----------------------------------------------------------------------------
 * Macro definition
-----------------------------------------------------------------------------*/
/* Exception handler parameters config */

#define MAX_EXCEPTION_TIME_ALLOWED       30U
#define NORMAL_TIME_HOLDED               10U
#define RECHARGING_WAIT_TIMES_ALLOWED    3U

#define MAX_PAUSE_TIME                   180U  //the max paused time allowed while charging,unit:s
#define MAX_WAIT_TIME                    3600U //the max wait time for s2 switch on,unit:s           

/* Charging power configure,3.5KW or 7 KW */
#define CHARGING_CURRENT_16A

#define PWM_DUTY_CURRENT_16A              (0.266667)
#define PWM_DUTY_CURRENT_32A              (0.53333)

#ifdef CHARGING_CURRENT_32A
	#undef CHARGING_CURRENT_16A 
#endif

#ifdef CHARGING_CURRENT_16A
  #undef CHARGING_CURRENT_32A
#endif

/* Function configure */

#define LED_INFO_ENABLE  
//#define RTC_ENABLE       
//#define WDOG_ENABLE      
//#define EEPROM_ENABLE
#ifdef WDOG_ENABLE
	#define  WDOG_TIMEOUT_VALUE_MS          20000 //define timeout value of watchdog  
#endif

#define DISABLE_NETWORK_CONMUNICATION_FUNC 

#define ENABLE_FULL_CHARGING_DETECT
#ifdef  ENABLE_FULL_CHARGING_DETECT
	#define  FULL_CHARGING_DETECT_TIME      60 //unit:s
#endif	

/* Config size of socket buffer  */
#define SOCKET_OUT_BUFFER_SIZE         256
#define SOCKET_IN_BUFFER_SIZE         (1024)
#define SOCKET_RESEND_TIME             10   // Unit:s

/*----------------------------------------------------------------------------
 * Type definition
-----------------------------------------------------------------------------*/
typedef struct _EventFlag {
    bool heatbeatFlag;
    bool checkMeterFlag;
    bool updateChargerInfoFlag;
    bool updateChargerStatusFlag;
	  bool updateVersionDoneFlag;
    bool updataVersionFailFlag;
    bool stopChargingFlag;
    bool getLatestFWFromServerFlag;
    #ifdef DISABLE_NETWORK_CONMUNICATION_FUNC
    bool stopCommunicationFlag; //stop communication between sever and client for 1 min?
    #endif
	  bool firstConnectFlag;
} SystemEventHandle;

typedef struct _SocketInfo {
    char inBuffer[SOCKET_IN_BUFFER_SIZE];
    char outBuffer[SOCKET_OUT_BUFFER_SIZE];
} SocketInfo;

typedef enum _cpSignal {
    PWMNone = 0,
    PWM12V = 1,
    PWM9V = 2,
    PWM6V = 3,
		PWM0V = 4,
		PWMOtherVoltage = 5,
} CPSignal;

typedef enum {
	EV_IDLE = 1,
	EV_CONNECTED_PRE_START_CHARGING,
	EV_CONNECTED_ON_CHARGING,
	EV_CONNECTED_PRE_STOP_CHARGING,
}EVChargingState;

/*----------------------------------------------------------------------------
 * Declaration
-----------------------------------------------------------------------------*/
extern Serial pc;
#ifdef EEPROM_ENABLE
extern I2C i2c1;
#endif 
extern char EVChargerModelNO[];
extern DigitalOut switchRelayOnoff;
extern Mutex myMutex;
extern int systemTimer;
extern SystemEventHandle eventHandle;
extern SocketInfo socketInfo;
extern float startEnergyReadFromMeter;
extern float totalEnergyReadFromMeter;
extern bool pwmState;
void disable_cp_pwm(void);
void enable_cp_pwm(float duty);
int get_meter_info(void);
void get_time_stamp_ms(char* timeStampMs);
#ifdef RTC_ENABLE
void rtc_init(uint32_t timeSeconds);
#define RTC_INIT_TIME          (uint32_t)(3600*24*(365*47+30*10))
#endif
extern char tempBuffer[];
extern char dataBuffer[];

extern DigitalOut red;       
extern DigitalOut green;    
extern DigitalOut blue;   

#endif
