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
// Booze Design. Ported from sd2iec's fl-booze.c (Ingo Korb, GPL v2).
//
// A block server with TWO addressing schemes, chosen by what the disk holds.
// If track 18 carries a directory sector -- a run of valid track/sector pairs
// padded with zeros, found at sector 9, 12 or 6 -- the computer asks for a
// FILE by index into it. If it does not, the computer sends the track and
// sector directly. Which one is in use is decided once, at startup, by
// find_dir().
//
// Everything is clocked by ATN. Receiving takes one bit per ATN transition off
// the CLK line; sending puts two bits on CLK and DATA per transition, least
// significant pair first. A block runs to byte 255 when its link says another
// follows, or to the link's own byte count when it does not.
//
// Two things here exist only because real releases need them. Some titles do
// not tolerate a block arriving as fast as the drive can send it, so a table
// of per-release delays keyed on the CRC of the PREVIOUS file slows those
// down; and "bus lock" is a command that makes the drive ignore the bus until
// it sees a low/high/low/high pattern on ATN with each phase about 18 us,
// which the drive answers by asserting DATA for 18 us.
//
// Disk flips are not supported: the protocol lets the computer ask for another
// disk by id, and sd2iec waits for a disk-change event. Nothing at this layer
// reports one, so a request for a disk that is not mounted ends the session
// instead of waiting forever.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

#if defined(IEC_FP_BOOZE) && defined(IEC_IMPL_SOFTLOAD)

#define BOOZE_BOOT_TRACK     18
#define BOOZE_BOOT_SECTOR     0
#define BOOZE_DISK_ID_OFFSET 0xFF

// Per-release block delays, in milliseconds, keyed on the CRC of the file sent
// before this one. Straight from sd2iec; there is nothing to derive them from.
struct BoozeQuirk { uint16_t crc; uint8_t blockDelayMs; };

static const BoozeQuirk s_boozeQuirks[] = {
  { 0x3562, 120 },   // the elder scrollers    / file $19 at $1f/$04
  { 0x19b2, 120 },   // uncensored             / disk 2 file $10 at $11/$0e
  { 0xd41b, 240 },   // smart girls hate booze / file at $1b/$02
  { 0xe529, 240 },   // andropolis             / file at $17/$02
  { 0, 0 }
};


static uint8_t boozeBlockDelay(uint16_t crc)
{
  for(const BoozeQuirk *q = s_boozeQuirks; q->crc!=0; q++)
    if( q->crc==crc ) return q->blockDelayMs;

  return 0;
}



// One bit per ATN transition, taken off CLK, least significant first. Leaves
// DATA asserted, which is what the caller expects.
bool IECBusHandler::receiveBoozeByte(uint8_t &data)
{
  uint8_t b = 0;

  timer_init();
  timer_reset();
  timer_start();

  for(uint8_t i=0; i<8; i++)
    {
      writePinDATA(HIGH);

      if( !fastWaitATN(HIGH, 0) ) return false;
      delayMicrosecondsISafe(2);
      b = (uint8_t)((b >> 1) | (readPinCLK() ? 0 : 0x80));
      writePinDATA(LOW);

      if( !fastWaitATN(LOW, 1000) ) return false;
    }

  data = b;
  return true;
}


// Send one block starting at "from". A block whose link byte is non-zero runs
// to byte 255; otherwise the link's second byte says where it ends.
bool IECBusHandler::boozeSendBlock(const uint8_t *data, uint8_t from, uint16_t *crc)
{
  if( !fastWaitATN(LOW, 1000) ) return true;   // timed out

  noInterrupts();
  writePinDATA(HIGH);   // ready

  uint8_t p = from;
  uint8_t last = (data[0]!=0) ? 0xFF : data[1];
  bool failed = false;
  while( true )
    {
      if( !clockedWriteByte(data[p], 4000) ) { failed = true; break; }
      if( crc!=NULL && p>1 ) *crc = iecCrc16Update(*crc, data[p]);
      if( p==last ) break;
      p++;
    }

  if( !failed )
    {
      // the last bit pair is still unacknowledged when the loop ends
      failed = !fastWaitATN(HIGH, 0);
      writePinCLK(HIGH);
      writePinDATA(LOW);   // busy
    }

  interrupts();
  return failed;
}


// Walk the chain whose first link is already in m_buffer.
bool IECBusHandler::boozeSendFile(uint16_t &fileCrc)
{
  uint8_t blockDelay = boozeBlockDelay(fileCrc);

  // The first block has to be held back; at least Neon, Edge of Disgrace and
  // The Elder Scrollers need it.
  delay(60);

  fileCrc = 0xFFFF;

  while( m_buffer[0]!=0 )
    {
      if( blockDelay!=0 ) delay(blockDelay);

      uint8_t track = m_buffer[0], sector = m_buffer[1];
      if( !m_currentDevice->epyxReadSector(track, sector, m_buffer) ) return true;

      if( boozeSendBlock(m_buffer, 0, &fileCrc) ) return true;
    }

  return false;
}


// "Bus lock": ignore everything until ATN shows low/high/low/high with each
// phase about 18us, then answer by asserting DATA for 18us. sd2iec allows 30us
// per phase rather than 18 so an interrupt cannot break the match, and the
// same slack is kept here.
void IECBusHandler::boozeBusLock()
{
  writePinDATA(HIGH);

  uint8_t phase = 0;
  uint32_t start = micros();

  while( true )
    {
      if( !isResetPinIdle() ) return;

      if( (micros()-start) > 30 )
        {
          if( phase==4 ) break;
          phase = 0;
          start = micros();
          continue;
        }

      if( (readPinATN() ? 0 : 1) != (phase & 1) )
        {
          phase++;
          start = micros();
        }
    }

  noInterrupts();
  writePinDATA(LOW);
  delayMicrosecondsISafe(18);
  writePinDATA(HIGH);
  interrupts();
}


// Look for a directory sector on track 18. A valid one holds only usable
// track/sector pairs and is padded with zeros to the end. The candidates are
// sectors 9, 12 and 6, in that order.
bool IECBusHandler::boozeFindDir(uint8_t &dirSector, uint8_t *dirBuf)
{
  static const uint8_t candidates[3] = { 9, 12, 6 };

  for(uint8_t c=0; c<3; c++)
    {
      uint8_t s = candidates[c];
      if( !m_currentDevice->epyxReadSector(BOOZE_BOOT_TRACK, s, dirBuf) )
        return false;

      // Andropolis has a sector here that looks valid but is not used
      if( s==6 && dirBuf[0]!=1 ) break;

      for(uint16_t i=0; ; i+=2)
        {
          if( dirBuf[i]==0 )
            {
              if( i<2 ) break;   // need at least one real entry

              uint16_t j = i;
              bool padded = true;
              while( dirBuf[++j]==0 )
                if( j==0xFF ) { dirSector = s; return true; }
              padded = false;
              (void) padded;
              break;
            }

          uint8_t t = dirBuf[i], sec = dirBuf[i+1];
          uint8_t spt = m_currentDevice->sectorsPerTrack(t);
          if( !(t>0 && t<=42 && spt>0 && sec<spt) ) break;

          if( i>=0xFE ) break;
        }
    }

  dirSector = 0;
  return false;
}


bool IECBusHandler::runBoozeLoader()
{
  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  writePinDATA(LOW);   // busy

  // The directory sector, if there is one, has to stay resident for the whole
  // session; m_buffer is needed for the blocks being sent.
  uint8_t dirBuf[256];
  uint8_t dirSector = 0;
  boozeFindDir(dirSector, dirBuf);

  uint16_t fileCrc = 0xFFFF;

  while( true )
    {
      writePinDATA(HIGH);

      if( !fastWaitATN(LOW, 0) ) break;

      uint8_t cmd;
      if( !receiveBoozeByte(cmd) ) break;
      if( !readPinATN() ) break;   // probably a host reset

      if( dirSector!=0 || (cmd & 0x80)!=0 )
        {
          // directory-sector protocol
          if( (cmd & 0x80)==0 )
            {
              uint16_t idx = (uint16_t)(cmd << 1);
              m_buffer[0] = dirBuf[idx];
              m_buffer[1] = dirBuf[idx+1];
            }
          else if( cmd==0xFF )
            {
              boozeBusLock();
              continue;
            }
          else
            {
              // A request for a different disk. sd2iec waits for a disk-change
              // event here; nothing at this layer reports one, so rather than
              // wait forever the session ends.
              break;
            }
        }
      else
        {
          // track/sector protocol. A command of 0 asks for the disk id first.
          while( cmd==0 )
            {
              if( !m_currentDevice->epyxReadSector(BOOZE_BOOT_TRACK, BOOZE_BOOT_SECTOR, m_buffer) )
                goto done;

              if( boozeSendBlock(m_buffer, BOOZE_DISK_ID_OFFSET, NULL) ) goto done;

              // no timeout here: "Let's scroll it" waits for a keypress before
              // continuing once it is happy with the disk
              if( !fastWaitATN(LOW, 0) ) goto done;

              if( !receiveBoozeByte(cmd) ) goto done;
              if( cmd!=0 ) break;   // happy: cmd is the track of the first file

              break;                // wrong disk and no way to wait for a flip
            }

          if( cmd==0 ) break;

          m_buffer[0] = cmd;
          if( !receiveBoozeByte(m_buffer[1]) ) break;
        }

      if( m_buffer[0]==0 ) break;   // loader done

      if( boozeSendFile(fileCrc) ) break;
    }

 done:
  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}

#endif
