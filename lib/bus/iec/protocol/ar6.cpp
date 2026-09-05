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
// Action Replay 6
//
// These are IECBusHandler member functions living in their own translation
// unit. Everything they need from the bus handler -- the timer macros, the
// inline pin accessors and the state flags -- comes from
// IECBusHandlerInternal.h.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

// ------------------------------------  Action Replay 6 support routines  ------------------------------------


#ifdef IEC_FP_AR6
// necessary for UNO since otherwise the writePinCLK/writePinDATA 
// calls take too long and the timing will be off.
#pragma GCC push_options
#pragma GCC optimize ("O2")

bool RAMFUNC(IECBusHandler::transmitAR6Byte)(uint8_t data, bool ar6Protocol)
{
  noInterrupts();
  timer_init();
  timer_reset();

  // release CLK (signal "ready")
  writePinCLK(HIGH);

  // wait (indefinitely) for DATA high
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
  timer_start();

  // abort if ATN low
  if( !readPinATN() ) { interrupts(); return false; }

  if( ar6Protocol )
    {
      // Action Replay 6 protocol

      JDEBUG0();
      writePinCLK( data & bit(0));
      writePinDATA(data & bit(1));
      JDEBUG1();
      // receiver reads bits 0(CLK) and 1(DATA) 10us after DATA high
      timer_wait_until(12);
      JDEBUG0();
      writePinCLK( data & bit(2));
      writePinDATA(data & bit(3));
      JDEBUG1();
      // receiver reads bits 2(CLK) and 3(DATA) 18us after DATA high
      timer_wait_until(20);
      JDEBUG0();
      writePinCLK( data & bit(4));
      writePinDATA(data & bit(5));
      JDEBUG1();
      // receiver reads bits 4(CLK) and 5(DATA) 26us after DATA high
      timer_wait_until(28);
      JDEBUG0();
      writePinCLK( data & bit(6));
      writePinDATA(data & bit(7));
      JDEBUG1();
      // receiver reads bits 4(CLK) and 5(DATA) 34us after DATA high
      timer_wait_until(36);
    }
  else
    {
      // Action Replay 3 protocol (for image loader)
      data = ~data;

      JDEBUG0();
      writePinCLK( data & bit(7));
      writePinDATA(data & bit(5));
      JDEBUG1();
      // receiver reads bits 7(CLK) and 5(DATA) 16us after DATA high
      timer_wait_until(18);
      JDEBUG0();
      writePinCLK( data & bit(6));
      writePinDATA(data & bit(4));
      JDEBUG1();
      // receiver reads bits 6(CLK) and 4(DATA) 26us after DATA high
      timer_wait_until(28);
      JDEBUG0();
      writePinCLK( data & bit(3));
      writePinDATA(data & bit(1));
      JDEBUG1();
      // receiver reads bits 3(CLK) and 1(DATA) 36us after DATA high
      timer_wait_until(38);
      JDEBUG0();
      writePinCLK( data & bit(2));
      writePinDATA(data & bit(0));
      JDEBUG1();
      // receiver reads bits 2(CLK) and 0(DATA) 46us after DATA high
      timer_wait_until(48);
    }

  // pull CLK low ("not ready") and release DATA
  writePinCLK(LOW);
  writePinDATA(HIGH);

  interrupts();

  // receiver pulls DATA low ("not ready") 38us after DATA high
  if( !waitPinDATA(LOW) ) return false;
  timer_stop();

  return true;
}


bool RAMFUNC(IECBusHandler::receiveAR6Byte)(uint8_t *pdata)
{
  uint8_t data = 0;

  noInterrupts();
  timer_init();
  timer_reset();

  // release CLK (signal "ready")
  writePinCLK(HIGH);

  JDEBUG1();
  // wait (indefinitely) for DATA high
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
  timer_start();
  JDEBUG0();

  // abort if ATN low
  if( !readPinATN() ) { interrupts(); return false; }
  
  // sender sets bits 7(CLK) and 5(DATA) 8us after CLK high
  timer_wait_until(11);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(7);
  if( !readPinDATA() ) data |= bit(5);
  JDEBUG0();

  // sender sets bits 6(CLK) and 4(DATA) 18us after CLK high
  timer_wait_until(21);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(6);
  if( !readPinDATA() ) data |= bit(4);
  JDEBUG0();
  
  // sender sets bits 3(CLK) and 1(DATA) 34us after CLK high
  timer_wait_until(37);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(3);
  if( !readPinDATA() ) data |= bit(1);
  JDEBUG0();
  
  // sender sets bits 2(CLK) and 0(DATA) 44us after CLK high
  timer_wait_until(47);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(2);
  if( !readPinDATA() ) data |= bit(0);
  JDEBUG0();

  // signal "not ready"
  writePinCLK(LOW);

  interrupts();

  // sender releases CLK and pulls DATA low 57us after CLK high
  if( !waitPinDATA(LOW) ) return false;
  timer_stop();

  *pdata = data;
  return true;
}

#pragma GCC pop_options


int8_t RAMFUNC(IECBusHandler::transmitAR6Block)(bool ar6Protocol)
{
  uint8_t n;

  // the first two bytes of the file (load address) have already been sent using the
  // regular IEC protocol but the fast loader expects the file to "start over".
  // It discards the first two butes so we don't really have to send the same values.
  if( m_buffer[255]==0 )
    n = m_currentDevice->read(m_buffer+2, 252) + 2;
  else
    n = m_currentDevice->read(m_buffer, 254);

  if( !transmitAR6Byte(n, ar6Protocol) ) return -1;

  for(uint8_t i=0; i<n; i++)
    if( !transmitAR6Byte(m_buffer[i], ar6Protocol) )
      return -1;

  // next block number
  m_buffer[255]++;

  return n==0 ? 0 : 1;
}


int8_t RAMFUNC(IECBusHandler::receiveAR6Block)()
{
  for(uint16_t i=0; i<256; i++)
    if( !receiveAR6Byte(m_buffer+i) )
      return -1;
  
  // first byte of block is number of blocks to receive AFTER this one
  // it that is 0 then second byte is number of valid bytes within this block (+2)
  bool    eoi = m_buffer[0]==0;
  uint8_t n   = eoi ? m_buffer[1]-2 : 254;

  // send data to device
  m_inTask = false;
  bool ok = m_currentDevice->write(m_buffer+2, n, eoi)==n;
  m_inTask = true;

  if( ok )
    return eoi ? 0 : 1;
  else
    return -1;
}

#endif
