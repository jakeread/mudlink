#ifndef CRC16_CCITT_H_
#define CRC16_CCITT_H_

#include <Arduino.h>

void crc16_generate_table(void);
uint16_t crc16_ccitt(uint8_t* data, size_t len);

#endif 