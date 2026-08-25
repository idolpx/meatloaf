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
// ULoad Model 3. Ported from sd2iec's fl-ulm3.c, with the bit timing taken
// from its AVR assembly (avr/fastloader-ll.S, uload3_get_byte /
// uload3_send_byte) -- the C port of those routines no longer exists upstream.
// GPL v2, Ingo Korb.
//
// The computer names a file by its first TRACK and SECTOR rather than by name,
// so this loader walks the block chain itself and needs the sector hooks. Each
// block goes out as a count byte followed by that many data bytes, and a count
// of 0 ends the chain. The count is 254 for a full block, or the block's own
// "last used byte" minus one for the final block -- the standard CBM link-byte
// convention.
//
// Two bits go out per step, LSB first, CLK carrying the even bits and DATA the
// odd ones. The two directions do NOT share a cadence: what we send is sampled
// by the computer every 8 us, while what the computer sends arrives at 14, 24,
// 38 and 48 us after the handshake. Both numbers come from the assembly's
// delay_cycles counts at 8 MHz, where one delay_cycles unit of 8 is 1 us.
//
// A byte is inverted on the wire in both directions, and the two directions
// disagree about which level means one: we send bit 1 as a released line, and
// read bit 1 as an asserted one. That asymmetry is in the original and is not
// a transcription slip -- the computer's port inverts on its side.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

// The byte routines are shared with ELoad1, which speaks the same bit protocol
// and differs only in what it does with the bytes.
#if (defined(IEC_FP_ULOAD3) || defined(IEC_FP_ELOAD1)) && defined(IEC_IMPL_SOFTLOAD)

// Set CLK low, wait for DATA low, release CLK, wait for DATA high. Returns
// false without waiting if the computer asserts ATN, which is how it ends the
// session.
bool RAMFUNC(IECBusHandler::uload3Handshake)()
{
  writePinCLK(LOW);

  while( readPinDATA() )
    {
      if( !readPinATN() || !isResetPinIdle() ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        { interrupts(); noInterrupts(); timer_reset(); }
#endif
    }

  writePinCLK(HIGH);

  while( !readPinDATA() )
    {
      if( !readPinATN() || !isResetPinIdle() ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        { interrupts(); noInterrupts(); timer_reset(); }
#endif
    }

  return true;
}


bool RAMFUNC(IECBusHandler::receiveULoad3Byte)(uint8_t &data)
{
  timer_init();
  timer_reset();
  timer_start();

  if( !uload3Handshake() ) return false;

  // the handshake is what starts the clock the computer is counting from
  timer_reset();
  timer_start();

  uint8_t v = 0;

  // Sample DATA into bit 0 and CLK into bit 2, then shuffle -- the sequence
  // below is the assembly's, kept step for step because the bit order it
  // produces is not something to re-derive.
  //
  // The assembly uses "bld", which ASSIGNS a bit; this ORs instead. The two
  // agree because the shifts leave positions 0 and 2 clear before every
  // sample, which is what the original's own running comments say
  // ("_76543_1" then "76543_1_" -- position 2 clear in both). Checked over all
  // 256 values, both forms round-trip exactly; if the shift sequence is ever
  // changed, that stops being true and the OR has to become an assignment.
  timer_wait_until(14);
  if( readPinDATA() ) v |= 0x01;
  if( readPinCLK()  ) v |= 0x04;
  v <<= 1;

  timer_wait_until(24);
  if( readPinDATA() ) v |= 0x01;
  if( readPinCLK()  ) v |= 0x04;
  v = (uint8_t)((v << 4) | (v >> 4));   // swap nibbles
  v >>= 1;

  timer_wait_until(38);
  if( readPinDATA() ) v |= 0x01;
  if( readPinCLK()  ) v |= 0x04;
  v <<= 1;

  timer_wait_until(48);
  if( readPinDATA() ) v |= 0x01;
  if( readPinCLK()  ) v |= 0x04;

  // let the computer return the bus to idle before we touch it again
  timer_wait_until(68);

  data = ~v;
  return true;
}


bool RAMFUNC(IECBusHandler::transmitULoad3Byte)(uint8_t value)
{
  writePinDATA(LOW);

  while( readPinCLK() )
    {
      if( !readPinATN() || !isResetPinIdle() ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        { interrupts(); noInterrupts(); timer_reset(); }
#endif
    }

  writePinDATA(HIGH);

  while( !readPinCLK() )
    {
      if( !readPinATN() || !isResetPinIdle() ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        { interrupts(); noInterrupts(); timer_reset(); }
#endif
    }

  timer_init();
  timer_reset();
  timer_start();

  uint8_t v = ~value;
  uint32_t t = 14;
  for(uint8_t pair=0; pair<4; pair++)
    {
      timer_wait_until(t);
      writePinCLK (v & 1);
      writePinDATA(v & 2);
      v >>= 2;
      t += 8;
    }

  // the last pair went out at 38us and the release belongs ~10us after it;
  // the loop has already advanced t past that pair to 46
  timer_wait_until(t+2);
  writePinCLK(HIGH);
  writePinDATA(HIGH);
  delayMicrosecondsISafe(1);

  return true;
}


#if defined(IEC_FP_ULOAD3) && defined(IEC_IMPL_SOFTLOAD)

// Walk a block chain, sending it or receiving it. Returns false if the session
// should end (the computer asserted ATN part way through).
bool IECBusHandler::uload3TransferChain(uint8_t track, uint8_t sector, bool saving)
{
  bool first = true;

  while( true )
    {
      if( !m_currentDevice->epyxReadSector(track, sector, m_buffer) )
        {
          noInterrupts();
          transmitULoad3Byte(0xFF);
          interrupts();
          return true;
        }

      // byte 0/1 are the link to the next block; on the last block byte 0 is
      // zero and byte 1 is the index of the last used byte
      uint8_t bytecount = (m_buffer[0]==0) ? (uint8_t)(m_buffer[1]-1) : 254;

      noInterrupts();
      bool ok = transmitULoad3Byte(bytecount);
      interrupts();
      if( !ok ) return false;

      if( saving )
        {
          uint8_t i = 0;
          if( first )
            {
              // the load address stays as it is on disk and is sent, not received
              first = false;
              noInterrupts();
              ok = transmitULoad3Byte(m_buffer[2]) && transmitULoad3Byte(m_buffer[3]);
              interrupts();
              if( !ok ) return false;
              i = 2;
            }

          for(; i<bytecount; i++)
            {
              uint8_t b;
              noInterrupts();
              ok = receiveULoad3Byte(b);
              interrupts();
              if( !ok ) return false;
              m_buffer[i+2] = b;
            }

          if( !m_currentDevice->epyxWriteSector(track, sector, m_buffer) )
            {
              noInterrupts();
              transmitULoad3Byte(0xFF);
              interrupts();
              return true;
            }
        }
      else
        {
          for(uint8_t i=0; i<bytecount; i++)
            {
              noInterrupts();
              ok = transmitULoad3Byte(m_buffer[i+2]);
              interrupts();
              if( !ok ) return false;
            }
        }

      track  = m_buffer[0];
      sector = m_buffer[1];
      if( track==0 ) break;
    }

  noInterrupts();
  transmitULoad3Byte(0);   // end marker
  interrupts();
  return true;
}


bool IECBusHandler::runULoad3Loader()
{
  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  while( true )
    {
      uint8_t cmd;
      noInterrupts();
      bool ok = receiveULoad3Byte(cmd);
      interrupts();
      if( !ok ) break;   // ATN: the computer is done with us

      if( cmd==1 || cmd==2 )
        {
          uint8_t track, sector;
          noInterrupts();
          ok = receiveULoad3Byte(track) && receiveULoad3Byte(sector);
          interrupts();
          if( !ok ) break;

          if( !uload3TransferChain(track, sector, cmd==2) ) break;
        }
      else
        {
          // '$' asks for the directory chain, which needs the first block of
          // the current directory. Nothing here can supply that for the media
          // Meatloaf mounts -- it is only a fixed track/sector on a D64 -- so
          // it is refused along with every other unknown command. A directory
          // read under ULoad3 therefore fails cleanly instead of serving the
          // wrong blocks.
          noInterrupts();
          transmitULoad3Byte(0xFF);
          interrupts();
        }
    }

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}

#endif // IEC_FP_ULOAD3

#endif // IEC_FP_ULOAD3 || IEC_FP_ELOAD1
