// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.
// -----------------------------------------------------------------------------
//
// N0SDOS file read. Ported from sd2iec's fl-n0sdos.c, with the send timing
// from its AVR assembly (n0sdos_send_byte). GPL v2, Ingo Korb.
//
// The two directions are not alike. Receiving a file name is fully
// handshaked -- the computer moves whichever of CLK/DATA carries the bit, we
// acknowledge on the OTHER line, and both are released again -- so it carries
// no timing at all. Sending is clocked by us: after the computer raises CLK,
// bit pairs go out at 9, 17, 25 and 33 us, CLK carrying the even bits.
//
// There is no length and no end marker in the data stream. The loader sends
// 254-byte blocks until the computer raises CLK to say it has enough, and at
// end of file it keeps re-sending the last block rather than stopping -- that
// is what the original does, and the computer is the side that knows when the
// file is complete.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

#if defined(IEC_FP_N0SDOS) && defined(IEC_IMPL_SOFTLOAD)

// Receive one byte, least significant bit first. Returns false when the
// computer asserts ATN, which is how it ends the session.
bool RAMFUNC(IECBusHandler::receiveN0SDOSByte)(uint8_t &data)
{
  uint8_t value = 0;

  timer_init();
  timer_reset();
  timer_start();

  for(uint8_t i=0; i<8; i++)
    {
      // wait for either line to go low -- whichever it is carries the bit
      bool clk, dat;
      while( true )
        {
          if( !readPinATN() || !isResetPinIdle() ) return false;
          clk = readPinCLK();
          dat = readPinDATA();
          if( !clk || !dat ) break;
#ifdef ESP_PLATFORM
          if( !timer_less_than(IWDT_FEED_TIME) )
            { interrupts(); noInterrupts(); timer_reset(); }
#endif
        }

      value >>= 1;
      if( !dat ) value |= 0x80;

      // acknowledge on the line the computer did NOT use
      if( dat ) writePinDATA(LOW); else writePinCLK(LOW);
      delayMicrosecondsISafe(2);

      // wait for the computer to acknowledge in turn, i.e. for at least one
      // line to come back up
      while( true )
        {
          if( !readPinATN() || !isResetPinIdle() ) return false;
          if( readPinCLK() || readPinDATA() ) break;
#ifdef ESP_PLATFORM
          if( !timer_less_than(IWDT_FEED_TIME) )
            { interrupts(); noInterrupts(); timer_reset(); }
#endif
        }

      writePinCLK(HIGH);
      writePinDATA(HIGH);
      delayMicrosecondsISafe(2);
    }

  data = value;
  return true;
}


bool RAMFUNC(IECBusHandler::transmitN0SDOSByte)(uint8_t value)
{
  writePinCLK(HIGH);
  writePinDATA(HIGH);

  timer_init();
  timer_reset();
  timer_start();

  while( !readPinCLK() )
    {
      if( !isResetPinIdle() ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        { interrupts(); noInterrupts(); timer_reset(); }
#endif
    }

  timer_reset();
  timer_start();

  // bit 0 goes to CLK and bit 1 to DATA, and unlike ULoad3 the byte is NOT
  // inverted first -- so here a set bit is an ASSERTED line
  uint32_t t = 9;
  for(uint8_t pair=0; pair<4; pair++)
    {
      timer_wait_until(t);
      writePinCLK (!(value & 1));
      writePinDATA(!(value & 2));
      value >>= 2;
      t += 8;
    }

  // finish with CLK released and DATA asserted
  timer_wait_until(t-3);
  writePinCLK(HIGH);
  writePinDATA(LOW);
  timer_wait_until(t+3);

  return true;
}


bool IECBusHandler::runN0SDOSLoader()
{
  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  writePinCLK(HIGH);
  writePinDATA(LOW);
  delay(10);

  while( readPinATN() )
    {
      // receive a file name of up to 7 characters, terminated by a NUL
      writePinCLK(HIGH);
      writePinDATA(HIGH);
      delayMicrosecondsISafe(2);

      char name[9];
      uint8_t n = 0;
      bool ok = true;
      for(; n<7; n++)
        {
          uint8_t c;
          noInterrupts();
          ok = receiveN0SDOSByte(c);
          interrupts();
          if( !ok ) break;
          if( c==0 ) break;
          name[n] = (char) c;
        }
      if( !ok ) break;

      // a name that fills the field is treated as a prefix
      if( n==7 ) name[n++] = '*';

      writePinCLK(LOW);
      writePinDATA(LOW);

      // open the file on channel 0
      m_currentDevice->listen(0xF0);
      ok = (n>0);
      for(uint8_t i=0; i<n && ok; i++)
        {
          int8_t w;
          while( (w = m_currentDevice->canWrite())<0 )
            if( !isResetPinIdle() ) { ok = false; break; }

          if( ok && w>0 )
            m_currentDevice->write(name[i], i==n-1);
          else
            ok = false;
        }
      m_currentDevice->unlisten();

      m_currentDevice->talk(0);
      m_inTask = false;
      uint8_t got = ok ? m_currentDevice->read(m_buffer, 254) : 0;
      m_inTask = true;

      if( got==0 )
        {
          noInterrupts();
          transmitN0SDOSByte(0xFF);   // no such file
          interrupts();
          continue;
        }

      noInterrupts();
      ok = transmitN0SDOSByte(0x00);  // file is open
      interrupts();
      if( !ok ) break;

      // Stream 254-byte blocks until the computer raises CLK. At end of file
      // the same block is sent again rather than stopping -- see the header
      // comment; the computer is the side that knows when it has enough.
      bool last = (got < 254);
      while( true )
        {
          noInterrupts();
          for(uint8_t i=0; i<254; i++)
            {
              if( readPinCLK() || !readPinATN() ) { ok = false; break; }
              if( !transmitN0SDOSByte(m_buffer[i]) ) { ok = false; break; }
            }
          interrupts();
          if( !ok ) break;

          if( !last )
            {
              m_currentDevice->talk(0);
              m_inTask = false;
              got = m_currentDevice->read(m_buffer, 254);
              m_inTask = true;
              last = (got < 254);
            }
        }

      // close the file -- N0SDOS sends no CLOSE of its own
      m_currentDevice->listen(0xE0);
      m_currentDevice->unlisten();

      if( !readPinATN() ) break;
    }

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}

#endif
