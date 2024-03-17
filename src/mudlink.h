/*
mudlink.h

Modular UART Duplex Link 

Jake Read at the Center for Bits and Atoms
(c) Massachusetts Institute of Technology 2024

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef MUDL_H_
#define MUDL_H_

#include <Arduino.h>
#include "mudl_stats_type.h"
#include "utils/mudl_cobs.h"
#include "utils/crc16_ccitt.h"
#include "utils/micros_base_64.h"

// buffers are + 8 bytes: 2 crc, 1 delimiter, 1 cobs stuffer, ... 
#define MUDL_MAX_PACKET_SIZE 248 
#define MUDL_BUFFERS_SIZE 256

#define MUDL_MAX_NUM_RETRIES 10
#define MUDL_RETRY_EXP_BASE 2
#define MUDL_RETRY_INITIAL_MULTIPLE 6 

template <typename SerialImplementation>
class MUDL_Link {
  public:
    MUDL_Link(SerialImplementation* _impl, uint32_t _baudrate){
      impl = _impl; 
      baudrate = _baudrate;
      // we use this to calculate timeout intervals, 
      microsecondsPerByte = 1000000 / (baudrate / 10);
      // max interval is for 100k byte times, 
      retryAbsMaxInterval = microsecondsPerByte * 100000; 
      // keepalive tx interval is 1/4 that, and keepalive rx interval is 1/2 that, 
      keepAliveTxInterval = retryAbsMaxInterval >> 2;
      keepAliveRxInterval = retryAbsMaxInterval >> 1; 
    }

    void begin(void){
      impl->begin(baudrate);
      crc16_generate_table();
    }

    // ---------------------------------------------------- Reading from Stash 
    
    boolean clearToRead(void){
      return (incomingMessageStashLen > 0);
    }

    size_t read(uint8_t* dest, size_t maxLen){
      if(!clearToRead()) return 0; 
      // ok the app is finally reading, we will want to ack this, 
      // can finally swap these around, and make sure we get an ack queued up, 
      ackingSequenceNum = incomingMessageSequenceNum; 
      ackRequired = true; 
      // guard them lengths, 
      if(maxLen > MUDL_MAX_PACKET_SIZE) maxLen = MUDL_MAX_PACKET_SIZE;
      if(incomingMessageStashLen > maxLen) incomingMessageStashLen = maxLen;
      // copy it to caller's buffer, return length, reset stash, 
      memcpy(dest, incomingMessageStash, incomingMessageStashLen);
      size_t len = incomingMessageStashLen;
      incomingMessageStashLen = 0; 
      return len; 
    }

    // ---------------------------------------------------- Writing to Stash 

    boolean isOpen(void){
      return (lastEverIncoming + keepAliveRxInterval) > microsBase64();
    }

    boolean clearToSend(void){
      // we can accept a new packet to send if we are totally clear 
      return outgoingMessageStashLen == 0;
    }

    void send(uint8_t* data, size_t len){
      if(!clearToSend()) return; 
      if(len > MUDL_MAX_PACKET_SIZE) len = MUDL_MAX_PACKET_SIZE;
      // then copy it inboard, 
      memcpy(outgoingMessageStash, data, len); 
      outgoingMessageStashLen = len; 
      // and increment our sequence number ! 
      outgoingMessageSequenceNum ++; 
      // and do a little stats-info tracking 
      outgoingMessageStartTime = microsBase64();
    }

    // ---------------------------------------------------- RX End 

    void onPacketRx(void){
      // we have a freshy, 
      uint8_t incomingAckNum = rxBuffer[rxBufferLen - 4];       // transmitters ack
      uint8_t incomingSequenceNum = rxBuffer[rxBufferLen - 3];  // transmitters current send ptr 
      // check acks if we haven't yet cleared our outgoing stash: 
      if(incomingAckNum == outgoingMessageSequenceNum && outgoingMessageStashLen){
        // the ack matches, success, and we haven't reset our transmit states yet, so: 
        // and we're tracking this metric:
        uint32_t totalTransmitTime = microsBase64() - outgoingMessageStartTime;
        stats.averageTotalTransmitTime = stats.averageTotalTransmitTime * 0.99F + (float)totalTransmitTime * 0.01F;
        // we also want to know what the wire time alone should've been, 
        uint32_t wireTime = outgoingMessageStashLen * microsecondsPerByte;
        stats.averageWireTime = stats.averageWireTime * 0.99F + (float)wireTime * 0.01F;
        // and how many retries we are taking on average, 
        // nvm we can calc this just when asked, 
        // stats.averageRetryCount = stats.averageRetryCount * 0.99F + (float)outgoingRetryCount * 0.01F;
        // now we can reset our states 
        stats.txSuccessCount ++;
        resetAllOutgoingStates();
      }
      // check incoming message, 
      if(rxBufferLen > 4){
        if(incomingMessageSequenceNum == incomingSequenceNum){
          // it must be a retransmission... ?
          if(incomingMessageStashLen){
            // noop, we are still waiting to read it, 
          } else {
            // if we are being re-transmitted to, we should re-ack,
            // retransmitReAcks ++;
            ackRequired = true; 
          }
        } else {
          // it's a new message, let's copy it into our hold-on-to-that-buffer, 
          incomingMessageSequenceNum = incomingSequenceNum; 
          incomingMessageStashLen = rxBufferLen - 4; 
          memcpy(incomingMessageStash, rxBuffer, incomingMessageStashLen); 
        }
      }
    }

    // ---------------------------------------------------- TX End 

    void loadTxBufferWithAck(void){
      // we're stashing at an offset, 
      static const uint8_t preEncodeOffset = 10; 
      txBuffer[0 + preEncodeOffset] = ackingSequenceNum;
      txBuffer[1 + preEncodeOffset] = outgoingMessageSequenceNum;
      uint16_t crc = crc16_ccitt(txBuffer + preEncodeOffset, 2);
      txBuffer[2 + preEncodeOffset] = crc >> 8;   // MSB at len[0] 
      txBuffer[3 + preEncodeOffset] = crc & 255;  // LSB at len[1] 
      // so that we can encode from-to without sucking *even more* memory 
      size_t encodedLen = mudl_cobsEncode(txBuffer + preEncodeOffset, 4, txBuffer);
      txBuffer[encodedLen] = 0;
      txBufferLen = encodedLen + 1;
      txBufferRp = 0; 
      // no timeouts for this one, yall 
    }

    void loadTxBufferFromStash(void){
      // we can append sequence nums to the tail of the message buffer, 
      outgoingMessageStash[outgoingMessageStashLen] = ackingSequenceNum;
      outgoingMessageStash[outgoingMessageStashLen + 1] = outgoingMessageSequenceNum; 
      uint16_t crc = crc16_ccitt(outgoingMessageStash, outgoingMessageStashLen + 2);
      outgoingMessageStash[outgoingMessageStashLen + 2] = crc >> 8;   // MSB at len[0] 
      outgoingMessageStash[outgoingMessageStashLen + 3] = crc & 255;  // LSB at len[1] 
      // we can encode on the way over,
      size_t encodedLen = mudl_cobsEncode(outgoingMessageStash, outgoingMessageStashLen + 4, txBuffer);
      // stuffing our own delimiter, and send it by setting the length flag, 
      txBuffer[encodedLen] = 0;
      txBufferLen = encodedLen + 1;
      txBufferRp = 0; 
      // and we update our timeout interval 
      outgoingTimeoutLength = txTimeoutGenerator();
    }

    void resetAllOutgoingStates(void){
      outgoingMessageStashLen = 0; 
      outgoingLastTxTime = 0; 
      outgoingRetryCount = 0; 
    }

    // ---------------------------------------------------- LOOP 

    void loop(void){
      // ---------------------------------------- RX LOOP 
      // we'll go a-catching all the time: 
      while(impl->available()){
        rxBuffer[rxBufferWp ++] = impl->read();
        if(rxBufferWp >= MUDL_BUFFERS_SIZE * 2) rxBufferWp = 0; 
        if(rxBuffer[rxBufferWp - 1] == 0){
          // nicely, we can decode COBS in place, 
          size_t len = mudl_cobsDecode(rxBuffer, 255, rxBuffer);
          // rm'ing the trailing zero, 
          rxBufferLen = len - 1;
          // calculate the crc on the packet (less the crc itself), and compare to the crc reported: 
          uint16_t crc = crc16_ccitt(rxBuffer, rxBufferLen - 2);
          uint16_t tx_crc = ((uint16_t)(rxBuffer[rxBufferLen - 2]) << 8) | rxBuffer[rxBufferLen - 1];
          // pass / fail based on crc, 
          if(crc == tx_crc){
            stats.rxSuccessCount ++;
            lastEverIncoming = microsBase64(); 
            onPacketRx();
          } else {
            stats.rxFailureCount ++;
          }
          // every time we hit the zero, we reset these
          rxBufferLen = 0; 
          rxBufferWp = 0; 
        }
      } // end rx-while-available
      // digitalWrite(PIN_LED_R, LOW);

      // ---------------------------------------- TX LOOP 
      // firstly we should load the buffer if it is available to write, 
      if(!txBufferLen){
        uint32_t now = microsBase64(); 
        // first priority should be getting messages out, and checking re-transmits, 
        if(outgoingMessageStashLen && outgoingLastTxTime == 0){
          // it's the initial tx, 
          outgoingLastTxTime = now; 
          loadTxBufferFromStash(); 
          ackRequired = false; 
          lastEverOutgoing = now;
        } else if (outgoingLastTxTime && ((now - outgoingLastTxTime) > outgoingTimeoutLength)){
          stats.txTotalRetries ++; // just stats 
          outgoingRetryCount ++;
          if(outgoingRetryCount > MUDL_MAX_NUM_RETRIES){
            // bail bail bail, reset everything, 
            stats.txFailureCount ++; 
            resetAllOutgoingStates(); 
          } else {
            // setup to retry, 
            outgoingLastTxTime = now;
            loadTxBufferFromStash();
            ackRequired = false; 
            lastEverOutgoing = now;
          }
        } else if (ackRequired){
          loadTxBufferWithAck();
          ackRequired = false;
          lastEverOutgoing = now;
        } else if (lastEverOutgoing + keepAliveTxInterval < now) {
          loadTxBufferWithAck();
          // retransmitReAcks ++;
          ackRequired = false; 
          lastEverOutgoing = now; 
        }
      } // end if-no-tx-buffer, 

      // now if we've loaded the buffer, or was previously loaded, sendy 
      if(txBufferLen){
        noInterrupts();
        size_t fifoAvail = impl->availableForWrite();
        for(size_t i = 0; i < fifoAvail; i ++){
          // on RP2040, this line sometimes causes hangup ? 
          impl->write(txBuffer[txBufferRp ++]);
          if(txBufferRp >= txBufferLen){
            // tx'ing is done, this all resets, 
            txBufferRp = 0;
            txBufferLen = 0; 
            break; 
          }
        }
        interrupts();
      }
    } // end loop() 

    // ---------------------------------------------------- Backoff Generator 
    
    uint32_t txTimeoutGenerator(){
      // so i.e. tcp-ip does exponential backoff, 
      // we have a base time, we can just multiply that by some factor each backoff, 
      // we use a multiple of ... 
      uint32_t retryTime = MUDL_RETRY_INITIAL_MULTIPLE * ((txBufferLen + 1) * microsecondsPerByte); 
      //... 
      for(uint8_t i = 0; i < outgoingRetryCount; i ++){
        retryTime = retryTime * MUDL_RETRY_EXP_BASE; 
        if(retryTime > retryAbsMaxInterval) retryTime = retryAbsMaxInterval;
      }
      // stats for now, 
      if(retryTime > stats.outgoingTimeoutLengthHighWaterMark) stats.outgoingTimeoutLengthHighWaterMark = retryTime;
      return retryTime;
    }

    // ---------------------------------------------------- Stats Getter 

    MUDLStats getStats(void){
      // recalc just here, 
      // could probably also i.e. accumulate uint64_t of the other two and do some divs here ? idk 
      stats.averageRetryCount = (float)stats.txTotalRetries / (float)(stats.txSuccessCount + stats.txFailureCount);
      return stats; 
    }

  private: 
    SerialImplementation* impl; 
    // we've got some parameters babey 
    uint32_t baudrate; 
    uint32_t microsecondsPerByte;
    uint32_t retryAbsMaxInterval; // in us, calcualted by baudrate 
    uint32_t keepAliveTxInterval; 
    uint32_t keepAliveRxInterval;
    // and we have stateful packet interface / separation, 
    // so when people .write() or .read() to this they are interacting with these, 
    uint8_t incomingMessageStash[MUDL_BUFFERS_SIZE];
    uint8_t incomingMessageStashLen = 0; 
    uint8_t incomingMessageSequenceNum = 0;   // sequence num. of msg we are waiting for app to read 
    uint8_t ackingSequenceNum = 0;            // latest sequence num. of msg we have red
    bool ackRequired = false; 
    uint8_t outgoingMessageStash[MUDL_BUFFERS_SIZE];
    uint8_t outgoingMessageStashLen = 0; 
    uint8_t outgoingMessageSequenceNum = 12;  // seq num of our own, incremented every new .send() 
    // for retries, we count
    uint64_t outgoingLastTxTime = 0;          // in us 
    uint32_t outgoingTimeoutLength = 0;       // in us 
    uint8_t outgoingRetryCount = 0; 
    // for keepalive, we count
    uint64_t lastEverOutgoing = 0;            // in us 
    uint64_t lastEverIncoming = 0; 
    // for tx/rx machines, 
    // buffers, write/read-pointers, lengths 
    uint8_t rxBuffer[MUDL_BUFFERS_SIZE];
    uint16_t rxBufferWp = 0;
    uint16_t rxBufferLen = 0;
    // and tx, 
    uint8_t txBuffer[MUDL_BUFFERS_SIZE];
    uint8_t txBufferRp = 0;
    uint8_t txBufferLen = 0;
    // some stats yall, a lotta stats
    MUDLStats stats; 
    uint64_t outgoingMessageStartTime = 0; 
};

#endif 