// -----------------------------------------------------------------------------
// Copyright (C) 2023 David Hansel
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
// -----------------------------------------------------------------------------
//
// JiffyDOS
//
// These are IECBusHandler member functions living in their own translation
// unit. Everything they need from the bus handler -- the timer macros, the
// inline pin accessors and the state flags -- comes from
// IECBusHandlerInternal.h.
// -----------------------------------------------------------------------------

//
// https://github.com/MEGA65/open-roms/blob/master/doc/Protocol-JiffyDOS.md
// http://www.nlq.de/
// http://www.baltissen.org/newhtm/sourcecodes.htm
// https://www.amigalove.com/viewtopic.php?t=1734
// https://ar.c64.org/rrwiki/images/4/48/The_Transactor_Vol09_03_1989_Feb_JD_review.pdf
// https://web.archive.org/web/20090826145226/http://home.arcor.de/jochen.adler/ajnjil-t.htm
// https://web.archive.org/web/20220423162959/https://sites.google.com/site/h2obsession/CBM/C128/JiffySoft128
// https://web.archive.org/web/20090211031551/http://hem.passagen.se/harlekin/jiffy1.doc
// https://www.c64-wiki.com/wiki/SJLOAD
// https://github.com/mist64/cbmbus_doc/blob/cb021f3454b499c579c265859ce67ba99e85652b/7%20JiffyDOS.md
// https://ar.c64.org/rrwiki/images/4/48/The_Transactor_Vol09_03_1989_Feb_JD_review.pdf
// https://c65gs.blogspot.com/2023/10/hardware-accelerated-iec-serial.html?m=0
// https://c65gs.blogspot.com/2023/12/hardware-accelerated-iec-serial.html?m=0
// https://c65gs.blogspot.com/2024/01/hardware-accelerated-iec-serial-bus.html?m=0
// https://c65gs.blogspot.com/2024/01/hardware-accelerated-iec-controller.html?m=0
//

// https://retrocomputing.stackexchange.com/questions/14071/what-are-my-options-for-fast-bidirectional-transfer-between-a-c64-and-a-1541?rq=1
//
// ltransferbyte:
//     nop     ; timing critical section
//     nop
//     nop
//     nop
//     lda #$03
//     ldx #$23
//     stx $dd00   ; data=active,clock=inactive,ATN=inactive
//     bit $dd00
//     bvc lloadinnerloop  ; branch if 1541 sets clock active (needs to load next block)
//     nop
//     sta $dd00   ; set data inactive
//     lda $dd00   ; read bits 1/0
//     nop
//     lsr
//     lsr
//     eor $dd00   ; read bits 3/2
//     bit $00     ; burn cycles
//     lsr
//     lsr
//     eor $dd00   ; read bits 5/4
//     bit $00     ; burn cycles
//     lsr
//     lsr
//     eor $dd00   ; read bits 7/6
//     eor #$03
//     sta ($ae),y ; store byte
//     inc $ae     ; load address lo
//     bne ltransferbyte
//     inc $af     ; load address hi
//     jmp ltransferbyte
//

#include "../IECBusHandlerInternal.h"

#ifdef IEC_FP_JIFFY

// ------------------------------------  JiffyDos support routines  ------------------------------------  

bool RAMFUNC(IECBusHandler::receiveJiffyByte)(bool canWriteOk)
{
  uint8_t data = 0;
  JDEBUG1();
  timer_init();
  timer_reset();

  noInterrupts(); 

  // signal "ready" by releasing DATA
  writePinDATA(HIGH);

  // wait (indefinitely) for either CLK high ("ready-to-send") or ATN low
  // NOTE: this must be in a blocking loop since the sender starts transmitting
  // the byte immediately after setting CLK high. If we exit the "task" function then
  // we may not get back here in time to receive.
  while( !digitalReadFastExtIEC(m_pinCLK, m_regCLKread, m_bitCLK) && digitalReadFastExtIEC(m_pinATN, m_regATNread, m_bitATN) )
#ifdef ESP_PLATFORM
    if( !timer_less_than(IWDT_FEED_TIME) )
      {
        // briefly enable interrupts to "feed" the WDT, otherwise we'll get re-booted
        interrupts(); noInterrupts();
        timer_reset();
      }
#else
    {}
#endif

  // start timer (on AVR, lag from CLK high to timer start is between 700...1700ns)
  timer_start();
  JDEBUG0();

  // abort if ATN low
  if( !readPinATN() )
    { interrupts(); return false; }

  // bits 4+5 are set by sender 11 cycles after CLK HIGH (FC51)
  // wait until 14us after CLK
  timer_wait_until(14);
  
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(4);
  if( !readPinDATA() ) data |= bit(5);
  JDEBUG0();

  // bits 6+7 are set by sender 24 cycles after CLK HIGH (FC5A)
  // wait until 27us after CLK
  timer_wait_until(27);
  
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(6);
  if( !readPinDATA() ) data |= bit(7);
  JDEBUG0();

  // bits 3+1 are set by sender 35 cycles after CLK HIGH (FC62)
  // wait until 38us after CLK
  timer_wait_until(38);

  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(3);
  if( !readPinDATA() ) data |= bit(1);
  JDEBUG0();

  // bits 2+0 are set by sender 48 cycles after CLK HIGH (FC6B)
  // wait until 51us after CLK
  timer_wait_until(51);

  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(2);
  if( !readPinDATA() ) data |= bit(0);
  JDEBUG0();

  // sender sets EOI status 61 cycles after CLK HIGH (FC76)
  // wait until 64us after CLK
  timer_wait_until(64);

  // if CLK is high at this point then the sender is signaling EOI
  JDEBUG1();
  bool eoi = readPinCLK();

  // acknowledge receipt
  writePinDATA(LOW);

  // sender reads acknowledgement 80 cycles after CLK HIGH (FC82)
  // wait until 83us after CLK
  timer_wait_until(83);

  JDEBUG0();

  interrupts();

  if( canWriteOk )
    {
      // pass received data on to the device
      m_currentDevice->write(data, eoi);
    }
  else
    {
      // canWrite() reported an error
      return false;
    }
  
  return true;
}


bool RAMFUNC(IECBusHandler::transmitJiffyByte)(uint8_t numData)
{
  uint8_t data = numData>0 ? m_currentDevice->peek() : 0;

  JDEBUG1();
  timer_init();
  timer_reset();

  noInterrupts();

  // signal "READY" by releasing CLK
  writePinCLK(HIGH);
  
  // wait (indefinitely) for either DATA high ("ready-to-receive", FBCB) or ATN low
  // NOTE: this must be in a blocking loop since the receiver receives the data
  // immediately after setting DATA high. If we exit the "task" function then
  // we may not get back here in time to transmit.
  while( !digitalReadFastExtIEC(m_pinDATA, m_regDATAread, m_bitDATA) && digitalReadFastExtIEC(m_pinATN, m_regATNread, m_bitATN) )
#ifdef ESP_PLATFORM
    if( !timer_less_than(IWDT_FEED_TIME) )
      {
        // briefly enable interrupts to "feed" the WDT, otherwise we'll get re-booted
        interrupts(); noInterrupts();
        timer_reset();
      }
#else
    {}
#endif

  // start timer (on AVR, lag from DATA high to timer start is between 700...1700ns)
  timer_start();
  JDEBUG0();

  // abort if ATN low
  if( !readPinATN() )
    { interrupts(); return false; }

  writePinCLK(data & bit(0));
  writePinDATA(data & bit(1));
  JDEBUG1();
  // bits 0+1 are read by receiver 16 cycles after DATA HIGH (FBD5)

  // wait until 16.5 us after DATA
  timer_wait_until(16.5);
  
  JDEBUG0();
  writePinCLK(data & bit(2));
  writePinDATA(data & bit(3));
  JDEBUG1();
  // bits 2+3 are read by receiver 26 cycles after DATA HIGH (FBDB)

  // wait until 27.5 us after DATA
  timer_wait_until(27.5);

  JDEBUG0();
  writePinCLK(data & bit(4));
  writePinDATA(data & bit(5));
  JDEBUG1();
  // bits 4+5 are read by receiver 37 cycles after DATA HIGH (FBE2)

  // wait until 39 us after DATA
  timer_wait_until(39);

  JDEBUG0();
  writePinCLK(data & bit(6));
  writePinDATA(data & bit(7));
  JDEBUG1();
  // bits 6+7 are read by receiver 48 cycles after DATA HIGH (FBE9)

  // wait until 50 us after DATA
  timer_wait_until(50);
  JDEBUG0();
      
  // numData:
  //   0: no data was available to read (error condition, discard this byte)
  //   1: this was the last byte of data
  //  >1: more data is available after this
  if( numData>1 )
    {
      // CLK=LOW  and DATA=HIGH means "at least one more byte"
      writePinCLK(LOW);
      writePinDATA(HIGH);
    }
  else
    {
      // CLK=HIGH and DATA=LOW  means EOI (this was the last byte)
      // CLK=HIGH and DATA=HIGH means "error"
      writePinCLK(HIGH);
      writePinDATA(numData==0);
    }

  // EOI/error status is read by receiver 59 cycles after DATA HIGH (FBEF)
  timer_wait_until(61);

  JDEBUG1();
  if( numData==1 )
    {
      writePinDATA(HIGH);   // make sure DATA is released after signaling EOI
      timer_wait_until(65); // give it time to settle
    }

  // receiver signals "done" by pulling DATA low (FBF2)
  // 63 cycles after initial DATA HIGH (FBF2)
  if( !waitPinDATA(LOW) ) { interrupts(); return false; }
  JDEBUG0();

  interrupts();

  if( numData>0 )
    {
      // success => discard transmitted byte (was previously read via peek())
      m_currentDevice->read();
      return true;
    }
  else
    return false;
}


bool RAMFUNC(IECBusHandler::transmitJiffyBlock)(uint8_t *buffer, uint8_t numBytes)
{
  JDEBUG1();
  timer_init();

  // wait (indefinitely) until receiver is not holding DATA low anymore (FB07)
  // NOTE: this must be in a blocking loop since the receiver starts counting
  // up the EOI timeout immediately after setting DATA HIGH. If we had exited the 
  // "task" function then it might be more than 200us until we get back here
  // to pull CLK low and the receiver will interpret that delay as EOI.
  while( !readPinDATA() )
    if( !readPinATN() )
      { JDEBUG0(); return false; }

  // receiver will be in "new data block" state at this point,
  // waiting for us (FB0C) to release CLK
  if( numBytes==0 )
    {
      // nothing to send => signal EOI by keeping DATA high
      // and pulsing CLK high-low
      writePinDATA(HIGH);
      writePinCLK(HIGH);
      if( !waitTimeout(100) ) return false;
      writePinCLK(LOW);
      if( !waitTimeout(100) ) return false;
      JDEBUG0(); 
      return false;
    }

  // signal "ready to send" by pulling DATA low and releasing CLK
  writePinDATA(LOW);
  writePinCLK(HIGH);

  // delay to make sure receiver has seen DATA=LOW - even though receiver 
  // is in a tight loop (at FB0C), a VIC "bad line" may steal 40-50us.
  if( !waitTimeout(60) ) return false;

  noInterrupts();

  for(uint8_t i=0; i<numBytes; i++)
    {
      uint8_t data = buffer[i];

      // release DATA
      writePinDATA(HIGH);

      // stop and reset timer
      timer_stop();
      timer_reset();

      // signal READY by releasing CLK
      writePinCLK(HIGH);

      // make sure DATA has settled on HIGH
      // (receiver takes at least 19 cycles between seeing DATA HIGH [at FB3E] and setting DATA LOW [at FB51]
      // so waiting a couple microseconds will not hurt transfer performance)
      delayMicrosecondsISafe(2);

      // wait (indefinitely) for either DATA low (FB51) or ATN low
      // NOTE: this must be in a blocking loop since the receiver receives the data
      // immediately after setting DATA high. If we exit the "task" function then
      // we may not get back here in time to transmit.
      while( digitalReadFastExtIEC(m_pinDATA, m_regDATAread, m_bitDATA) && digitalReadFastExtIEC(m_pinATN, m_regATNread, m_bitATN) )
#ifdef ESP_PLATFORM
        if( !timer_less_than(IWDT_FEED_TIME) )
          {
            // briefly enable interrupts to "feed" the WDT, otherwise we'll get re-booted
            interrupts(); noInterrupts();
            timer_reset();
          }
#else
        {}
#endif

      // start timer (on AVR, lag from DATA low to timer start is between 700...1700ns)
      timer_start();
      JDEBUG0();
      
      // abort if ATN low
      if( !readPinATN() )
        { interrupts(); return false; }

      // receiver expects to see CLK high at 4 cycles after DATA LOW (FB54)
      // wait until 6 us after DATA LOW
      timer_wait_until(6);

      JDEBUG0();
      writePinCLK(data & bit(0));
      writePinDATA(data & bit(1));
      JDEBUG1();
      // bits 0+1 are read by receiver 16 cycles after DATA LOW (FB5D)

      // wait until 17 us after DATA LOW
      timer_wait_until(17);
  
      JDEBUG0();
      writePinCLK(data & bit(2));
      writePinDATA(data & bit(3));
      JDEBUG1();
      // bits 2+3 are read by receiver 26 cycles after DATA LOW (FB63)

      // wait until 27 us after DATA LOW
      timer_wait_until(27);

      JDEBUG0();
      writePinCLK(data & bit(4));
      writePinDATA(data & bit(5));
      JDEBUG1();
      // bits 4+5 are read by receiver 37 cycles after DATA LOW (FB6A)

      // wait until 39 us after DATA LOW
      timer_wait_until(39);

      JDEBUG0();
      writePinCLK(data & bit(6));
      writePinDATA(data & bit(7));
      JDEBUG1();
      // bits 6+7 are read by receiver 48 cycles after DATA LOW (FB71)

      // wait until 50 us after DATA LOW
      timer_wait_until(50);
    }

  // signal "not ready" by pulling CLK LOW
  writePinCLK(LOW);

  // release DATA
  writePinDATA(HIGH);

  interrupts();

  JDEBUG0();

  return true;
}


#endif // IEC_FP_JIFFY

