/*****************************************************************
* @FileName      : me3612.cpp
* @Describtion   : me3612 api
* @Arthor        : Orange Cai
* @Date          : 2018-03-20
* @History       : Initial
*****************************************************************/

#include "me3612.h"
#include "stdlib.h"

char result[40];

extern Serial nb;
extern Serial pc;
extern Timer timer;
extern volatile uint16_t pBufNext;
extern char nb_uart_buf[];

extern void clear_uart_buffer(void);

/*******************************************************************
* @FunctionName  : send_at_cmd()
* @Describtion   : send at command
* @Input         : 
									 @cmd: at command string
									 @cmdType: set,read or test
                   @param: command parameter,it may be needed when cmdType is set
									 @resp_exp: result code expected
									 @result: result imformation
                   @timeout: timeout value in ms									 
* @Output        : result code
*******************************************************************/
int at_send_command(char* cmd,uint8_t cmdType,char* param,char* resp_exp,char res[],uint16_t timeout){
	clear_uart_buffer();
	switch(cmdType){
		case set:
			nb.puts(cmd);
		  if(param != NULL){
				nb.putc('=');
				nb.puts(param);
			}
			nb.puts(END_CODE);break;
		case read:
			nb.puts(cmd);nb.putc('?');nb.puts(END_CODE);break;
		case test:
			nb.puts(cmd);nb.puts("=?");nb.puts(END_CODE);break;
		case pass:
			nb.puts(cmd);nb.puts(END_CODE);break;
		default:
			return at_illegal;
	}
	timer.reset();
	timer.start();
	while(timer.read_ms()<timeout){
		if(strstr(nb_uart_buf,resp_exp) != NULL){
			char* p[5];
			int len;
			if(*resp_exp == '>'){
				wait_ms(5);  //take care!
				clear_uart_buffer();
				return at_ok;
			}
			if(res == NULL){
				p[0] = strstr(nb_uart_buf,END_CODE);
				p[1] = strstr(p[0]+2,END_CODE);
				if(p[0] != NULL && p[1] != NULL){//wait for recieve finish
					clear_uart_buffer();
					return at_ok;
				}
			}else{
				p[0] = strstr(nb_uart_buf,END_CODE);
				p[1] = strstr(p[0]+2,END_CODE);
				p[2] = strstr(p[1]+2,END_CODE);
				p[3] = strstr(p[2]+2,END_CODE);
				if(p[0] != NULL && p[1] != NULL && p[2] != NULL && p[3] != NULL){
//					pc.printf("recv=%s\r\n",nb_uart_buf);
					if(p[0]+2 == strstr(nb_uart_buf,resp_exp)){
						len = p[3] - p[2] - strlen(END_CODE);
						for(int i=0;i<len;i++){
							res[i] = *(p[2]+2+i);
						}
						res[len] = '\0';
						clear_uart_buffer();
						return at_ok;
					}else if(p[1]+2 == strstr(nb_uart_buf,resp_exp)){
						p[4] = strstr(p[3]+2,END_CODE);
						if(p[4] != NULL){
							len = p[4] - p[3] - strlen(END_CODE);
							for(int i=0;i<len;i++){
								res[i] = *(p[3]+2+i);
							}
							res[len] = '\0';
							clear_uart_buffer();
							return at_ok;
						}
					}else if(p[2]+2 == strstr(nb_uart_buf,resp_exp)){
						len = p[1] - p[0] - strlen(END_CODE);
						for(int i=0;i<len;i++){
							res[i] = *(p[0]+2+i);
						}
						res[len] = '\0';
						clear_uart_buffer();
						return at_ok;
					}else{
						return at_error;
					}
				}
			}
		}
	}
	pc.printf("uart buf=%s\r\n",nb_uart_buf);
	timer.stop();
	clear_uart_buffer();
	return at_timeout;
}

/*******************************************************************
* @FunctionName  : at_set_baudrate()
* @Describtion   : set baudrate
* @Input         : baudrate value							 
* @Output        : 1 if ok and 0 otherwise
*******************************************************************/
int at_set_baudrate(char* baudrate)
{
	if(at_send_command(AT_CMD_IPR,set,baudrate,RESULT_CODE_OK,NULL,TIMEOUT_VALUE_1S) == at_ok){
		return 1;
	}else{
		return 0;
	}
}

/*******************************************************************
* @FunctionName  : at_echo_setting()
* @Describtion   : setting echo function
* @Input         : enable echo if 1 and disable echo if 0							 
* @Output        : 1 if ok and 0 otherwise
*******************************************************************/
int at_echo_setting(bool setting)
{
	if(setting == true){
		return at_send_command(AT_CMD_ATE1,set,NULL,RESULT_CODE_OK,NULL,TIMEOUT_VALUE_1S);
	}else{
		return at_send_command(AT_CMD_ATE0,set,NULL,RESULT_CODE_OK,NULL,TIMEOUT_VALUE_1S);
	}
}

/*******************************************************************
* @FunctionName  : check_network_register()
* @Describtion   : check register status of network
* @Input         : none							 
* @Output        : 1 if network registered
									 0 if send command failed
									-1 if network no service
									-2 if other conditions
*******************************************************************/
int check_network_register(void)
{
	if(at_send_command(AT_CMD_ZPAS,read,NULL,RESULT_CODE_OK,result,TIMEOUT_VALUE_1S) == at_ok){
		if(strcmp(result,"+ZPAS: \"LTE Cat.NB1\",\"PS_ONLY\"") == NULL){
			pc.printf("network registered!\r\n");
			return 1;
		}else if(strcmp(result,"+ZPAS: \"NO SERVICE\"") == NULL){
			pc.printf("no service!\r\n");
			return -1;
		}else{
			return -2;
		}
	}else{
		return 0;//send command failed
	}
}

/*******************************************************************
* @FunctionName  : at_ps_call()
* @Describtion   : open or close ps call
* @Input         : 
										1. flag: true->open and false->close
										2. ip: pointer to current device ip
* @Output        : 
										-1 if send command fail
										0 if status is disconnected
										1 if status is connected
										2 if status is connecting
										3 if status is disconnecting
*******************************************************************/
int at_ps_call(bool flag,char ip[])
{
	char* param = (flag==true)?(char*)"1":(char*)"0";
	if( at_send_command(AT_CMD_ZIPCALL,set,param,RESULT_CODE_OK,result,TIMEOUT_VALUE_5S) == at_ok){
		char* p = strstr(result,",");
		int status = atoi(p-1);
		int i=0;p++;
		while(*p != ','){
			ip[i++] = *p++;
		}
		pc.printf("result=%s,status=%d,ip=%s\r\n",result,status,ip);
		return status;
	}else{
		return -1;
	}
}

/*******************************************************************
* @FunctionName  : at_check_ps_call()
* @Describtion   : check status of ps call
* @Input         : none
* @Output        : 1 if ps call is opened
									
*******************************************************************/
int at_check_ps_call(void)
{
	if(at_send_command(AT_CMD_ZIPCALL,read,NULL,RESULT_CODE_OK,result,TIMEOUT_VALUE_1S) == at_ok){
		char* p = strstr(result,":");
		int status = atoi(p+2);
		return status;
	}else{
		return -1;
	}
}

/*******************************************************************
* @FunctionName  : at_connect_server()
* @Describtion   : connect to server
* @Input         : 	
									 1. sock_id:socket id:1~5
									 2. protocol:0->tcp,1->udp
	                 3. ip, server ip address with ipv4 format
									 4. port, server port
* @Output        : 
										-100 if sock_id is out of range
										-99 if protocol is illegal
										-98 if ip or port is null
										-1 if send at command fail
										1 if connected to server,socket is open
										
*******************************************************************/
int at_connect_server(char sock_id,char protocol,char* ip,char* port)
{
	if(sock_id < '1' || sock_id > '5')
		return -100;
	if(protocol != '0' && protocol != '1')
		return -100;
	if(ip == NULL || port == NULL)
		return -100;
	char param[30];
	int p = 0;
	param[p++] = sock_id;
	param[p++] = ',';
	param[p++] = protocol;
	param[p++] = ',';
	for(int i=0;i<strlen(ip);i++){
		param[p++] = ip[i];
	}
	param[p++] = ',';
	for(int i=0;i<strlen(port);i++){
		param[p++] = port[i];
	}
	param[p] = '\0';
	pc.printf("param=%s\r\n",param);
	if(at_send_command(AT_CMD_ZIPOPEN,set,param,RESULT_CODE_OK,result,TIMEOUT_VALUE_10S) == at_ok){
		int status;
		char* p = strstr(result,",");
		status = atoi(p+1);
		pc.printf("status=%d\r\n",status);
		return status;
	}else{
		return -1;
	}
}

/*******************************************************************
* @FunctionName  : at_close_socket()
* @Describtion   : close socket
* @Input         : sock_id:socket id whose range is 1~5 	
* @Output        : 
									 -100 if socket id is out of range
									 -1 if close socket error or send command failed
                    1 if close socket successfully
								   othre value is the socket status
*******************************************************************/
int at_close_socket(char sock_id)
{
	if(sock_id < '1' || sock_id > '5')
		return -100;
	char* param = (char*)malloc(2);
	param[0] = sock_id;
	param[1] = '\0';
	if(at_send_command(AT_CMD_ZIPCLOSE,set,param,RESULT_CODE_OK,NULL,TIMEOUT_VALUE_5S) != at_ok){
			pc.printf("close socket error!\r\n");
	}else{
//		char* p = strstr(result,",");
//		int status = atoi(p+1);
//		if(status == 0){
//			pc.printf("close successfully!\r\n");
//			return 1;
//		}else{
//			return status;
//		}
		pc.printf("close socket!\r\n");
	}
}

/*******************************************************************
* @FunctionName  : at_get_sock_status()
* @Describtion   : get socket status
* @Input         : sock_id:socket id whose range is 1~5
* @Output        : 
									-100 if socket id is out of range
                  -1 if send command failed
                   1 if socket is open
                   0 if socket is close
                   
*******************************************************************/
int at_get_sock_status(char sock_id)
{
	if(sock_id < '1' || sock_id > '5'){
		return -100;
	}
	if(at_send_command(AT_CMD_ZIPSTAT,set,"1",RESULT_CODE_OK,result,TIMEOUT_VALUE_1S) != at_ok){
		return -1;
	}else{
		char* p = strstr(result,",");
		int status = atoi(p+1);
		return status;
	}
}
/*******************************************************************
* @FunctionName  : at_init()
* @Describtion   : initial module configuration
* @Input         : none							 
* @Output        : 
*******************************************************************/
int at_init(void)
{
	if(at_send_command(AT_CMD_ZPCHSC,set,"0",RESULT_CODE_OK,NULL,TIMEOUT_VALUE_1S) != at_ok)  //set Scrambling algorithm
		return -1;
	if(at_send_command(AT_CMD_ZIOTPREF,set,"2",RESULT_CODE_OK,NULL,TIMEOUT_VALUE_1S) != at_ok)//choose nb network
		return -2;
	if(at_send_command(AT_CMD_ZSNT,set,"6,0,0",RESULT_CODE_OK,NULL,TIMEOUT_VALUE_1S) != at_ok)//set LTE only
		return -3;
	if(at_send_command(AT_CMD_CPIN,read,NULL,RESULT_CODE_OK,result,TIMEOUT_VALUE_1S) == at_ok){
//		pc.printf("result=%s\r\n",result);
		if(strcmp(result,"+CPIN: READY") == NULL){
			pc.printf("sim ready!\r\n");
		}
	}else{
		return -4;
	}
	return 1;
}

/*******************************************************************
* @FunctionName  : at_get_sn()
* @Describtion   : get sn from ME3612 module
* @Input         : char array for	storing module sn number						 
* @Output        : 1 if get sn successfully
                   -1 if error appear
                   -100 if param is illegal
*******************************************************************/
int at_get_sn(char sn[])
{
	if(sn == NULL)
		return -100;
	if(at_send_command(AT_CMD_GSN,pass,NULL,RESULT_CODE_OK,result,TIMEOUT_VALUE_1S) == at_ok){
		sprintf(sn,result);
		pc.printf("sn=%s\r\n",sn);
		return 1;
	}else{
		pc.printf("get sn timeout!");
		return -1;
	}
}

/*******************************************************************
* @FunctionName  : at_send()
* @Describtion   : send data to server
* @Input         : 
										1. sock_id : socket id used  
										2. data: pointer to the data to be sended
										3. len: length expected to send
* @Output        :  
									  -100 if socket id is out of range
										1 if send successfully

*******************************************************************/
int at_send(char sock_id,char* data,int len)
{
	if(sock_id < '1' || sock_id > '5')
		return -100;
	if(data == NULL)
		return -99;
	if(len <= 0 || len > AT_MAX_SEND_DATA_SIZE)
		return -98;
	char* param = (char*)malloc(len + 8);
	int p = 0;
	char length[4];
	sprintf(length,"%d",len);
	param[p++] = sock_id;
  param[p++] = ',';
	for(int i=0;i<4;i++)
		param[p++] = length[i];
  param[p] = '\0';
  pc.printf("param=%s\r\n",param);
  int ret = at_send_command(AT_CMD_ZIPSENDRAW,set,param,">",NULL,TIMEOUT_VALUE_1S);
	if(ret != at_ok){
		pc.printf("send data no respond!ret=%d\r\n",ret);
		return -1;
	}else{
		if(at_send_command(data,pass,NULL,RESULT_CODE_OK,result,TIMEOUT_VALUE_5S) == at_ok){
			pc.printf("send data!result=%s\r\n",result);
			return 1;
		}else{
			pc.printf("send data failed!\r\n");
			return -1;
		}
	}
}	