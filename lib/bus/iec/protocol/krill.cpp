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
// Krill's loader. Ported from sd2iec's fl-krill.c (Ingo Korb, GPL v2).
//
// Nine revisions between r58 and r192, and unlike Bitfire the differences are
// not confined to a table -- which line requests, which line carries data,
// what the two metadata bytes in front of a block mean, and even which line
// means "ready" all move between revisions. Everything below that branches on
// the variant does so because the wire behaviour genuinely differs.
//
// A file is asked for BY NAME, except that a request of one or two bytes which
// look like a valid track and sector is taken as a direct block address --
// sd2iec notes this misreads a one-character filename whose PETSCII code is
// 41 or less, and that no affected production is known.
//
// Three things are deliberately not supported, all of which need to run 6502
// code on a real drive:
//
//   - Custom drive-code upload (r186's Scramble Infinity, and r192's, which
//     announces itself by sending a name longer than 17 bytes).
//   - The save plugin, which backs up drive memory and writes it back.
//   - Fast serial (burst) transfer on r184 and later, which needs a C128's
//     hardware shift register. Those revisions fall back to the two-bit
//     protocol, which they also speak.
//
// The uploaded drive code itself is consumed and discarded: which revision it
// is has already been decided by the CRC of that same upload (see fastload.h),
// so there is nothing left for it to tell us.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"
#include <string.h>

#if defined(IEC_FP_KRILL) && defined(IEC_IMPL_SOFTLOAD)

#define KR_CBM_NAME_LENGTH 16
#define KR_R192_NAME_LENGTH (KR_CBM_NAME_LENGTH+1)

// r58pre shuffles its bits on the way out; every later revision does not.
static const uint8_t s_krEncoding58pre[8] = {
  1<<7, 1<<5, 1<<6, 1<<4, 1<<3, 1<<1, 1<<2, 1<<0
};

struct KrillQuirk { uint16_t crc; uint8_t blockDelayMs; };
static const KrillQuirk s_krQuirks[] = {
  { 0, 0 }
};


// One byte, clocked by the computer: a bit per edge of the request line, with
// the data line's sense inverted.
bool IECBusHandler::krillReadByte(uint8_t rxtx, uint8_t &data)
{
  // r164 asks on ATN while its data still rides CLK; everything else pairs
  // CLK with DATA one way round or the other
  bool clkIsRequest = (rxtx==IEC_FLRX_KRILL_CLOCK);

  uint8_t b = 0;
  bool prev = clkIsRequest ? readPinDATA() : readPinCLK();

  for(uint8_t i=8; i!=0; i--)
    {
      uint32_t start = micros();
      while( (clkIsRequest ? readPinDATA() : readPinCLK()) == prev )
        {
          if( !isResetPinIdle() ) return false;
          if( (micros()-start) > 90000UL ) return false;   // 90ms: host reset
        }

      delayMicrosecondsISafe(2);
      prev = clkIsRequest ? readPinDATA() : readPinCLK();
      bool bit = clkIsRequest ? readPinCLK() : readPinDATA();
      b = (uint8_t)((b >> 1) | (bit ? 0 : 0x80));
    }

  data = b;
  return true;
}


bool IECBusHandler::krillSendByte(uint8_t rxtx, uint8_t b)
{
  if( rxtx==IEC_FLRX_KRILL_58PRE )
    {
      uint8_t v = (uint8_t) ~b;
      for(uint8_t i=0; i<8; i+=2)
        {
          uint8_t o = 0;
          if( v & s_krEncoding58pre[i]   ) o |= 0x01;
          if( v & s_krEncoding58pre[i+1] ) o |= 0x02;

          if( i & 2 )
            { if( !fastWaitATN(LOW, 1000) ) return false; }
          else
            { if( !fastWaitATN(HIGH, 0) ) return false; }

          writePinCLK (o & 1);
          writePinDATA(o & 2);
        }

      return true;
    }

  return clockedWriteByte(b, 1000);
}


// Consume the drive code the computer uploads. Its bytes are not kept: the CRC
// of this same upload is what already identified the revision.
bool IECBusHandler::krillLoadDrivecode(uint8_t rxtx)
{
  writePinCLK(HIGH);
  writePinDATA(HIGH);

  while( true )
    {
      uint8_t b;
      if( !krillReadByte(IEC_FLRX_KRILL_CLOCK, b) ) break;
      if( !isResetPinIdle() ) return false;
    }

  (void) rxtx;
  return true;
}


// Read the name of the file being asked for. One or two bytes that look like a
// valid track and sector are taken as a block address instead -- see the
// header comment about the one-character-filename case that misreads.
int16_t IECBusHandler::krillReadFilename(uint8_t rxtx, uint8_t variant,
                                         char *name, uint8_t maxLen, bool firstFile)
{
  uint8_t limit = (variant >= IEC_FLV_KRILL_R192) ? (KR_R192_NAME_LENGTH+2) : maxLen;

  writePinCLK(HIGH);
  writePinDATA(HIGH);

  uint8_t i = 0;
  for(; i<limit; i++)
    {
      uint8_t b;
      if( !krillReadByte(rxtx, b) ) return -1;
      name[i] = (char) b;
      if( b==0 ) break;

      // a plausible track/sector pair ends the request early
      if( i==1 && firstFile )
        {
          uint8_t t = (uint8_t) name[0], sec = (uint8_t) name[1];
          uint8_t spt = m_currentDevice->sectorsPerTrack(t);
          if( t>0 && spt>0 && sec<spt ) { i = 2; break; }
        }
    }

  if( variant != IEC_FLV_KRILL_R164 ) writePinCLK(LOW);
  writePinDATA(LOW);

  if( i==0 )
    {
      name[0] = '*';   // match the next file
      name[1] = 0;
    }
  else if( maxLen < KR_CBM_NAME_LENGTH )
    {
      // only maxLen characters have to match, so the rest is a prefix
      name[maxLen]   = '*';
      name[maxLen+1] = 0;
    }

  return i;
}


// Build the two metadata bytes that precede a block. Every revision means
// something different by them, which is why this is a switch and not a table.
void IECBusHandler::krillBlockHeader(uint8_t variant, uint8_t bi, uint8_t lastUsed,
                                     bool eoi, uint8_t hd[2])
{
  switch( variant )
    {
    case IEC_FLV_KRILL_R58PRE:
    case IEC_FLV_KRILL_R58:
    case IEC_FLV_KRILL_R146:
      // here the EOF marker is the status byte instead
      hd[0] = bi;
      hd[1] = (uint8_t)(lastUsed - 2);
      break;

    case IEC_FLV_KRILL_R159:
    case IEC_FLV_KRILL_R164:
      hd[0] = (uint8_t)(0x82 - (eoi?1:0));
      hd[1] = !eoi ? (uint8_t)(bi+2) : (uint8_t)(~lastUsed + 2);
      break;

    case IEC_FLV_KRILL_R184:
      hd[0] = (uint8_t)(2 | (eoi?1:0));
      hd[1] = !eoi ? (uint8_t)(bi+2) : (uint8_t)(~lastUsed + 1);
      break;

    default:   // r186 and later swap the two
      hd[0] = !eoi ? (uint8_t)(bi+1) : (uint8_t)(~lastUsed + 1);
      hd[1] = (uint8_t)(2 | (eoi?1:0));
      break;
    }
}


bool IECBusHandler::krillSendFile(uint8_t rxtx, uint8_t variant, const char *name,
                                  bool byTS, uint16_t &fileCrc)
{
  uint8_t blockDelay = 0;
  for(const KrillQuirk *q = s_krQuirks; q->crc!=0; q++)
    if( q->crc==fileCrc ) { blockDelay = q->blockDelayMs; break; }

  bool found;
  uint8_t track = 0, sector = 0;

  if( byTS )
    {
      track  = (uint8_t) name[0];
      sector = (uint8_t) name[1];
      found  = m_currentDevice->epyxReadSector(track, sector, m_buffer);
    }
  else
    {
      m_currentDevice->listen(0xF0);
      found = true;
      for(uint8_t i=0; name[i] && found; i++)
        {
          int8_t w;
          while( (w = m_currentDevice->canWrite())<0 )
            if( !isResetPinIdle() ) { found = false; break; }

          if( found && w>0 )
            m_currentDevice->write((uint8_t)name[i], name[i+1]==0);
          else
            found = false;
        }
      m_currentDevice->unlisten();
    }

  if( found ) fileCrc = 0xFFFF;

  uint8_t hd[2] = { 0xFF, 0 };
  if( !found )
    hd[0] = (variant > IEC_FLV_KRILL_R146) ? 0 : 0xFE;

  uint8_t bi = 0;
  bool ok = true;

  while( ok )
    {
      uint16_t n = 0;
      bool eoi = true;

      if( found )
        {
          if( byTS )
            {
              // a block chain: byte 0 is the next track, 0 when this is the last
              eoi = (m_buffer[0]==0);
              n   = eoi ? (uint16_t)(m_buffer[1]) : 255;
              krillBlockHeader(variant, bi, (uint8_t)n, eoi, hd);
            }
          else
            {
              m_currentDevice->talk(0);
              m_inTask = false;
              uint8_t got = m_currentDevice->read(m_buffer+2, 254);
              m_inTask = true;
              if( got==0 ) break;
              eoi = (got < 254);
              n = (uint16_t)(got + 1);
              krillBlockHeader(variant, bi, (uint8_t)n, eoi, hd);
            }
        }

      noInterrupts();

      // data ready -- r164 signals on the opposite lines from everything else
      writePinDATA(variant == IEC_FLV_KRILL_R164);
      writePinCLK (variant != IEC_FLV_KRILL_R164);

      if( variant >= IEC_FLV_KRILL_R192 )
        {
          // r192 can ask only whether a file EXISTS, without loading it
          while( readPinATN() && readPinCLK() )
            if( !isResetPinIdle() ) { ok = false; break; }

          if( ok && !readPinCLK() )
            {
              if( !fastWaitATN(LOW, 0) ) ok = false;
              writePinDATA(found);
              interrupts();
              break;
            }
        }
      else if( variant <= IEC_FLV_KRILL_R146 )
        {
          if( !fastWaitATN(LOW, 0) ) ok = false;
        }

      if( ok ) ok = krillSendByte(rxtx, hd[0]);

      if( ok && found )
        {
          ok = krillSendByte(rxtx, hd[1]);
          for(uint16_t i=2; i<=n && ok; i++)
            {
              ok = krillSendByte(rxtx, m_buffer[i]);
              fileCrc = iecCrc16Update(fileCrc, m_buffer[i]);
            }
        }

      // the two-bit sender leaves the last pair unacknowledged
      if( ok ) ok = fastWaitATN(HIGH, 0);

      // busy
      writePinCLK (variant == IEC_FLV_KRILL_R164);
      writePinDATA(variant != IEC_FLV_KRILL_R164);
      interrupts();

      if( !ok ) break;
      if( found && blockDelay!=0 ) delay(blockDelay);

      if( !found ) break;

      if( (variant > IEC_FLV_KRILL_R146) && !fastWaitATN(LOW, 1000) ) break;

      if( !eoi )
        {
          if( byTS )
            {
              uint8_t nt = m_buffer[0], ns = m_buffer[1];
              if( !m_currentDevice->epyxReadSector(nt, ns, m_buffer) ) { hd[0] = 0xFF; found = false; }
              else { bi++; continue; }
            }
          else
            { bi++; continue; }
        }
      else
        {
          hd[0] = (variant > IEC_FLV_KRILL_R146) ? 0 : 0xFE;
          found = false;
        }
    }

  if( !byTS )
    {
      m_currentDevice->listen(0xE0);
      m_currentDevice->unlisten();
    }

  return ok;
}


bool IECBusHandler::runKrillLoader(uint8_t rxtx, uint8_t variant,
                                   const uint8_t *cmd, uint8_t cmdLen)
{
  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  uint8_t maxLen = (variant != IEC_FLV_KRILL_R58PRE) ? KR_CBM_NAME_LENGTH : 2;

  // r192 announces itself with an id string in the M-E and carries its
  // parameters there
  if( variant >= IEC_FLV_KRILL_R192 && cmdLen>=19 )
    maxLen = cmd[cmdLen-1];
  if( maxLen==0 || maxLen>KR_CBM_NAME_LENGTH ) maxLen = KR_CBM_NAME_LENGTH;

  if( !krillLoadDrivecode(rxtx) ) goto done;

  {
  // r159 and r184 and later request on DATA; the rest on ATN
  bool reqOnData = (variant==IEC_FLV_KRILL_R159 || variant >= IEC_FLV_KRILL_R184);
  uint16_t fileCrc = 0xFFFF;
  bool firstFile = true;
  char name[KR_R192_NAME_LENGTH+4];

  while( !readPinATN() )
    {
      writePinCLK (variant == IEC_FLV_KRILL_R164);
      writePinDATA(variant != IEC_FLV_KRILL_R164);

      // wait for a request
      while( !(reqOnData ? readPinDATA() : readPinATN()) )
        if( !isResetPinIdle() ) goto done;

      delayMicrosecondsISafe(10);

      // both released means the computer has finished with us
      if( readPinDATA() && readPinATN() ) break;

      if( variant >= IEC_FLV_KRILL_R184 )
        {
          writePinCLK(HIGH);
          delayMicrosecondsISafe(2);
        }

      // r186 and later use CLK to say "this is a custom drive-code upload",
      // which is not supported -- see the header comment
      if( variant >= IEC_FLV_KRILL_R186 && !readPinCLK() ) break;

      int16_t len = krillReadFilename(rxtx, variant, name, maxLen, firstFile);
      if( len < 0 ) break;

      // more than 17 bytes can only be r192 announcing a drive-code upload
      if( len > KR_R192_NAME_LENGTH ) break;

      bool byTS = (len==2 && firstFile);
      firstFile = false;

      if( !krillSendFile(rxtx, variant, name, byTS, fileCrc) ) break;

      // wait for the request line to be set again
      while( !(reqOnData ? readPinDATA() : readPinATN()) )
        if( !isResetPinIdle() ) goto done;
    }
  }

 done:
  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}

#endif
