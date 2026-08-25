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
// Ultraboot. Ported from sd2iec's fl-ultraboot.c and its AVR assembly
// (ultraboot_send_byte). GPL v2, Ingo Korb.
//
// Unlike every other loader here, Ultraboot is NOT identified by a CRC of its
// upload. It is identified from the M-E COMMAND itself: the bytes after the
// address are hashed, skipping the two that carry the start track and sector,
// and the result says whether this is Ultraboot proper (which always starts at
// 36/0) or the Ultraboot Menue (which carries its own start block). That is
// what the catch-all rows in the handler table exist for.
//
// It lives on an EXTENDED 1541 image -- tracks 36 and up, which a plain D64
// does not have -- and stores those extra tracks in one of four "speedzones",
// 17, 18, 19 or 21 sectors per track. The zone is not in the image: it is read
// out of byte 207 of the loader block at 36/0, and every later track/sector is
// then remapped from that zone's geometry onto the image's flat 17 sectors.
// Getting that mapping wrong reads plausible-looking wrong blocks, not errors.
//
// The transfer is drive-clocked with no handshake per bit: DATA is asserted at
// 0 us and released at 15, four bit pairs go out at 19, 27, 35 and 43, DATA is
// released again at 51, and at 56 the computer's DATA line is read back as an
// acknowledgement.
//
// Only the LOAD half is implemented. sd2iec also serves Ultraboot Maker's
// format and write commands, which need to extend a D64 to 40 or 42 tracks and
// to mark sectors in its error-info block -- neither of which this layer can
// ask VDrive for. Those two commands are declined, so making a disk is not
// supported; using one is.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

#if defined(IEC_FP_ULTRABOOT) && defined(IEC_IMPL_SOFTLOAD)

// sectors per track in each of the four speedzones
static const uint8_t s_ubSectorsPerTrack[4] = { 17, 18, 19, 21 };


// Map a track/sector expressed in the current speedzone onto the flat
// 17-sectors-per-track layout the image actually stores.
void IECBusHandler::ultrabootMapSector(uint8_t &track, uint8_t &sector, uint8_t speedzone)
{
  if( speedzone==0 || track<36 ) return;

  uint16_t index = (uint16_t)s_ubSectorsPerTrack[speedzone] * (track - 36) + sector;
  track  = (uint8_t)(36 + index / 17);
  sector = (uint8_t)(index % 17);
}


// One byte, drive-clocked. Returns false if the computer did not acknowledge.
bool RAMFUNC(IECBusHandler::transmitUltrabootByte)(uint8_t value)
{
  timer_init();
  timer_reset();
  timer_start();

  while( !readPinDATA() )
    {
      if( !readPinATN() || !isResetPinIdle() ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        { interrupts(); noInterrupts(); timer_reset(); }
#endif
    }

  timer_reset();
  timer_start();

  writePinDATA(LOW);                 // 0us
  timer_wait_until(15);
  writePinDATA(HIGH);                // 15us

  // Bit pairs are taken from bits 7 and 5 of a working copy that is shifted
  // and nibble-swapped between them, so the order is 7/5, 6/4, 3/1, 2/0.
  uint8_t v = value;

  timer_wait_until(19);
  writePinCLK (!(v & 0x80));
  writePinDATA(!(v & 0x20));

  v <<= 1;
  timer_wait_until(27);
  writePinCLK (!(v & 0x80));
  writePinDATA(!(v & 0x20));

  v = (uint8_t)((value >> 4) | (value << 4));   // swap nibbles
  timer_wait_until(35);
  writePinCLK (!(v & 0x80));
  writePinDATA(!(v & 0x20));

  v <<= 1;
  timer_wait_until(43);
  writePinCLK (!(v & 0x80));
  writePinDATA(!(v & 0x20));

  timer_wait_until(51);
  writePinDATA(HIGH);

  timer_wait_until(56);
  return readPinDATA();              // the computer's acknowledgement
}


// Identify Ultraboot from the M-E command. Returns false if this is not one.
bool IECBusHandler::ultrabootDetect(const uint8_t *cmd, uint8_t cmdLen,
                                    uint8_t &track, uint8_t &sector, uint8_t &speedzone)
{
  if( cmdLen<5 ) return false;
  uint16_t address = (uint16_t)(cmd[3] | (cmd[4] << 8));
  if( address!=0x0205 ) return false;   // 0x0417 is the Maker's format code

  uint16_t crc = 0xFFFF;
  for(uint8_t i=5; i<cmdLen; i++)
    {
      if( i==6 || i==11 ) continue;     // start track and sector are variable
      crc = iecCrc16Update(crc, cmd[i]);
    }

  if( crc==0xd75a )                     // Ultraboot
    {
      track = 36; sector = 0;
      speedzone = 0;                    // 36/0 is never remapped; the real zone
      return true;                      // comes out of the loader block
    }

  if( crc==0x3e82 && cmdLen>11 )        // Ultraboot Menue
    {
      track = cmd[6]; sector = cmd[11];
      speedzone = 0;
      return true;
    }

  return false;
}


bool IECBusHandler::runUltrabootLoader(const uint8_t *cmd, uint8_t cmdLen)
{
  uint8_t track, sector, speedzone;
  if( !ultrabootDetect(cmd, cmdLen, track, sector, speedzone) ) return false;

  // Ultraboot only exists on an extended 1541 image
  if( m_currentDevice->imageType()!=IEC_IMG_1541 ) return false;

  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  writePinCLK(LOW);
  writePinDATA(HIGH);

  // wait for the computer to release DATA
  while( !readPinDATA() )
    if( !readPinATN() || !isResetPinIdle() ) goto done;

  // one frame, so the computer's screen is off before the transfer starts
  delay(20);

  while( track>0 && track<=40 )
    {
      uint8_t t = track, s = sector;
      ultrabootMapSector(t, s, speedzone);

      // an extended image is required, so a read failure here is expected
      // rather than exceptional
      if( !m_currentDevice->epyxReadSector(t, s, m_buffer) ) break;

      if( sector!=0 || track!=36 )
        {
          noInterrupts();
          bool ok = true;
          for(uint16_t i=0; i<256 && ok; i++)
            ok = transmitUltrabootByte(m_buffer[i]);
          interrupts();
          if( !ok ) break;
        }
      else
        {
          // The loader block itself is not sent -- it is read only to find the
          // speedzone and where the payload starts.
          if( m_buffer[207] & ~0x60 ) break;   // not a plausible speedzone byte
          speedzone = (uint8_t)(m_buffer[207] >> 5);
        }

      sector = m_buffer[254];
      track  = m_buffer[255];
    }

 done:
  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}

#endif
