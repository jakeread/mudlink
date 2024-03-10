#include "micros_base_64.h"

uint64_t microsBase64(void){
  static uint64_t lastMicros = 0;
  static uint64_t overflows = 0;

  uint32_t currentMicros = micros();

  if(currentMicros < lastMicros){
    overflows ++;
  }
  lastMicros = currentMicros; 

  return (overflows << 32) | currentMicros; 
}