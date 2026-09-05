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
// Hypra-Load (64er Magazin)
//
// These are IECBusHandler member functions living in their own translation
// unit. Everything they need from the bus handler -- the timer macros, the
// inline pin accessors and the state flags -- comes from
// IECBusHandlerInternal.h.
// -----------------------------------------------------------------------------

//
// https://www.64er-magazin.de/SH8506/hypra-load.html
//

#include "../IECBusHandlerInternal.h"

#ifdef IEC_FP_HYPRALOAD

bool RAMFUNC(IECBusHandler::transmitHypraLoadByte)(uint8_t data)
{
  // invert data byte
  data = ~data;

  // wait until computer pulls ATN low
  JDEBUG1();
  waitPinATN(LOW);

  // signal "ready"
  noInterrupts();
  writePinDATA(HIGH);

  // wait (indefinitely) for ATN high
  while( !digitalReadFastExtIEC(m_pinATN, m_regATNread, m_bitATN) )
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
  timer_init();
  timer_reset();
  timer_start();

  JDEBUG0();
  writePinCLK( data & bit(0));
  writePinDATA(data & bit(1));
  JDEBUG1();
  // receiver reads bits 0(CLK) and 1(DATA) 40us after ATN high
  timer_wait_until(45);
  JDEBUG0();
  writePinCLK( data & bit(2));
  writePinDATA(data & bit(3));
  JDEBUG1();
  // receiver reads bits 2(CLK) and 3(DATA) 64us after ATN high
  timer_wait_until(75);
  JDEBUG0();
  writePinCLK( data & bit(4));
  writePinDATA(data & bit(5));
  JDEBUG1();
  // receiver reads bits 4(CLK) and 5(DATA) 89us after ATN high
  timer_wait_until(100);
  timer_reset();
  JDEBUG0();
  writePinCLK( data & bit(6));
  writePinDATA(data & bit(7));
  JDEBUG1();
  // receiver reads bits 6(CLK) and 7(DATA) 113us after ATN high
  timer_wait_until(30); // 130us since ATN
  JDEBUG0();

  // signal "not ready"
  writePinDATA(LOW);
  interrupts();

  return true;
}


bool IECBusHandler::transmitHypraLoadBlock()
{
  // generally, 254 data bytes are read into m_buffer[1..254]
  // buffer[0]=0 serves as a flag to signal whether we're reading the first sector
  // we read one more byte than needed for the sector to see if we need to send
  // another sector after this. The extra byte will be at m_buffer[255] and
  // will not be sent until the next data block
  uint8_t n;
  if( m_buffer[0]==0 )
    {
      // reading first sector:
      // the first two bytes of the file (load address) have already been sent using the
      // regular IEC protocol but the fast loader expects a full 254-byte block. However,
      // it discards the first two butes of data so we can just send any values
      n = m_currentDevice->read(m_buffer+3, 253) + 2;
      m_buffer[0] = 0xFF;
    }
  else
    {
      // get extra byte from previous block
      m_buffer[1] = m_buffer[255];

      // read the next 254 bytes (extra byte will end up in m_buffer[255])
      n = m_currentDevice->read(m_buffer+2, 254) + 1;
    }

  // 0x00 signals success, 0xFF signals error condition
  transmitHypraLoadByte(0x00);

  // transmit first two sector bytes (in 1541 this is track/sector pointer to next data block)
  if( n<255 )
    {
      // there are fewer than 254+1 bytes to send => this is the final sector
      transmitHypraLoadByte(0);   // next track == 0: "final sector"
      transmitHypraLoadByte(n+1); // next sector: position of final valid byte in sector
    }
  else
    {
      transmitHypraLoadByte(1); // next track != 0: "more sectors to follow"
      transmitHypraLoadByte(1); // next sector: not used in receiver
    }

  // transmit sector data bytes (2-256)
  for(uint8_t i=1; i<=254; i++)
    transmitHypraLoadByte(m_buffer[i]);

  // return true if there are more blocks to transmit
  return n==255;
}

#endif
