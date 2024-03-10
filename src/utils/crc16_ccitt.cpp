#include "crc16_ccitt.h"

const uint16_t polynomial = 0x1021;
uint16_t crcTable[256];

void crc16_generate_table() {
  for (int i = 0; i < 256; i++) {
    uint16_t crc = i << 8;
    for (int j = 0; j < 8; j++) {
      if (crc & 0x8000)
        crc = (crc << 1) ^ polynomial;
      else
        crc <<= 1;
    }
    crcTable[i] = crc;
  }
}

const uint16_t initialValue = 0xFFFF;

uint16_t crc16_ccitt(uint8_t* data, size_t len) {
  uint16_t crc = initialValue;
  for (size_t i = 0; i < len; i++) {
    // Use XOR to find index into crc table and combine with the rest of the CRC.
    uint8_t index = (crc >> 8) ^ data[i]; // Find the index into the table
    crc = crcTable[index] ^ (crc << 8);   // Look up in the table and combine with the rest
  }
  return crc;
}
