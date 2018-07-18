/*****************************************************************
* Describtion:            Firmware for AC EV Charger
* Company:                WPI-ATU
* Author:                 Orange
* Version:                V1.0
* Date:                   2018-03-16
* Function List:
*		  1. rs485 Communication
*	  	2. Ethernet Communication,RJ45 interface
*     3. Wechat control
*		  4. Follow the GB/T 18487.1-2015 Standard
*		  5. Use Mbed OS 2.0
* History:
*
******************************************************************/

/*****************************************************************
* @FileName      : main.c
* @Describtion   : 
* @Arthor        : Orange Cai
* @Date          : 2018-03-30
* @History       : Initial
*****************************************************************/

#include "mbed.h"
#include "me3612.h"
#include "string.h"
#include "cJSON.h"
#include "lib_crc16.h"
#include "EVCharger.h"
#include "UserConfig.h"
#include "ota.h"

#ifdef EEPROM_ENABLE
extern "C"{
	#include "MK64F12.h"
}
#endif

#define  NB_UART_BUF_SIZE     (512)

#if 0
#define  SERVER_IP_ADDRESS     "112.74.170.197"
#define  SERVER_PORT           "55555"
#else
#define  SERVER_IP_ADDRESS     "219.144.130.27"
#define  SERVER_PORT           "8885"
#endif

#define  DEFAULT_MODEL_NO      "12345678abcd"

Serial nb(PTB11,PTB10);    //at command uart
Serial pc(USBTX, USBRX);   //dubug uart
Serial rs485(PTE24,PTE25);  //uart for rs485
DigitalOut switchRelayOnoff(PTB23);//PTB23
AnalogIn cpADC(A0);  // adc for cp detect PTB2
PwmOut cpPWM(D6);  // cp pwm signal generator PTC2
Timer timer;

/* LED for indicate device status */
DigitalOut red(LED_RED);        //warning led
DigitalOut green(LED_GREEN);    //charging led
DigitalOut blue(LED_BLUE);    //connect led
DigitalOut server(PTB20);    //server connected led

#ifdef EEPROM_ENABLE
I2C i2c1(PTC11,PTC10);    //orangecai I2C1 Read Epprom data
#endif

SocketInfo socketInfo;
SystemEventHandle eventHandle;
int systemTimer = 0;
int resetTimer = 0;
float startEnergyReadFromMeter = 0;
float totalEnergyReadFromMeter = 0;
bool pwmState = false;

volatile bool checkChargingFinishFlag = false;
volatile bool pauseChargingFlag = false;
volatile bool startS2SwitchWaitCounterFlag = false;
volatile int pauseChargingCounter = 0;
volatile int waitS2SwitchOnCounter = 0;

volatile bool S2SwitchEnabledFlag = false;
volatile bool startS2SwitchOnOffCounterFlag = false;
volatile int S2SwitchOnOffCounter = 0;
volatile int gChargingState = EV_IDLE;

/*the flag would be set when exception happened while charging*/
volatile bool chargingExceptionFlag = false;
 /*the flag would be set when exception disappeared while charging*/
volatile bool rechargingFlag = false;
volatile bool startContinueChargingFlag = false; 
volatile uint32_t chargingErrorTimer = 0;
volatile uint32_t rechargingTimer = 0;
volatile uint8_t rechargingWaitCounter = 0;

 /*Update code while the flag is set */
volatile bool updateCodeFlag = false;
 /* true:OTA code download success */
volatile bool otaSuccessFlag = false;

#ifdef ENABLE_FULL_CHARGING_DETECT
	volatile uint16_t fullChargingDetectCounter = 0;
	volatile bool fullChargingFlag = false;
#endif

char EVChargerModelNO[20];
char tempBuffer[256];
char dataBuffer[256];
char nb_uart_buf[NB_UART_BUF_SIZE];
char device_ip[20];
TCP_RECV_DATA_T recvData = {0,"",0,0,"",0};
volatile uint16_t pBufNext = 0;
Semaphore at_parse_sema;

char* at_baudrate[] = {"300","600","1200","2400","4800","9600",
											"19200","38400","57600","115200","230400","921600",
											"2000000","2900000","3000000","3200000","3686400","4000000"};

AT_SOCK_T sock = {1,sock_close};											
bool checkSocketFlag = false;

#ifdef EEPROM_ENABLE
/*!
	@birfe Config pins for I2C1
	@input None
	@output None
*/
static void pinConfig(void)
{
	/* config I2C1 SCL pin , PTC10 */
	PORTC->PCR[10] |= (uint32_t)0x03; 
	/* config I2C1 SDA pin , PTC11 */
	PORTC->PCR[11] |= (uint32_t)0x03;
}
#endif											

/*******************************************************************
* @FunctionName  : get_meter_info()
* @Describtion   : get meter info including voltage,current,energy and power
* @Input         : none
* @Output        : 
									 0 if read meter successfully
                   -1 if failed to read meter
*******************************************************************/
int get_meter_info(void)
{
	  /*slave addr,function code,Hi PDU addr,low PDU addr,Hi N reg,Lo N reg,Lo CRC,Hi CRC*/
    char ModbusData[8] = {0x2C,0x03,0x00,0x00,0x00,0x07};
    char regvalue[20] = {0};
    uint16_t crc16;
    int i,count;
    static int meterErrorTimes = 0;
		static int voltageErrorTimes = 0;
		static int currentErrorTimes = 0;
		
    ModbusData[0] = atoi(chargerInfo.meterNumber + 6); // get last two number [6][7] as meter addr
//    pc.printf("meter address: %x\r\n",ModbusData[0]);

    crc16 = calculate_crc16_Modbus(ModbusData, 6);
    ModbusData[6] = crc16 & 0xff;
    ModbusData[7] = (crc16 >> 8) & 0xff;

    //myMutex.lock();
    for(i = 0; i < 8; i++)
        rs485.putc(ModbusData[i]);

    i = 0;
    count = 0;

    while((i < 19) && (count < 50)) {

        if(rs485.readable()) {
            count = 0;
            regvalue[i++] = rs485.getc(); // Retrieving received register from modbus
        } else {
					  wait_ms(1);
            count++;
        }
    }
    //myMutex.unlock();
    crc16 = calculate_crc16_Modbus(regvalue,17);
    if(( (crc16 & 0xff) == regvalue[17]) && (((crc16 >> 8) & 0xff) == regvalue[18])) {
        meterErrorTimes = 0;
        if(chargerException.meterCrashedFlag == true) {
            chargerException.meterCrashedFlag = false; // ericyang 20160824 meter recover ok
        }

        totalEnergyReadFromMeter = (regvalue[5]<<24 | regvalue[6]<<16 | regvalue[3]<<8 | regvalue[4])*0.1;
        chargerInfo.energy =  totalEnergyReadFromMeter - startEnergyReadFromMeter;
        chargerInfo.voltage = (regvalue[7]<<8 | regvalue[8])*0.01;
        chargerInfo.current = (regvalue[11]<<24 | regvalue[12]<<16 | regvalue[9]<<8 | regvalue[10])*0.001;
        chargerInfo.power = ((regvalue[15]&0x7F)<<24 | regvalue[16]<<16 | regvalue[13]<<8 | regvalue[14])*0.1;

        if((chargerInfo.voltage > 264) || (chargerInfo.voltage < 176)) {  // voltage: 176~264
						if((chargerException.chargingVoltageErrorFlag == false)&&(voltageErrorTimes++ >= 5)) {
                chargerException.chargingVoltageErrorFlag = true;
            }
            pc.printf("\r\n Voltage Error!!!\r\n"); 
        } else {
						voltageErrorTimes = 0;
            chargerException.chargingVoltageErrorFlag = false;
        }
        if(chargerInfo.status == charging) {
					#ifdef CHARGING_CURRENT_16A
						if(chargerInfo.current > 18) // current: 0~18 13+2 >5s  fix 18487.1-2015 A3.10.7
					#endif
					#ifdef CHARGING_CURRENT_32A
						if(chargerInfo.current > 35.2)// current > 32*1.1=35.2A && >5s fix fix 18487.1-2015 A3.10.7
          #endif
            {						
							if((chargerException.chargingCurrentErrorFlag == false)&&(currentErrorTimes++ >= 4)){//max 5s
                chargerException.chargingCurrentErrorFlag = true;
							}
							pc.printf("\r\n Current out of range  Error!!!\r\n"); 
            }
						else {
								currentErrorTimes = 0;
                chargerException.chargingCurrentErrorFlag = false;
            }
        } else {
            if(chargerInfo.current > 0.2) { // leak current
							if((chargerException.chargingCurrentErrorFlag == false)&&(currentErrorTimes++ >= 5)){
                chargerException.chargingCurrentErrorFlag = true;
							}
							pc.printf("\r\n Current leakage  Error!!!\r\n"); 
            } else {
								currentErrorTimes = 0;
                chargerException.chargingCurrentErrorFlag = false;
            }
        }
				if((systemTimer % 30 == 0)&&(chargerInfo.status == charging)) {            
						eventHandle.updateChargerInfoFlag = true;
        }
        pc.printf("%0.1f - %0.2f - %0.3f - %0.1f \r\n",chargerInfo.energy,chargerInfo.voltage,chargerInfo.current,chargerInfo.power);  //voltage V/ current A/ power W
    } else { //default value
        if(chargerException.meterCrashedFlag == false) {
            if(meterErrorTimes++ >= 4) {
                chargerException.meterCrashedFlag = true;
            }
            pc.printf("\r\nMeter Crashed!!!\r\n");
        }
        return -1; // read meter error!!!
    }
    return 0;
}

/*******************************************************************
* @FunctionName  : disable_cp_pwm()
* @Describtion   : diable pwm     
* @Input         : none
* @Output        : none
*******************************************************************/
void disable_cp_pwm(void)
{
    cpPWM.period_ms(0);
    cpPWM = 1;
    pwmState = false;
}
/*******************************************************************
* @FunctionName  : enable_cp_pwm()
* @Describtion   : enable pwm     
* @Input         : none
* @Output        : none
*******************************************************************/
void enable_cp_pwm(float duty)
{
    cpPWM.period_ms(1);
    cpPWM = duty;
    pwmState = true;
}

int get_max_no(float *array,int len)
{
    int i,pos=0;
    for(i=1;i<len;i++)
    {
       if(array[i]>=array[pos])
          pos = i ;
    }
    return pos;
}
int get_min_no(float *array,int len)
{
    int i,pos=0;
    for(i=1;i<len;i++)
    {
        if(array[i]<=array[pos])
          pos = i ;       
    }
    return pos;
}

#define ADC_SAMPLE_RATE 20     // 1ms 20 times
#define ADC_SAMPLE_USE_TOP_NUM  3
/*******************************************************************
* @FunctionName  : check_cp_signal()
* @Describtion   : detect cp signal  
* @Input         : none
* @Output        : status of cp signal
*******************************************************************/
CPSignal check_cp_signal(void)
{
    float cpADCRead;
    float sumValue=0,averValue=0;
    float cp[20];
    int validCPNum=0;
		int minPos,maxPos;
		float top3Num[3];
    CPSignal ret = PWMNone;
    int i = 0;
	  float tmp; 
	
    for(i=0; i<ADC_SAMPLE_RATE; i++) {
        cpADCRead = cpADC*3.3;
//        pc.printf("CP =%f\r\n",cpADCRead);
         tmp = cpADCRead;
        wait_us(50);//50*20 us = 1 ms
    }
		#if 0
		for(i=0; i<ADC_SAMPLE_RATE; i++) 
				pc.printf("cp[%d] = %f\r\n",i,cp[i]);
		#endif
		for(i=0;i<ADC_SAMPLE_USE_TOP_NUM;i++){  //FIX_GB18487_TEST_PROBLEM
				maxPos = get_max_no(cp,20);
				top3Num[i] = cp[maxPos];
				cp[maxPos] = 0;	 
		}
		#if 0
		for(i=0; i<ADC_SAMPLE_USE_TOP_NUM; i++) 
			 pc.printf("top3Num[%d]=%f\r\n",i,top3Num[i]);
		#endif	
		minPos = get_min_no(top3Num,ADC_SAMPLE_USE_TOP_NUM);
		maxPos = get_max_no(top3Num,ADC_SAMPLE_USE_TOP_NUM);
		if(top3Num[maxPos] - top3Num[minPos] > 0.2)
				return ret;
		
		for(i=0; i<ADC_SAMPLE_USE_TOP_NUM; i++){
			 sumValue += top3Num[i];
		}		
		averValue = sumValue / ADC_SAMPLE_USE_TOP_NUM;
		if((averValue > 2.14)&&(averValue < 2.46)) { // (12-0.5) / 5  = 2.30 +/- (0.8/5)
				ret = PWM12V;
			} else if((averValue > 1.54)&&(averValue < 1.86)) { // (9-0.5) / 5  = 1.70 +/- (0.8/5)
				ret = PWM9V;
			}	else if((averValue > 0.94) && (averValue < 1.26)) { // (6-0.5) / 5  = 1.10 +/- (0.8/5)
				ret = PWM6V;
		}	else if(averValue < 0.1){
				ret = PWM0V;
		} else {
				ret = PWMOtherVoltage;
		}
//		float value = averValue*5+0.5f;
//		pc.printf("voltage:%f v\r\n",value);
    return ret;
}


/*******************************************************************
* @FunctionName  : check_charger_connection_status()
* @Describtion   : check the connect status of charger,total 3 status,
									 NOT_CONNECTED,CONNECTED_6V,CONNECTED_9V  
* @Input         : none
* @Output        : none
*******************************************************************/
void check_charger_connection_status(void)
{
		static int connectStatus = NOT_CONNECTED; 
    static CPSignal cpSignal = PWMNone;
		CPSignal tmpCPSignal = check_cp_signal();

		
		if(cpSignal != tmpCPSignal)
		{
			cpSignal = tmpCPSignal;
//			pc.printf("1: 12V  2: 9V 3: 6V 4: 0V 5: Other cpSignal=%d\r\n",cpSignal);
		}

		if((cpSignal == PWM12V)||(cpSignal == PWM0V)||(cpSignal == PWMOtherVoltage)) {
				connectStatus = NOT_CONNECTED;
				chargerException.chargingEnableFlag = false;
				S2SwitchEnabledFlag = false;  
		}
		else if((cpSignal == PWM9V) || (cpSignal == PWM6V)){
				if(cpSignal == PWM9V) {
						connectStatus = CONNECTED_9V;
						S2SwitchEnabledFlag = true;  // EV CAR S2 SWITCH DETECTED
				}
				else if(cpSignal == PWM6V) {
						connectStatus = CONNECTED_6V;
				}
				chargerException.chargingEnableFlag = true;
		}
		#ifdef LED_INFO_ENABLE		
		if(chargerException.chargingEnableFlag == true){
				CONNECT_LED_ON;
		}else{
				CONNECT_LED_OFF;
		}
		#endif		

    if(connectStatus != chargerInfo.connect) {
        chargerInfo.connect = connectStatus;
//				pc.printf("chargerInfo.connect = %d\r\n",chargerInfo.connect);
        eventHandle.updateChargerStatusFlag = true;
			  #if defined(EEPROM_ENABLE)
			  save_charger_info_to_eeprom();
		    #else
        save_charger_info_to_flash();
			  #endif
    }
}


/*******************************************************************
* @FunctionName  : check_charging_status()
* @Describtion   : check charging status
* @Input         : none
* @Output        : none
*******************************************************************/
void check_charging_status(void)
{
		switch(gChargingState){
			case EV_CONNECTED_PRE_START_CHARGING:
					if(chargerInfo.connect == CONNECTED_6V){
							gChargingState = EV_CONNECTED_ON_CHARGING;
						#ifdef LED_INFO_ENABLE
							CHARGING_LED_ON;
						#endif	
							if(chargerInfo.status == connected) {
									start_charging();   //Begin charging!
							}
							else{
									if(pauseChargingFlag == true){  // recover charging ;do not notify server
											pause_charging(false);
									}
							}
					}
					else{
						if(chargerInfo.connect != CONNECTED_9V){
								if(pauseChargingFlag == true){
										stop_charging();
										pauseChargingFlag = false;
								}
								disable_cp_pwm();//GB/T 18487.1-2015 A3.10.9
								pc.printf("error connect, disable charging!\r\n");
								gChargingState = EV_IDLE;
						}else{
								if(pauseChargingFlag == true && pauseChargingCounter > MAX_PAUSE_TIME){ //timeout of pause charging
										pauseChargingFlag = false;
									  eventHandle.stopChargingFlag = true;
									  chargingEndType = END_IN_ADVANCE_FULL;
										pauseChargingCounter = 0;
										stop_charging();
									  disable_cp_pwm(); 
										init_charger_info();
                    chargerInfo.status = connected;									

									  gChargingState = EV_IDLE;
								}
								if(startS2SwitchWaitCounterFlag == true && waitS2SwitchOnCounter > MAX_WAIT_TIME){
										startS2SwitchWaitCounterFlag = false;
										waitS2SwitchOnCounter = 0;
									  disable_cp_pwm();
										init_charger_info();
										chargerInfo.status = connected;

									  gChargingState = EV_IDLE;
								}
						}
					}
					break;
			case EV_CONNECTED_ON_CHARGING:
				  if(startS2SwitchWaitCounterFlag == true){
						startS2SwitchWaitCounterFlag = false;
						waitS2SwitchOnCounter = 0;
					}
					if(chargerInfo.connect != CONNECTED_6V){
						#ifdef LED_INFO_ENABLE
							CHARGING_LED_OFF;
						#endif	
							if(chargerInfo.connect == CONNECTED_9V){  // S2 switch open  PWM still enable
									gChargingState = EV_CONNECTED_PRE_START_CHARGING;
									pause_charging(true);
							}
							else{
									stop_charging();	  //gChargingState = EV_IDLE;
									disable_cp_pwm();
									startS2SwitchOnOffCounterFlag = false;					
							}
					}
					break;
			case EV_CONNECTED_PRE_STOP_CHARGING:		// fix: 18487.1-2015 A3.9.2
			    if((S2SwitchEnabledFlag == true) && (chargerInfo.connect == CONNECTED_6V)) {
							if((startS2SwitchOnOffCounterFlag == true) && (S2SwitchOnOffCounter < 3)){   // 3s timeout , STOP charging!!!
									return;
							}	
					}		
				#ifdef LED_INFO_ENABLE
					CHARGING_LED_OFF;
				#endif					
					stop_charging();
					init_charger_info();
					chargerInfo.status = connected;
					eventHandle.stopChargingFlag = true;  //notify msg to server 
				
					startS2SwitchOnOffCounterFlag = false;
					break;
			default:
					break;	
		}
}

/*******************************************************************
* @FunctionName  : check_charging_finish()
* @Describtion   : Check whether charging is finished
* @Input         : none
* @Output        : none
*******************************************************************/
void check_charging_finish(void)
{
	static int chargingTime = 0;
	static float chargingEnergy = 0;
	if(chargerInfo.status == charging){
		if(chargerInfo.chargingType == CHARGING_TYPE_MONEY){
			if(fabs(chargingEnergy - chargerInfo.energy)>=0.1){
				chargingEnergy = chargerInfo.energy;
				pc.printf("Current charging energy:%0.1f kwh,Total energy:%0.1f kwh\r\n",chargingEnergy,chargerInfo.setEnergy);
			}
			if(chargerInfo.energy >= chargerInfo.setEnergy){
				chargerException.chargingEnergyOverFlag = true;
				chargingEnergy = 0;
				pc.printf("charging Energy over!\r\n");
			}
		}else if(chargerInfo.chargingType == CHARGING_TYPE_TIME){
			if(chargingTime != chargerInfo.duration/60){
				chargingTime = chargerInfo.duration/60;
				pc.printf("Current charging time:%d min,Total Time:%d min\r\n",chargingTime,chargerInfo.setDuration);
			}
			if(chargerInfo.duration/60 >= chargerInfo.setDuration){
				chargerException.chargingTimeOverFlag = true;
				chargingTime = 0;
				pc.printf("charging Time Over!\r\n");
			}
		}
	}
}

/*******************************************************************
* @FunctionName  : get_led_status()
* @Describtion   : get current led status
* @Input         : none
* @Output        : the status of led
*******************************************************************/
char get_led_status(void)
{
	char status = 0;
	if((chargerInfo.status & 0x70) && (chargerInfo.status & errorCode))
    status = 0x03;
	else if(chargerInfo.status == charging)
		status = 0x02;
	else
		status = 0x01; 
  if(chargerInfo.connect == 1 || chargerInfo.connect == 2)
    status |= 0x04;	
	return status;
}

/*******************************************************************
* @FunctionName  : recv_msg_handle()
* @Describtion   : handle recieved massege
* @Input         : none
* @Output        : none
*******************************************************************/

void recv_msg_handle(void)
{
  if(recvData.flag){
		memcpy(socketInfo.inBuffer,recvData.data,recvData.len);
		socketInfo.inBuffer[recvData.len] = '\0';
		pc.printf("recieved %d bytes from %s\r\n",recvData.len,recvData.ip);
		memset(recvData.data,0,sizeof(recvData.data));
		recvData.flag = 0;
		parse_recv_msg(socketInfo.inBuffer);
	}
}



/*******************************************************************
* @FunctionName  : init_event_handle()
* @Describtion   : init event handle
* @Input         : none
* @Output        : none
*******************************************************************/
void init_event_handle(void)
{
    eventHandle.heatbeatFlag = false;
    eventHandle.checkMeterFlag = false;
    eventHandle.updateChargerInfoFlag = false;
    eventHandle.updateChargerStatusFlag = false;
    eventHandle.stopChargingFlag = false;
    eventHandle.getLatestFWFromServerFlag = false;
	  eventHandle.updateVersionDoneFlag = false;
	  eventHandle.firstConnectFlag = true;
}


/*******************************************************************
* @FunctionName  : check_network_available()
* @Describtion   : check if network available     
* @Input         : none
* @Output        : none
*******************************************************************/
void check_network_available(void)
{
	int sockStatus;
	/*check socket stauts*/
	if(checkSocketFlag == true){
		checkSocketFlag = false;
		sockStatus = at_get_sock_status(AT_DEFAULT_SOCK_ID);
		pc.printf("current socket status:%d\r\n",sockStatus);
		if(sockStatus == sock_open_normal || sockStatus == sock_open_send_buf_full){
			chargerException.serverConnectedFlag = true;
		}else{
			chargerException.serverConnectedFlag = false;
		}
	
	if(chargerException.serverConnectedFlag == true){/* send hearbeat if connected to server */
		if(eventHandle.heatbeatFlag == true){
			char buf[64];
			sprintf(buf,NOTIFY_REQ_HeartPackage,get_led_status());
			at_send(AT_DEFAULT_SOCK_ID,buf,strlen(buf));
			pc.printf("send heartbeat:%s\r\n",buf);
			eventHandle.heatbeatFlag = false;
		}
	}else{ /* reconnect to server */
			if(at_check_ps_call() != ps_connected){
				if(at_ps_call(1,device_ip) != ps_connected){
					pc.printf("ps call fail!\r\n");
					return;
				}
			}
			sockStatus = at_get_sock_status(AT_DEFAULT_SOCK_ID);
			if(sockStatus == sock_close){
				if(at_connect_server(AT_DEFAULT_SOCK_ID,AT_TCP_PROTOCOL,SERVER_IP_ADDRESS,SERVER_PORT) == 1){
					pc.printf("connected to server!\r\n");
					chargerException.serverConnectedFlag = true;
				}
			}else if(sockStatus == sock_opening || sockStatus == sock_closing){
				at_close_socket(AT_DEFAULT_SOCK_ID);
			}
		}
	}
}

/*******************************************************************
* @FunctionName  : one_second_thread()
* @Describtion   : 1s thread      
* @Input         : none
* @Output        : none
*******************************************************************/
void one_second_thread(void const *argument)
{
    while (true) {	
//			if(systemTimer >= 3){
//						eventHandle.checkMeterFlag = true; // 1s check meter info
//        }
//				
//        if(chargerException.serverConnectedFlag == false) {
//            resetTimer++;
//        }
//#ifdef RTC_ENABLE
//		getTimeStampMs(timeStampMs);
//		pc.printf("time:%s\r\n",timeStampMs);
//#endif
//#ifdef ENABLE_FULL_CHARGING_DETECT
//				if(chargerInfo.status == charging){
//					if(chargerInfo.current > 0.9f && chargerInfo.current < 1.1f){
//						fullChargingDetectCounter++;
//					}else{
//						fullChargingDetectCounter = 0;
//					}
//					if(fullChargingDetectCounter >= FULL_CHARGING_DETECT_TIME){
//						fullChargingFlag = true;
//						fullChargingDetectCounter = 0;
//					}
//				}else{
//					fullChargingDetectCounter = 0;
//					fullChargingFlag = false;
//				}
//#endif

//				if(rechargingFlag == true){
//						if(rechargingTimer > NORMAL_TIME_HOLDED){
//							  startContinueChargingFlag = true;
//								rechargingFlag = false;
//								rechargingWaitCounter = 0;
//								rechargingTimer = 0;
//						}else if(chargerInfo.status != connected){
//								if(++rechargingWaitCounter > RECHARGING_WAIT_TIMES_ALLOWED){
//									  rechargingWaitCounter = 0;
//										eventHandle.stopChargingFlag = true; //notify END charging msg to server
//									  chargingEndType = get_charging_end_type(chargerInfo.status);
//										init_charger_info();
//                    pc.printf("\r\nrecharging fail ! stop charging!\r\n\r\n");									
//								}else{
//										chargingExceptionFlag = true;
//								}
//								rechargingFlag = false;
//								rechargingTimer = 0;							
//								pc.printf("\r\nrechargingWaitCounter =%d\r\n\r\n",rechargingWaitCounter);
//						}else{
//								rechargingTimer++;
//							  pc.printf("rechargingTimer = %d \r\n",rechargingTimer);
//						}
//				}
//				if(chargingExceptionFlag  == true){
//					if(chargingErrorTimer > MAX_EXCEPTION_TIME_ALLOWED){
//							chargingExceptionFlag = false;
//						  chargingErrorTimer = 0;
//						  rechargingWaitCounter = 0;
//						  eventHandle.stopChargingFlag = true;  //notify END charging msg to server
//						  if(chargerInfo.status == 0x4082)
//								chargerInfo.status = idle;
//							else
//								chargerInfo.status &= 0xFFFC;
//						  chargingEndType = get_charging_end_type(chargerInfo.status);
//						  init_charger_info();
//              pc.printf("Exception Timeout,stop charging!\r\n");						
//					}else if(chargerInfo.status == connected){
//							rechargingFlag = true;
//						  chargingExceptionFlag = false;
//						  chargingErrorTimer = 0;
//						  rechargingTimer = 0;
//						  pc.printf("rechargingFlag = true,prepare to continute charging!\r\n");
//					}else
//						chargingErrorTimer++;
//				}

//				if(startS2SwitchOnOffCounterFlag == true)
//						S2SwitchOnOffCounter++;

//				if(systemTimer % 5 == 0)
//					checkChargingFinishFlag = true;
				if(systemTimer % 30 == 0)
					eventHandle.heatbeatFlag = true;
				if(systemTimer % 5 == 0)
					checkSocketFlag = true;
        Thread::wait(1000);
        systemTimer++;
				pc.printf("systemTimer=%d\r\n",systemTimer);
//        if(chargerInfo.status == charging)
//          chargerInfo.duration++;
//				if(pauseChargingFlag == true){
//					pauseChargingCounter++;
//				}
//				if(startS2SwitchWaitCounterFlag == true){
//					waitS2SwitchOnCounter++;
//				}
    }
} 
/*******************************************************************
* @FunctionName  : exception_handle_thread()
* @Describtion   : handle charging exception,period is 20 ms      
* @Input         : none
* @Output        : none
*******************************************************************/
void exception_handle_thread(void const *argument)
{
	while(true){
			check_charger_connection_status();			
  		check_charging_status();
			charging_exception_handle();		

		if(eventHandle.checkMeterFlag == false){
			Thread::wait(20);
		}else{
			eventHandle.checkMeterFlag = false;
      get_meter_info();
		}
	}
}
/*******************************************************************
* @FunctionName  : nb_uart_irq()
* @Describtion   : recieve data,Store data in the buffer      
* @Input         : none
* @Output        : none
*******************************************************************/
void nb_uart_irq(void)
{
	if(pBufNext <= NB_UART_BUF_SIZE)
		nb_uart_buf[pBufNext++] = nb.getc(); 
	else
		return;
}
/*******************************************************************
* @FunctionName  : clear_uart_buffer()
* @Describtion   : clear uart rx buffer      
* @Input         : none
* @Output        : none
*******************************************************************/
void clear_uart_buffer(void)
{
	memset(nb_uart_buf,0,strlen(nb_uart_buf));//clear uart rx buffer
	pBufNext = 0;
}

/*******************************************************************
* @FunctionName  : at_recv_thread()
* @Describtion   : parse at command      
* @Input         : none
* @Output        : none
*******************************************************************/
void at_recv_thread(void const* argument)
{
	while(true){
		at_parse_sema.wait();
		char* pStart = strstr(nb_uart_buf,AT_ZIPRECV);
		if(pStart != NULL){//handle cmd msg 
			pc.printf("recv data!\r\n");
			char* p = pStart + strlen(AT_ZIPRECV);
			int i = 0;
			char tmp[10];
			while(*p != ','){  //get socket id
				tmp[i++] = *p++; 
			}
			tmp[i] = '\0';
			recvData.sockId = atoi(tmp); 
			p++;i=0;
			while(*p != ','){ //get server ip
				recvData.ip[i++] = *p++;
			}
			recvData.ip[i] = '\0';
			p++;i=0;
			while(*p != ','){//get server port
				tmp[i++] = *p++;
			}
			tmp[i] = '\0';
			recvData.port = atoi(tmp);
			p++;i=0;
			while(*p != ','){ //get data len
				tmp[i++] = *p++;
			}
			tmp[i] = '\0';
			recvData.len = atoi(tmp);
			p++;i=0;
			for(int i=0;i<recvData.len;i++){ //get data
				recvData.data[i] = *p++;
			}
			recvData.data[recvData.len] = '\0';
			recvData.flag = 1;  //set flag
			pc.printf("socket id =%d,ip=%s,port=%d,len=%d,data=%s",
				recvData.sockId,recvData.ip,recvData.port,recvData.len,recvData.data);
		}
			pStart = strstr(nb_uart_buf,AT_ZIPSTAT);
			if(pStart != NULL){
				char* p = pStart + strlen(AT_ZIPSTAT) + 1;
				char tmp[4];
				tmp[0] = *p;
				tmp[1] = '\0';
				sock.sock_id = atoi(tmp);
				tmp[0] = *(p+2);
				sock.sock_stat = atoi(tmp);
				pc.printf("status change,sock id:%d,sock status:%d\r\n",sock.sock_id,sock.sock_stat);
			}
			clear_uart_buffer();
		}
}
/*******************************************************************
* @FunctionName  : recv_detect_thread()
* @Describtion   : detect if recieved data per 50 ms     
* @Input         : None
* @Output        : None
*******************************************************************/
void recv_detect_thread(void const* argument)
{
	while(true){
		if(pBufNext != 0){
			if(strstr(nb_uart_buf,AT_ZIPRECV) != NULL || strstr(nb_uart_buf,AT_ZIPSTAT) != NULL)
				at_parse_sema.release();  //recieve data,notify to handle
		}
		Thread::wait(500);
	}
}

/*******************************************************************
* @FunctionName  : main()
* @Describtion   : main function      
* @Input         : None
* @Output        : 0
*******************************************************************/
int main()
{
	  #ifdef LED_INFO_ENABLE
			WARNING_LED_OFF;
			CHARGING_LED_OFF;
			CONNECT_LED_OFF;
			SERVER_CONNECT_LED_OFF;
		#endif	
	  #ifdef EEPROM_ENABLE
			pinConfig();  //initialize i2c1 pins
		#endif	
	  
		pc.baud(115200);
    nb.baud(115200);
    rs485.baud(9600);  
    nb.attach(&nb_uart_irq);  //register uart interrupt function
    switchRelayOnoff = RELAY_OFF;
	  disable_cp_pwm();
    
	  init_charger();
	  init_event_handle();
	  us_ticker_init();
	  memcpy(EVChargerModelNO,DEFAULT_MODEL_NO,strlen(DEFAULT_MODEL_NO));
//	  init_ota();
//    pc.printf("waiting nb module setup!\r\n");
		pc.printf("Model NO:%s\r\n",EVChargerModelNO);
//		wait_ms(5000);
		pc.printf("\r\nAPP VER: %s\r\n",VERSION_INFO);
		
	
				
	  int ret;
		ret = at_set_baudrate(at_baudrate[9]);
		pc.printf("set baudrate=%s\r\nret=%d\r\n",at_baudrate[9],ret);
		ret = at_echo_setting(0); //disable echo function
		pc.printf("disable echo,ret=%d\r\n",ret);
		ret = at_init();
		pc.printf("ret=%d\r\n",ret);
		ret = check_network_register();
		pc.printf("check network conditions,ret=%d\r\n",ret);
		if(at_check_ps_call()!=1){
			pc.printf("open ps call!\r\n");
			ret = at_ps_call(1,device_ip);
			if(ret>0){
				pc.printf("status=%d,ip=%s\r\n",ret,device_ip);
			}
		}
    ret = at_get_sock_status(AT_DEFAULT_SOCK_ID);
		if(ret == sock_open_normal){
			at_close_socket(AT_DEFAULT_SOCK_ID);
		}
		at_get_sn(EVChargerModelNO);
		ret = at_connect_server(AT_DEFAULT_SOCK_ID,AT_TCP_PROTOCOL,SERVER_IP_ADDRESS,SERVER_PORT);
		if(ret == 1){
			chargerException.serverConnectedFlag = true;
			pc.printf("connected to server:%s:%s!\r\n",SERVER_IP_ADDRESS,SERVER_PORT);
		}else{
			pc.printf("connot connect to server!ret=%d\r\n",ret);
		}
    
//		ret = at_close_socket(AT_DEFAULT_SOCK_ID); 
//		pc.printf("clsoe socket,ret=%d\r\n",ret);
		
	  /* create threads */
	  Thread th1(at_recv_thread,NULL,osPriorityNormal,1024);
	  Thread th2(recv_detect_thread,NULL,osPriorityNormal,512);
		Thread th3(one_second_thread,NULL,osPriorityNormal,1024);
    Thread th4(exception_handle_thread,NULL,osPriorityAboveNormal,1024);				
		
    while (true){
				check_network_available();
    }
}