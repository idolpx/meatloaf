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
// Nippon. Ported from sd2iec's fl-nippon.c (Ingo Korb, GPL v2).
//
// Unlike every other loader here Nippon does not ask for a file -- it asks for
// a BLOCK. After the M-E it sits in an idle loop taking a track and a sector
// from the computer and either sending that block or receiving one to write,
// which is why it needs the epyxReadSector/epyxWriteSector hooks rather than
// the file interface. A track byte with bit 7 set ends the session.
//
// Bit 7 of the sector byte selects the direction: set means the computer wants
// to read the block, clear means it is about to write one. The sector number
// itself is the low seven bits.
//
// ATN is part of the bit protocol here, not an interrupt: the computer asserts
// it to say "resync", and every handshake checks it. That is also what makes
// the loader recoverable -- a byte that goes out of step aborts back to the
// idle loop rather than corrupting a block.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

#if defined(IEC_FP_NIPPON) && defined(IEC_IMPL_SOFTLOAD)

// sd2iec leaves the idle loop when a button on the device is pressed. There is
// no equivalent here, so the escape is the RESET line dropping -- ATN cannot
// serve as one, because Nippon uses it as a protocol signal.
bool RAMFUNC(IECBusHandler::nipponAbort)()
{
  return !isResetPinIdle();
}


// Wait for "pin" to reach "state", feeding the watchdog. Returns false if the
// wait was given up on.
bool RAMFUNC(IECBusHandler::nipponWait)(bool clkNotAtn, bool state)
{
  while( (clkNotAtn ? readPinCLK() : readPinATN()) != state )
    {
      if( nipponAbort() ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        {
          interrupts(); noInterrupts();
          timer_reset();
        }
#endif
    }

  return true;
}


// One bit of handshake. Returns false when the computer has asserted ATN,
// which means the two ends are out of step and the transfer must be abandoned.
bool RAMFUNC(IECBusHandler::nipponHandshake)()
{
  if( readPinATN() )
    return nipponWait(true, LOW);   // ATN released: wait for CLK low

  // ATN asserted: acknowledge with CLK low and wait for it to be released.
  // The wait's result is deliberately dropped -- this returns false either
  // way, because ATN here means "out of step" and the byte is lost whether the
  // computer releases ATN or the device is being reset. A reset that lands
  // inside this wait is caught one lap later, by the idle loop's own wait.
  writePinCLK(LOW);
  (void) nipponWait(false, HIGH);
  return false;
}


bool RAMFUNC(IECBusHandler::receiveNipponByte)(uint8_t &data)
{
  uint8_t value = 0;

  timer_init();
  timer_reset();
  timer_start();

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  delayMicrosecondsISafe(3);   // allow for slow rise times

  for(uint8_t i=0; i<8; i++)
    {
      if( !nipponHandshake() ) return false;
      value = (value >> 1) | (readPinDATA() ? 0 : 0x80);
      if( !nipponWait(true, HIGH) ) return false;
    }

  writePinCLK(LOW);
  writePinDATA(HIGH);
  data = value;
  return true;
}


bool RAMFUNC(IECBusHandler::transmitNipponByte)(uint8_t value)
{
  timer_init();
  timer_reset();
  timer_start();

  writePinCLK(HIGH);
  delayMicrosecondsISafe(3);   // allow for slow rise times

  for(uint8_t i=0; i<8; i++)
    {
      if( !nipponHandshake() ) return false;
      writePinDATA(value & 1);
      value >>= 1;
      if( !nipponWait(true, HIGH) ) return false;
    }

  writePinCLK(LOW);
  writePinDATA(HIGH);
  return true;
}


bool IECBusHandler::runNipponLoader()
{
  // The loader owns the bus and reads ATN itself, so the interrupt has to
  // stand down: an atnRequest() here would consume the very signal the
  // protocol is built on.
  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  while( true )
    {
      // idle: both lines released, waiting to be given a command
      writePinDATA(HIGH);
      writePinCLK(HIGH);

      if( !nipponWait(false, LOW) ) break;   // wait for ATN asserted
      writePinCLK(LOW);
      if( !nipponWait(false, HIGH) ) break;  // wait for ATN released

      noInterrupts();

      uint8_t track, sector;
      if( !receiveNipponByte(track) ) { interrupts(); continue; }
      if( track & 0x80 ) { interrupts(); break; }   // exit code

      if( !receiveNipponByte(sector) ) { interrupts(); continue; }

      bool reading = (sector & 0x80)!=0;
      sector &= 0x7F;

      interrupts();

      if( reading )
        {
          if( !m_currentDevice->epyxReadSector(track, sector, m_buffer) )
            continue;

          noInterrupts();
          bool ok = true;
          for(uint16_t i=0; i<256 && ok; i++)
            ok = transmitNipponByte(m_buffer[i]);
          interrupts();
          if( !ok ) continue;
        }
      else
        {
          noInterrupts();
          bool ok = true;
          for(uint16_t i=0; i<256 && ok; i++)
            ok = receiveNipponByte(m_buffer[i]);
          interrupts();
          if( !ok ) continue;

          m_currentDevice->epyxWriteSector(track, sector, m_buffer);
        }
    }

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}

#endif
