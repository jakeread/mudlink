#ifndef MUDL_STATS_TYPE_H_
#define MUDL_STATS_TYPE_H_ 

#include <Arduino.h>

typedef struct MUDLStats {
  uint32_t rxSuccessCount = 0;
  uint32_t rxFailureCount = 0;
  uint32_t txSuccessCount = 0; 
  uint32_t txFailureCount = 0; 
  uint32_t txTotalRetries = 0;
  // the longest timeout issued since startup 
  uint32_t outgoingTimeoutLengthHighWaterMark = 0; 
  // maybe the most important, average message trip time, 
  float averageTotalTransmitTime = 0.0F;
  // comparable to the wire time (bits / baudrate) for most msgs 
  float averageWireTime = 0.0F;
  // and the num of tx retries normally required 
  float averageRetryCount = 0.0F; 
} MUDLStats;

#endif 