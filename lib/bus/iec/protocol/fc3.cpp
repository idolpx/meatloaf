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
// Final Cartridge 3 / EXOS
//
// These are IECBusHandler member functions living in their own translation
// unit. Everything they need from the bus handler -- the timer macros, the
// inline pin accessors and the state flags -- comes from
// IECBusHandlerInternal.h.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

// ------------------------------------  Final Cartridge 3 support routines  ------------------------------------


#ifdef IEC_FP_FC3

// necessary for UNO since otherwise the writePinCLK/writePinDATA 
// calls take too long and the timing will be off.
#pragma GCC push_options
#pragma GCC optimize ("O2")

void RAMFUNC(IECBusHandler::transmitFC3Bytes)(uint8_t *data)
{
  // In the following table, the "Cycle" column for PAL/NTSC gives the cycle number
  // counted from the beginning of the "CLK low" detection loop on the C64 (at $9962),
  // i.e.the fastest case where CLK is already low when it is read.
  // The loop itself takes 7 cycles for each iteration which introduces a 7-cycle
  // variation between when each bit may be read.
  //
  // The "read time" column for each bit shows the earliest and latest time after
  // "CLK low" that the C64 may read the given bits. The "write" column is the time
  // at which the NEXT bits should be written and "margin" gives the amount of time
  // after the write time before the bits are read. Note that because of the 7-cycle
  // variation and different timing between NTSC and PAL, the margins are very small.
  // Note that FC3 code has one extra NOP in the receive code on NTSC (before bits 2+3)
  // to (somewhat) balance out the faster clock speed.
  //
  //               ----- PAL -----   ---- NTSC ----  -- read time --
  // Byte | Bits | Cycle |     us | Cycle |    us       min      max | write | margin
  //    1 | 0+1  |  13   |  13.19 |  13   |  12.71 |  12.71 |  20.30 |  20.5 | 4.87
  //    1 | 2+3  |  25   |  25.37 |  27   |  26.40 |  25.37 |  33.24 |  33.5 | 4.05
  //    1 | 4+5  |  37   |  37.55 |  39   |  38.13 |  37.55 |  44.98 |  45.5 | 4.23
  //    1 | 6+7  |  49   |  49.73 |  51   |  49.87 |  49.73 |  56.84 |  57.5 | 6.06
  //    2 | 0+1  |  63   |  63.94 |  65   |  63.56 |  63.56 |  71.05 |  71.5 | 4.62
  //    2 | 2+3  |  75   |  76.12 |  79   |  77.24 |  76.12 |  84.09 |  84.5 | 3.80
  //    2 | 4+5  |  87   |  88.30 |  91   |  88.98 |  88.30 |  95.82 |  96.5 | 3.98
  //    2 | 6+7  |  99   | 100.48 | 103   | 100.71 | 100.48 | 107.59 | 108.5 | 5.90
  //    3 | 0+1  | 113   | 114.69 | 117   | 114.40 | 114.40 | 121.80 | 122.5 | 4.37
  //    3 | 0+1  | 125   | 126.87 | 131   | 128.09 | 126.87 | 134.93 | 135.5 | 3.55
  //    3 | 2+3  | 137   | 139.05 | 143   | 139.82 | 139.05 | 146.67 | 147.5 | 3.73
  //    3 | 4+5  | 149   | 151.23 | 155   | 151.56 | 151.23 | 158.40 | 159.5 | 5.74
  //    4 | 0+1  | 163   | 165.44 | 169   | 165.24 | 165.24 | 172.55 | 173   | 4.62
  //    4 | 2+3  | 175   | 177.62 | 183   | 178.93 | 177.62 | 185.78 | 186   | 3.80
  //    4 | 4+5  | 187   | 189.80 | 195   | 190.67 | 189.80 | 197.51 | 198   | 3.98
  //    4 | 6+7  | 199   | 201.98 | 207   | 202.40 | 201.98 | 209.24 | 210   |

#define FC3_TRANSMIT_BYTE(b, t) \
  JDEBUG0();                    \
  writePinCLK( b & bit(0));     \
  writePinDATA(b & bit(1));     \
  JDEBUG1();                    \
  timer_wait_until(t);          \
  JDEBUG0();                    \
  writePinCLK( b & bit(2));     \
  writePinDATA(b & bit(3));     \
  JDEBUG1();                    \
  timer_wait_until(t+13);       \
  JDEBUG0();                    \
  writePinCLK( b & bit(4));     \
  writePinDATA(b & bit(5));     \
  JDEBUG1();                    \
  timer_wait_until(t+25);       \
  JDEBUG0();                    \
  writePinCLK( b & bit(6));     \
  writePinDATA(b & bit(7));     \
  JDEBUG1();                    \
  timer_wait_until(t+37);

  timer_init();
  timer_reset();

  // signal "ready" by pulling CLK low
  // At this point the receiver is in a fairly tight loop waiting for CLK low (9962-9966)
  // but since each cycle of the loop takes 7 cycles there is up to 7.1us variance in when the
  // loop is exited and therefore when the transmitted data below is actually read.
  writePinCLK(LOW);
  timer_start();

  // make sure the C64 has seen our CLK low
  timer_wait_until(8);

#if defined(__AVR__)
  // On AVR we need to start TRANSMIT_BYTE early because of the slow processor
  // (takes time to get from timer_wait_until() to set the CLK/DATA pins)
#define FC3_OFFSET -1
#elif defined(ARDUINO_ARCH_RP2040)
  // PiPico timer resolution is only 1us, so if we try to wait 10us it may end up 
  // being just slightly more than 9us. However, it is fast so we can wait some
  // extra cycles and still have the CLK/DATA signals updated in time.
#define FC3_OFFSET 1.5
#else
#define FC3_OFFSET 0
#endif

  // transmit four bytes (for timing values see table above)
  FC3_TRANSMIT_BYTE(data[0],  20.5 + FC3_OFFSET);
  FC3_TRANSMIT_BYTE(data[1],  71.5 + FC3_OFFSET);
  FC3_TRANSMIT_BYTE(data[2], 122.5 + FC3_OFFSET);
  FC3_TRANSMIT_BYTE(data[3], 173   + FC3_OFFSET);

  // release CLK
  writePinCLK(HIGH);
  timer_stop();
}


bool RAMFUNC(IECBusHandler::receiveFC3Byte)(uint8_t *pdata)
{
  uint8_t data = 0;
  timer_init();
  timer_reset();

  // wait (indefinitely) for CLK high
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
  timer_start();

  // abort if ATN low
  if( !readPinATN() ) { JDEBUG0(); return false; }
  
  // sender sets bits 7(CLK) and 5(DATA) 12us after CLK high
  timer_wait_until(15);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(7);
  if( !readPinDATA() ) data |= bit(5);
  JDEBUG0();

  // sender sets bits 6(CLK) and 4(DATA) 22us after CLK high
  timer_wait_until(25);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(6);
  if( !readPinDATA() ) data |= bit(4);
  JDEBUG0();
  
  // sender sets bits 3(CLK) and 1(DATA) 38us after CLK high
  timer_wait_until(41);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(3);
  if( !readPinDATA() ) data |= bit(1);
  JDEBUG0();
  
  // sender sets bits 2(CLK) and 0(DATA) 48us after CLK high
  timer_wait_until(51);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(2);
  if( !readPinDATA() ) data |= bit(0);
  JDEBUG0();

  // sender releases DATA and pulls CLK low 58us after CLK high
  if( !waitPinCLK(LOW) ) return false;
  timer_stop();

  *pdata = data;
  return true;
}

#pragma GCC pop_options

int8_t RAMFUNC(IECBusHandler::transmitFC3Block)()
{
  m_inTask = false;
  if( m_buffer[1]==0 )
    {
      // first block => we need only 252 bytes since
      // the first two bytes (load address) were already transmitted via regular 
      // serial protocol. The receiver discards the repeated two bytes so their
      // actual value does not matter.
      // But we do read one more byte to test whether there will be a next block.
      // If there are more blocks to transmit then n must be 0, otherwise it must
      // be one more than the number of data bytes in this block 
      // (n+3 because of the repeated load address)
      uint8_t n = m_currentDevice->read(m_buffer+5, 253);
      m_buffer[2] = (n==253) ? 0 : n+3;
    }
  else
    {
      // second or later block => move the extra data byte that was read before to the beginning
      // and attempt to read 254 more bytes (one byte more than needed for this block).
      // If there is more data (i.e. blocks) to transmit then n must be 0, otherwise it must
      // be one more than the number of data bytes in this block (n+1 because of the extra data byte)
      m_buffer[3] = m_buffer[257];
      uint8_t n = m_currentDevice->read(m_buffer+4, 254);
      m_buffer[2] = (n==254) ? 0 : n+2;
    }

  m_inTask = true;

  // if ATN was asserted then done
  if( m_flags & P_ATN ) return -1;
  
  // signal "ready" by pulling CLK low
  writePinCLK(LOW);
  
  // wait for confirmation (DATA low)
  if( !waitPinDATA(LOW, 0) ) return -1;
  
  // release CLK
  writePinCLK(HIGH);

  // wait for DATA high
  if( !waitPinDATA(HIGH, 0) ) return -1;
  
  noInterrupts();

  // transmit 260 bytes of data (65 x 4 bytes)
  // byte 0: not used by receiver
  // byte 1: block number
  // byte 2: number of valid data bytes in block (0=full block of 254 bytes)
  // byte 3-256: 254 data bytes in block
  // byte 257-259: not used by receiver
  uint8_t *data = m_buffer;
  for(int i=0; i<65; i++)
    {
      // wait to give receiver time to get ready for next data segment
      delayMicrosecondsISafe(150);

      // transmit 4-byte tuple
      transmitFC3Bytes(data);

      // next 4 bytes
      data +=4;
    }

  // release CLK, signal end-of-data by pulling DATA low if this was the last block
  writePinCLK(HIGH);
  writePinDATA(m_buffer[2]==0 ? HIGH : LOW);

  // increment block number
  m_buffer[1]++;

  interrupts();

  // return true if more blocks to transmit
  return m_buffer[2]==0;
}


int8_t RAMFUNC(IECBusHandler::transmitFC3ImageBlock)()
{
  m_inTask = false;
  uint8_t n = m_currentDevice->read(m_buffer+3, 254);
  m_buffer[2] = (n==254) ? 0 : n+1;
  m_inTask = true;
  
  // if no more data available or ATN was asserted then done
  if( n==0 || (m_flags & P_ATN) ) return false;

  noInterrupts();

  // transmit 260 bytes of data (65 x 4 bytes)
  // bytes 0-2: not used by receiver
  // byte 3-256: 254 data bytes in block
  // byte 257-259: not used by receiver
  uint8_t *data = m_buffer;
  for(int i=0; i<65; i++)
    {
      // signal "ready" by pulling CLK low
      writePinCLK(LOW);
      
      // wait for confirmation (DATA low)
      if( !waitPinDATA(LOW, 0) ) { interrupts(); return -1; }
      
      // release CLK
      writePinCLK(HIGH);
      
      // wait for DATA high
      if( !waitPinDATA(HIGH, 0) ) { interrupts(); return -1; }
  
      // transmit 4-byte tuple
      transmitFC3Bytes(data);

      // release CLK and DATA
      writePinCLK(HIGH);
      writePinDATA(HIGH);

      // next 4 bytes
      data +=4;
    }

  interrupts();

  // return 1 if more blocks to transmit, otherwise 0
  return m_buffer[2]==0 ? 1 : 0;
}


int8_t RAMFUNC(IECBusHandler::receiveFC3Block)()
{
  noInterrupts();

  // signal "ready"
  writePinDATA(HIGH);
  
  // receive block data length
  uint8_t len;
  if( !receiveFC3Byte(&len) ) { interrupts(); return -1; }

  // receive block data
  uint8_t n = len==0 ? 254 : len-1;
  for(uint8_t i=0; i<n; i++)
    if( !receiveFC3Byte(m_buffer+i) ) 
      { interrupts(); return -1; }

  // signal "not ready"
  writePinDATA(LOW);
  
  interrupts();

  // len>0 signals that this was the last data block (EOI)
  bool eoi = len>0;

  // send data to device
  m_inTask = false;
  bool ok = m_currentDevice->write(m_buffer, n, eoi)==n;
  m_inTask = true;

  if( ok )
    return eoi ? 0 : 1;
  else
    return -1;
}


#endif
