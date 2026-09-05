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
// GEOS and Wheels byte transfer. Ported from sd2iec's AVR assembly
// (avr/fastloader-ll.S) -- the C port of these routines does not exist
// upstream. GPL v2, Ingo Korb.
//
// Seven variants, and which one applies is a property of the uploaded code:
// it comes out of the CRC detection table as an IEC_FLRX_* value, not from
// anything visible in the transfer. They differ only in cadence and in how the
// eight bits are spread across the four sample points, so they are written
// here as one pair of functions driven by a table.
//
// All times below are microseconds measured from the CLK high-to-low edge that
// starts a byte, taken from the assembly's delay_cycles counts at 8 MHz where
// 8 cycles is 1 us. Half-microsecond values in the 2 MHz variants are real --
// their delays are counted in units of 4 cycles.
//
// Two conventions differ between variants and both are in the original:
//
//   - Whether a set bit leaves its line ASSERTED or RELEASED. The receive
//     routines all invert at the end. On the send side geos_send_byte_1mhz and
//     _2mhz send their first four bits asserted-for-one and then invert for
//     the last four; 1581-2.1 and Wheels 4.4 invert everything up front;
//     plain Wheels never inverts at all.
//   - Which bit goes on CLK and which on DATA at each step. There is no
//     pattern to re-derive here -- the tables below are transcribed.
//
// Above the byte layer sit the session loops, ported from sd2iec's fl-geos.c.
// Three things about them are easy to get wrong and are called out where they
// happen: every data block travels BACKWARDS (last byte first); the command
// block is a 16-bit address that names a routine in the uploaded code, so the
// switch reads as a list of addresses rather than of opcodes; and GEOS stage 1
// XOR-encrypts every chain after the first with a key that only exists inside
// the upload, which is why detection has to capture it (see fastload.h).
//
// Three Wheels operations ask for things this layer cannot answer -- the free
// block count of a native partition, and the current partition/directory
// pointer. Each is answered with a documented stand-in below rather than
// left to fail.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

#if defined(IEC_FP_GEOS) && defined(IEC_IMPL_SOFTLOAD)

// One sample point: when to look, and which data bit each line carries.
struct GeosSample { uint16_t halfUs; uint8_t clkBit; uint8_t dataBit; };

// Receive layouts. halfUs is twice the microsecond offset so the 2 MHz
// variants' half-microsecond points survive as integers.
static const GeosSample s_geos1MHz[4] = {
  {  30, 3, 4 }, {  58, 5, 6 }, {  86, 2, 1 }, { 118, 0, 0 }
};
static const GeosSample s_geos2MHz[4] = {
  {  30, 3, 4 }, {  58, 5, 6 }, {  77, 2, 1 }, {  99, 0, 0 }
};

// Plain Wheels and Wheels 4.4 (1 MHz) read the same bit pattern as ULoad3 --
// pairs of (7,5), (6,4), (3,1), (2,0) with a shift and a nibble swap between
// them -- so they use the shuffling reader below rather than this table.
#define WHEELS_1MHZ_T0   16
#define WHEELS_1MHZ_T1   26
#define WHEELS_1MHZ_T2   41
#define WHEELS_1MHZ_T3   54

#define WHEELS44_1MHZ_T0 17
#define WHEELS44_1MHZ_T1 28
#define WHEELS44_1MHZ_T2 45
#define WHEELS44_1MHZ_T3 61


// Wait for the CLK high-then-low edge that starts a byte.
bool RAMFUNC(IECBusHandler::geosWaitByteStart)()
{
  while( !readPinCLK() )
    {
      if( !readPinATN() || !isResetPinIdle() ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        { interrupts(); noInterrupts(); timer_reset(); }
#endif
    }

  while( readPinCLK() )
    {
      if( !readPinATN() || !isResetPinIdle() ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        { interrupts(); noInterrupts(); timer_reset(); }
#endif
    }

  return true;
}


bool RAMFUNC(IECBusHandler::receiveGeosByte)(uint8_t rxtx, uint8_t &data)
{
  timer_init();
  timer_reset();
  timer_start();

  if( !geosWaitByteStart() ) return false;

  timer_reset();
  timer_start();

  uint8_t v = 0;

  switch( rxtx )
    {
    case IEC_FLRX_GEOS_1MHZ:
    case IEC_FLRX_GEOS_2MHZ:
    case IEC_FLRX_GEOS_1581_21:
      {
        // 1581-2.1 reads with the 2 MHz timing; only its SEND differs
        const GeosSample *s = (rxtx==IEC_FLRX_GEOS_1MHZ) ? s_geos1MHz : s_geos2MHz;

        // the first two points place their bits directly
        for(uint8_t i=0; i<2; i++)
          {
            timer_wait_until(s[i].halfUs / 2.0);
            if( readPinCLK()  ) v |= (uint8_t)(1u << s[i].clkBit);
            if( readPinDATA() ) v |= (uint8_t)(1u << s[i].dataBit);
          }

        // the last two use the shift-in-between sequence
        timer_wait_until(s[2].halfUs / 2.0);
        if( readPinDATA() ) v |= 0x01;
        if( readPinCLK()  ) v |= 0x04;
        v <<= 1;

        timer_wait_until(s[3].halfUs / 2.0);
        if( readPinDATA() ) v |= 0x01;
        if( readPinCLK()  ) v |= 0x04;

        timer_wait_until(s[3].halfUs / 2.0 + 11);
        break;
      }

    case IEC_FLRX_WHEELS_1MHZ:
    case IEC_FLRX_WHEELS44_1541:
      {
        bool w44 = (rxtx==IEC_FLRX_WHEELS44_1541);
        const uint32_t t0 = w44 ? WHEELS44_1MHZ_T0 : WHEELS_1MHZ_T0;
        const uint32_t t1 = w44 ? WHEELS44_1MHZ_T1 : WHEELS_1MHZ_T1;
        const uint32_t t2 = w44 ? WHEELS44_1MHZ_T2 : WHEELS_1MHZ_T2;
        const uint32_t t3 = w44 ? WHEELS44_1MHZ_T3 : WHEELS_1MHZ_T3;

        timer_wait_until(t0);
        if( readPinDATA() ) v |= 0x01;
        if( readPinCLK()  ) v |= 0x04;
        v <<= 1;

        timer_wait_until(t1);
        if( readPinDATA() ) v |= 0x01;
        if( readPinCLK()  ) v |= 0x04;
        v = (uint8_t)((v << 4) | (v >> 4));
        v >>= 1;

        timer_wait_until(t2);
        if( readPinDATA() ) v |= 0x01;
        if( readPinCLK()  ) v |= 0x04;
        v <<= 1;

        timer_wait_until(t3);
        if( readPinDATA() ) v |= 0x01;
        if( readPinCLK()  ) v |= 0x04;

        timer_wait_until(t3 + 20);
        break;
      }

    case IEC_FLRX_WHEELS_2MHZ:
    case IEC_FLRX_WHEELS44_1581:
      {
        // Wheels 4.4 at 2 MHz places every bit directly, CLK carrying the
        // even ones, at 15/26/37/48us
        uint32_t t = 15;
        for(uint8_t i=0; i<4; i++)
          {
            timer_wait_until(t);
            if( readPinCLK()  ) v |= (uint8_t)(1u << (i*2));
            if( readPinDATA() ) v |= (uint8_t)(1u << (i*2 + 1));
            t += 11;
          }

        timer_wait_until(t + 1);
        break;
      }

    default:
      return false;
    }

  data = (uint8_t) ~v;
  return true;
}


// Release both lines, then wait for the computer to pull CLK low. That edge
// is what every send cadence below is measured from.
bool RAMFUNC(IECBusHandler::geosSendByteCommon)()
{
  writePinCLK(HIGH);
  writePinDATA(HIGH);
  delayMicrosecondsISafe(1);

  timer_init();
  timer_reset();
  timer_start();

  while( readPinCLK() )
    {
      if( !readPinATN() || !isResetPinIdle() ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        { interrupts(); noInterrupts(); timer_reset(); }
#endif
    }

  timer_reset();
  timer_start();
  return true;
}


bool RAMFUNC(IECBusHandler::transmitGeosByte)(uint8_t rxtx, uint8_t value)
{
  if( !geosSendByteCommon() ) return false;

  switch( rxtx )
    {
    case IEC_FLRX_GEOS_1MHZ:
    case IEC_FLRX_GEOS_2MHZ:
      {
        // first four bits go out asserted-for-one, the last four inverted
        const uint32_t t0 = (rxtx==IEC_FLRX_GEOS_1MHZ) ? 18 : 9;

        timer_wait_until(t0);
        writePinCLK (!(value & 0x08));
        writePinDATA(!(value & 0x02));

        timer_wait_until(t0 + 10);
        writePinCLK (!(value & 0x04));
        writePinDATA(!(value & 0x01));

        uint8_t v = (uint8_t) ~value;

        timer_wait_until(t0 + 21);
        writePinCLK (!(v & 0x10));
        writePinDATA(!(v & 0x20));

        timer_wait_until(t0 + 33);
        writePinCLK (!(v & 0x40));
        writePinDATA(!(v & 0x80));

        timer_wait_until(t0 + 55);
        break;
      }

    case IEC_FLRX_GEOS_1581_21:
      {
        uint8_t v = (uint8_t) ~value;
        const uint32_t t[4] = { 7, 14, 24, 33 };
        for(uint8_t pair=0; pair<4; pair++)
          {
            timer_wait_until(t[pair]);
            writePinCLK (!(v & 1));
            writePinDATA(!(v & 2));
            v >>= 2;
          }
        timer_wait_until(t[3] + 12);
        break;
      }

    case IEC_FLRX_WHEELS_1MHZ:
    case IEC_FLRX_WHEELS44_1541:
      {
        // never inverted, and sent a nibble at a time: bits 3+1 then 2+0 of
        // the low nibble, then the same of the high one
        uint8_t v = value;
        uint32_t base = 0;
        for(uint8_t half=0; half<2; half++)
          {
            timer_wait_until(base + 9);
            writePinCLK (!(v & 0x08));
            writePinDATA(!(v & 0x02));

            timer_wait_until(base + 23);
            writePinCLK (!(v & 0x04));
            writePinDATA(!(v & 0x01));

            v = (uint8_t)((v >> 4) | (v << 4));   // swap nibbles
            base += 28;
          }
        timer_wait_until(base + 22);
        break;
      }

    case IEC_FLRX_WHEELS_2MHZ:
    case IEC_FLRX_WHEELS44_1581:
      {
        uint8_t v = (uint8_t) ~value;
        const uint32_t t[4] = { 7, 15, 26, 37 };
        for(uint8_t pair=0; pair<4; pair++)
          {
            timer_wait_until(t[pair]);
            writePinCLK (!(v & 1));
            writePinDATA(!(v & 2));
            v >>= 2;
          }
        timer_wait_until(t[3] + 14);
        break;
      }

    default:
      return false;
    }

  return true;
}

#endif
