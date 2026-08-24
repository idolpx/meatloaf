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
// DreamLoad. Ported from sd2iec's fl-dreamload.c and its AVR assembly
// (dreamload_get_byte / dreamload_send_byte). GPL v2, Ingo Korb.
//
// A block server: after the M-E the computer sends a two-byte job code (track,
// sector) whenever it wants something, and the drive answers with a status
// byte, 256 data bytes and an XOR checksum. Track 0 is not a track but a
// command -- sector 0 ends the session, sector 1 asks for the first block of
// the directory, sector 2 means "go idle".
//
// Neither direction has absolute timing. Receiving takes a bit per CLK edge,
// most significant first; sending puts two bits on CLK and DATA per ATN
// transition. Both invert.
//
// WHY THIS IS POLLED, when sd2iec refuses to build DreamLoad without a CLK
// interrupt: its main loop keeps doing other work while a loader is resident,
// so a job code can arrive while it is busy and must be latched by an ISR.
// Here the loader owns the bus outright and does nothing between finishing one
// block and waiting for the next code -- the sector read happens AFTER the
// code has been received, never while one could arrive -- so the wait for that
// code IS the idle loop and polling it catches every edge an ISR would. If a
// job code is ever observed being missed, an ISR on the CLK pin for the
// duration of the loader is the fix, and this comment is where to start.
//
// The old protocol variant differs only in signalling on ATN rather than CLK,
// and is told apart by the checksum of the second-stage code the computer
// uploads, exactly as sd2iec does it.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

#if defined(IEC_FP_DREAMLOAD) && defined(IEC_IMPL_SOFTLOAD)

bool IECBusHandler::dreamloadWait(bool atnNotCLK, bool state)
{
  while( (atnNotCLK ? readPinATN() : readPinCLK()) != state )
    {
      if( !isResetPinIdle() ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        { interrupts(); noInterrupts(); timer_reset(); }
#endif
    }

  return true;
}


// One bit per CLK edge, most significant first, inverted at the end.
bool RAMFUNC(IECBusHandler::receiveDreamloadByte)(uint8_t &data)
{
  uint8_t v = 0;

  timer_init();
  timer_reset();
  timer_start();

  for(uint8_t i=0; i<4; i++)
    {
      if( !dreamloadWait(false, LOW) ) return false;
      v <<= 1;
      if( readPinDATA() ) v |= 1;

      if( !dreamloadWait(false, HIGH) ) return false;
      v <<= 1;
      if( readPinDATA() ) v |= 1;
    }

  data = (uint8_t) ~v;
  return true;
}


// Two bits per ATN transition: CLK takes the low bit, DATA the next one.
bool RAMFUNC(IECBusHandler::transmitDreamloadByte)(uint8_t value)
{
  uint8_t v = (uint8_t) ~value;

  timer_init();
  timer_reset();
  timer_start();

  for(uint8_t i=0; i<2; i++)
    {
      writePinCLK (!(v & 1));
      writePinDATA(!(v & 2));
      v >>= 2;
      if( !dreamloadWait(true, LOW) ) return false;

      writePinCLK (!(v & 1));
      writePinDATA(!(v & 2));
      v >>= 2;
      if( !dreamloadWait(true, HIGH) ) return false;
    }

  return true;
}


bool IECBusHandler::dreamloadSendBlock(const uint8_t *data)
{
  uint8_t checksum = 0;
  for(uint16_t i=0; i<256; i++) checksum ^= data[i];

  noInterrupts();

  bool ok = transmitDreamloadByte(0);          // status: no error
  for(uint16_t i=0; i<256 && ok; i++)
    ok = transmitDreamloadByte(data[i]);
  if( ok ) ok = transmitDreamloadByte(checksum);

  writePinCLK(HIGH);
  writePinDATA(HIGH);

  interrupts();
  return ok;
}


bool IECBusHandler::runDreamloadLoader()
{
  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  writePinCLK(HIGH);
  writePinDATA(HIGH);

  if( !dreamloadWait(false, HIGH) )
    { setATNInterruptEnabled(atnInterruptWasEnabled); return true; }

  // The computer now uploads its final drive code over the fast protocol --
  // 1024 bytes, which are not kept. Their XOR tells the two protocol variants
  // apart: 0xAC or 0xDC is the old one, which signals on ATN instead of CLK.
  uint8_t type = 0;
  bool ok = true;
  noInterrupts();
  for(uint16_t i=0; i<4*256 && ok; i++)
    {
      uint8_t b;
      ok = receiveDreamloadByte(b);
      type ^= b;
    }
  interrupts();

  bool oldProtocol = (type==0xAC || type==0xDC);

  while( ok )
    {
      // Wait for a job code. This is the idle loop -- see the note at the top
      // of this file about why polling is enough here.
      if( !dreamloadWait(oldProtocol, LOW) ) break;

      uint8_t track, sector;
      noInterrupts();
      ok = receiveDreamloadByte(track) && receiveDreamloadByte(sector);
      interrupts();
      if( !ok ) break;

      if( track==0 )
        {
          if( sector==0 )
            break;                    // end of session

          if( sector==1 )
            {
              // The first block of the current directory. sd2iec reads this
              // from its own directory handle; the standard CBM directory
              // block is the only answer available here, and it is the right
              // one for the D64s DreamLoad targets.
              if( m_currentDevice->epyxReadSector(18, 1, m_buffer) )
                ok = dreamloadSendBlock(m_buffer);
            }

          // sector 2 is "go idle" -- nothing to do
          continue;
        }

      if( !m_currentDevice->epyxReadSector(track, sector, m_buffer) )
        continue;

      ok = dreamloadSendBlock(m_buffer);
    }

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}

#endif
