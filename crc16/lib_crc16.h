
#ifndef _LIB_CRC16_H_
#define _LIB_CRC16_H_

#include <stdint.h>


//uint16_t update_crc16_normal( uint16_t *table, uint16_t crc, char c );
uint16_t update_crc16_reflected( uint16_t *table, uint16_t crc, char c );


uint16_t update_crc16_A001( uint16_t crc, char c );



/* Does the CRC calculation over a string specified by length (allows 00 inside string) */
uint16_t calculate_crc16_Modbus(char *p, unsigned int length);
uint16_t calculate_crc16(char *p, unsigned int length);

#endif /* _LIB_CRC_H_ */
