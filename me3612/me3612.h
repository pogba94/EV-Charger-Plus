#ifndef _ME3612_H
#define _ME3612_H

#include "mbed.h"

#define TCP_RECV_DATA_BUF_SIZE     (256)

#define TIMEOUT_VALUE_1S           (1000)
#define TIMEOUT_VALUE_5S           (5000)
#define TIMEOUT_VALUE_10S          (10000)
#define TIMEOUT_VALUE_15S          (15000)

typedef struct {
	uint8_t sockId;
	char ip[20];
	uint16_t port;
	uint16_t len;
	char data[TCP_RECV_DATA_BUF_SIZE];
	uint8_t flag;
}TCP_RECV_DATA_T;

typedef enum{
	set = 1,
	read = 2,
	test = 3,
	pass = 4,
}CMD_TYPE_T;

typedef enum{
	at_error = -1,
	at_illegal = -2,
	at_timeout = -3,
  at_ok = 1,
}AT_RESP_T;

typedef enum{
	sock_close = 0,
	sock_open_normal,
	sock_open_send_buf_full,
	sock_opening,
	sock_closing,
}AT_SOCK_STATUS_T;

typedef struct sock{
	int sock_id;
	int sock_stat;
}AT_SOCK_T;


typedef enum{
	ps_disconnect = 0,
	ps_connected = 1,
	ps_connecting = 2,
	ps_disconnecting = 3,
}AT_PS_CALL_STATUS_T;

/* AT Command Definiton */
#define AT_CMD_ZPCHSC         "AT+ZPCHSC"
#define AT_CMD_ZIOTPREF       "AT+ZIOTPREF"
#define AT_CMD_ZSNT           "AT+ZSNT"
#define AT_CMD_CPIN           "AT+CPIN"
#define AT_CMD_ZPAS           "AT+ZPAS"
#define AT_CMD_CGDCONT        "AT+CGDCONT"
#define AT_CMD_ZIPCALL        "AT+ZIPCALL"
#define AT_CMD_ZIPOPEN        "AT+ZIPOPEN"
#define AT_CMD_ZIPSEND        "AT+ZIPSEND"
#define AT_CMD_ZIPSENDRAW     "AT+ZIPSENDRAW"
#define AT_CMD_ZIPCLOSE       "AT+ZIPCLOSE"
#define AT_CMD_IPR            "AT+IPR"
#define AT_CMD_ATE1           "ATE1"
#define AT_CMD_ATE0           "ATE0"
#define AT_CMD_ATQ            "ATQ"
#define AT_CMD_ZCSQ           "AT+ZCSQ"
#define AT_CMD_ZIPSTAT        "AT+ZIPSTAT" 
#define AT_CMD_GSN           "AT+GSN"

#define AT_ZIPRECV            "+ZIPRECV:"
#define AT_ZIPSTAT            "+ZIPSTAT:"

/* AT Command Result Code */
#define RESULT_CODE_OK        "OK"
#define RESULT_CODE_ERROR     "ERROR"
#define RESULT_CODE_BUSY      "BUSY"
#define RESULT_CODE_CONNECT   "CONNECT"
#define END_CODE              "\r\n"

#define AT_TCP_PROTOCOL       '0'
#define AT_UDP_PROTOCOL       '1'
#define AT_DEFAULT_SOCK_ID    '1'   //default socket id is 1
#define AT_MAX_SEND_DATA_SIZE  1024  //max size is 1024 Bytes 

int at_send_command(char* cmd,uint8_t cmdType,char* param,char* resp_exp,char result[],uint16_t timeout);
int at_init(void);
int at_set_baudrate(char* baudrate);
int at_echo_setting(bool setting);
int check_network_register(void);
int at_ps_call(bool flag,char ip[]);
int at_check_ps_call(void);
int at_connect_server(char sock_id,char protocol,char* ip,char* port);
int at_close_socket(char sock_id);
int at_get_sock_status(char sock_id);
int at_send(char sock_id,char* data,int len);
int at_get_sn(char sn[]);

#endif