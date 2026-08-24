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
// Final Cartridge 3, older freezed-file loader. Ported from sd2iec's
// fl-fc3exos.c with the bit timing from its AVR assembly
// (fc3_oldfreeze_pal_send / fc3_oldfreeze_ntsc_send). GPL v2, Ingo Korb.
//
// This is a different loader from the FC3 fast load and fast save the
// signature matcher in IECFileDevice already handles -- it is what an older
// cartridge uses to read back a freezed file, and the file it wants is already
// open on channel 0 when the M-E arrives.
//
// PAL and NTSC are two separate uploads with two different CRCs, and they
// differ only in send cadence: pairs at 14/22/30/38 us with the busy signal
// restored at 46 for PAL, and 14/24/34/44 with 52 for NTSC. Which one applies
// comes out of the detection table as the rx/tx variant, not from anything in
// the transfer itself.
//
// The byte is NOT inverted here, unlike ULoad3's: a set bit ASSERTS its line.
// DATA doubles as the busy signal -- asserted means "not ready" -- so it is
// released before each byte and re-asserted after it.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

#if defined(IEC_FP_FC3) && defined(IEC_IMPL_SOFTLOAD)

bool RAMFUNC(IECBusHandler::transmitFC3OldFreezeByte)(uint8_t value, bool ntsc)
{
  // clear the busy signal
  writePinDATA(HIGH);

  timer_init();
  timer_reset();
  timer_start();

  // wait for the start signal: CLK low, then high
  while( readPinCLK() )
    {
      if( !isResetPinIdle() ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        { interrupts(); noInterrupts(); timer_reset(); }
#endif
    }

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

  const uint32_t pal [5] = { 14, 22, 30, 38, 46 };
  const uint32_t ntscT[5] = { 14, 24, 34, 44, 52 };
  const uint32_t *t = ntsc ? ntscT : pal;

  uint8_t v = value;
  for(uint8_t pair=0; pair<4; pair++)
    {
      timer_wait_until(t[pair]);
      writePinCLK (!(v & 1));
      writePinDATA(!(v & 2));
      v >>= 2;
    }

  // hold, then restore the busy signal
  timer_wait_until(t[4]);
  writePinCLK(HIGH);
  writePinDATA(LOW);
  delayMicrosecondsISafe(1);

  // the computer asserting ATN means it wants the bus back
  return readPinATN();
}


bool IECBusHandler::runFC3OldFreezeLoader(uint8_t rxtx)
{
  bool ntsc = (rxtx == IEC_FLRX_FC3OF_NTSC);

  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  // mark ourselves busy. sd2iec also pulls SRQ low here; there is no level
  // control for SRQ at this layer (only sendSRQ(), which pulses) and not every
  // board wires it, so that part is left out.
  writePinCLK(HIGH);
  writePinDATA(LOW);

  // wait up to 100us for the computer to finish its UNLISTEN
  delayMicrosecondsISafe(1);
  uint32_t start = micros();
  while( !readPinCLK() && (micros()-start) < 100 )
    ;

  bool ok = true;
  while( ok )
    {
      m_currentDevice->talk(0);
      m_inTask = false;
      uint8_t n = m_currentDevice->read(m_buffer, 254);
      m_inTask = true;

      if( n==0 ) break;

      noInterrupts();
      for(uint8_t i=0; i<n; i++)
        if( !transmitFC3OldFreezeByte(m_buffer[i], ntsc) ) { ok = false; break; }
      interrupts();

      if( n < 254 ) break;   // end of file
    }

  // close the file -- this loader sends no CLOSE of its own
  m_currentDevice->listen(0xE0);
  m_currentDevice->unlisten();

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}

#endif
