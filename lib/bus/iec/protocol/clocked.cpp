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
// The ATN-clocked byte writer, shared by Booze and Bitfire. Ported from
// sd2iec's clocked_write_byte() (Ingo Korb, GPL v2).
//
// Two bits leave per ATN transition -- CLK takes the low bit of the pair and
// DATA the next -- with the waits alternating: ATN high before the first and
// third pair, ATN low before the second and fourth. Least significant pair
// first, and a set bit RELEASES its line.
//
// The routine deliberately returns with the last pair still unacknowledged;
// its callers wait for the closing ATN edge themselves, because what they do
// next differs.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

#if (defined(IEC_FP_BOOZE) || defined(IEC_FP_BITFIRE) || defined(IEC_FP_SPINDLE) || defined(IEC_FP_SPARKLE) || defined(IEC_FP_KRILL)) && defined(IEC_IMPL_SOFTLOAD)

bool IECBusHandler::fastWaitATN(bool state, uint32_t timeoutMs)
{
  // micros() rather than millis(): it is the clock the bus handler's own
  // timing helpers use and the one available on every platform here.
  uint32_t start = micros();
  uint32_t timeoutUs = timeoutMs * 1000;

  while( readPinATN()!=state )
    {
      if( !isResetPinIdle() ) return false;
      if( timeoutUs!=0 && (micros()-start) > timeoutUs ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        { interrupts(); noInterrupts(); timer_reset(); }
#endif
    }

  return true;
}


bool IECBusHandler::clockedWriteByte(uint8_t b, uint32_t timeoutMs)
{
  for(uint8_t i=0; i<8; i+=2)
    {
      uint8_t o = b;
      b >>= 2;

      if( i & 2 )
        { if( !fastWaitATN(LOW, timeoutMs) ) return false; }
      else
        { if( !fastWaitATN(HIGH, 0) ) return false; }

      writePinCLK (o & 1);
      writePinDATA(o & 2);
    }

  return true;
}

#endif
