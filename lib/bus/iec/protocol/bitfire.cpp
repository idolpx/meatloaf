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
// Bitfire. Ported from sd2iec's fl-bitfire.c (Ingo Korb, GPL v2).
//
// Thirteen revisions of one loader, and what changes between them is not the
// bit protocol but the LAYOUT: which line carries data and whether it is
// inverted, how a directory entry is shaped, and which fields precede a block.
// All three are driven by tables here rather than by branches, because that is
// how the differences actually present.
//
//   - Receive: one bit per edge of a request line, the other line carrying the
//     data, either sense. Four combinations, chosen by the detection table's
//     rx/tx value.
//   - Directory: 0.x stores six bytes per entry (track, sector, address,
//     length), 42 to a sector; 1.x stores four (address, length), 63 to a
//     sector, and does NOT store where a file starts -- only where the
//     sector's FIRST file starts, so a random file's position is found by
//     walking the lengths of everything before it.
//   - Block header: seven layouts, selected by the handler table's parameter.
//
// There is no track/sector in a 1.x directory, so the loader walks the disk
// itself at a fixed interleave of 4 -- except 1.2 and later, which use 3 above
// track 17. Skipping the directory track while walking is not an optimisation;
// the files are laid out around it.
//
// Payload bytes go out in REVERSE order within a block. Two commands sd2iec
// declines are declined here too: 0x80 and 0xED both hand control to custom
// drive code, which cannot run anywhere but a real 1541.
// -----------------------------------------------------------------------------
//
// https://github.com/idolpx/bitfire
//

#include "../IECBusHandlerInternal.h"
#include <string.h>

#if defined(IEC_FP_BITFIRE) && defined(IEC_IMPL_SOFTLOAD)

#define BF_INIT_TRACK      18
#define BF_DIR_START       18
#define BF_V0_DIR_ENTRIES  42
#define BF_V1_DIR_ENTRIES  63
#define BF_MAX_FILES      126
#define BF_INTERLEAVE       4

#define BF_EXEC_CMD      0xED   // hands over to custom drive code, not supported
#define BF_SKIP_FILE_CMD 0xEE   // not supported
#define BF_LOAD_NEXT_CMD 0xEF
#define BF_RESET_CMD     0xFF

// block header fields
enum {
  BF_IMM0, BF_LDLO, BF_LDHI, BF_BALO, BF_BAHI, BF_BIDX,
  BF_BARR, BF_BRDT, BF_BLST, BF_BLEN, BF_FNUM
};

#define BF_MAX_HDR_LEN 6

// The last field is always the block length, so it doubles as the end marker.
static const uint8_t s_bfHdrFields[7][BF_MAX_HDR_LEN] = {
  { BF_LDHI, BF_LDLO, BF_BIDX, BF_BLEN, 0, 0 },                       // 0.1
  { BF_IMM0, BF_LDHI, BF_LDLO, BF_BAHI, BF_BLEN, 0 },                 // 0.2/0.3
  { BF_BRDT, BF_LDHI, BF_LDLO, BF_BAHI, BF_BLEN, 0 },                 // 0.4/0.5
  { BF_BRDT, BF_LDLO, BF_LDHI, BF_BAHI, BF_BLEN, 0 },                 // 0.6, 0.7 pre
  { BF_BRDT, BF_FNUM, BF_LDLO, BF_LDHI, BF_BAHI, BF_BLEN },           // 0.7 pre debug
  { BF_BRDT, BF_LDLO, BF_LDHI, BF_BARR, BF_BAHI, BF_BLEN },           // 0.7
  { BF_BLST, BF_BARR, BF_BAHI, BF_BALO, BF_BLEN, 0 }                  // 1.x
};

// Per-release block delays, keyed on the CRC of the file sent before this one.
struct BitfireQuirk { uint16_t crc; uint8_t blockDelayMs; };
static const BitfireQuirk s_bfQuirks[] = {
  { 0x3393, 40 },   // stacked / file $0a at $0b/$0a
  { 0x2b90, 60 },   // beats   / file $0f at $0c/$03
  { 0, 0 }
};

static uint8_t bfBlockDelay(uint16_t crc)
{
  for(const BitfireQuirk *q = s_bfQuirks; q->crc!=0; q++)
    if( q->crc==crc ) return q->blockDelayMs;
  return 0;
}


// One bit per edge of the request line, the other line carrying the data.
// Returns BF_RESET_CMD on abort, which makes the caller's loop exit.
bool IECBusHandler::receiveBitfireByte(uint8_t rxtx, uint8_t &data)
{
  bool clkIsRequest = (rxtx==IEC_FLRX_BITFIRE_CLOCK || rxtx==IEC_FLRX_BITFIRE_ICLK);
  bool invert       = (rxtx==IEC_FLRX_BITFIRE_ICLK  || rxtx==IEC_FLRX_BITFIRE_IDATA);

  // wait for a request, tolerating a bus lock (ATN held low for a long time)
  while( true )
    {
      if( !isResetPinIdle() ) return false;

      bool req = clkIsRequest ? readPinCLK() : readPinDATA();
      if( !req || !readPinATN() ) break;
    }

  if( !readPinATN() )
    {
      // ATN low for more than about 2.5s is a bus lock rather than a request
      uint32_t start = micros();
      while( !readPinATN() )
        {
          if( !isResetPinIdle() ) return false;
          if( (micros()-start) > 2500000UL ) return false;
        }
    }

  uint8_t b = 0;
  for(uint8_t i=8; i>0; i--)
    {
      // wait for the request line to reach the state this bit expects
      uint32_t start = micros();
      while( true )
        {
          bool req = clkIsRequest ? readPinCLK() : readPinDATA();
          if( req == ((i & 1)!=0) ) break;
          if( !isResetPinIdle() ) return false;
          if( (micros()-start) > 90000UL ) return false;   // 90ms: host reset
        }

      delayMicrosecondsISafe(1);

      bool bit = clkIsRequest ? readPinDATA() : readPinCLK();
      if( bit ) b |= 0x80;

      if( i==1 ) break;
      b >>= 1;
    }

  data = invert ? (uint8_t) ~b : b;
  return true;
}


// The computer uploads its drive code over the fast protocol before anything
// else. The bytes are not kept -- only consumed, until it stops sending.
bool IECBusHandler::bitfireLoadDrivecode(uint8_t variant)
{
  bool waitData = (variant >= IEC_FLV_BITFIRE_06);

  writePinCLK(LOW);
  writePinDATA(LOW);

  if( !fastWaitATN(LOW, 1000) ) return false;

  writePinCLK(HIGH);
  writePinDATA(HIGH);

  while( true )
    {
      uint8_t b;
      // the upload itself always clocks on DATA
      if( !receiveBitfireByte(IEC_FLRX_BITFIRE_CLOCK, b) ) break;

      // then waits for the end-of-byte line to go low; 150us without that
      // means the upload is over
      uint32_t start = micros();
      bool sawEnd = false;
      while( (micros()-start) < 150 )
        {
          if( !(waitData ? readPinDATA() : readPinATN()) ) { sawEnd = true; break; }
          if( !isResetPinIdle() ) return false;
        }
      if( !sawEnd ) break;
    }

  writePinDATA(LOW);
  return true;
}


bool IECBusHandler::bitfireLoadDir(uint8_t *dirBuf, uint8_t sector)
{
  return m_currentDevice->epyxReadSector(BF_INIT_TRACK, (uint8_t)(BF_DIR_START - sector), dirBuf);
}


// Advance one sector at the current interleave, moving to the next track when
// this one runs out and skipping the directory track.
void IECBusHandler::bitfireIterateSector(BitfireSession &s)
{
  s.sector += s.interleave;
  uint8_t spt = m_currentDevice->sectorsPerTrack(s.track);
  if( spt==0 ) spt = 21;

  if( s.sector >= spt )
    {
      while( s.sector >= s.interleave ) s.sector -= s.interleave;
      s.sector++;
      if( s.sector == s.interleave )
        {
          s.sector = 0;
          while( ++s.track == BF_INIT_TRACK ) ;
          if( s.variant >= IEC_FLV_BITFIRE_12 )
            s.interleave = (s.track > 17) ? 3 : BF_INTERLEAVE;
        }
    }
}


// A 1.x directory holds only where its FIRST file starts, so a random file's
// position is found by walking the lengths of everything ahead of it.
void IECBusHandler::bitfireIterateFile(BitfireSession &s, const uint8_t *dirBuf, uint8_t file)
{
  uint8_t i = (s.variant >= IEC_FLV_BITFIRE_12PR1) ? 0x00 : 0xFC;

  s.track  = dirBuf[i];
  s.sector = dirBuf[i+1];
  s.offset = dirBuf[i+2];
  if( s.variant >= IEC_FLV_BITFIRE_12 )
    s.interleave = (s.track > 17) ? 3 : BF_INTERLEAVE;

  for(uint8_t n=0; n<file; n++)
    {
      uint16_t l;
      switch( s.variant )
        {
        case IEC_FLV_BITFIRE_12:
        case IEC_FLV_BITFIRE_12PR2:
        case IEC_FLV_BITFIRE_13:
          l = (uint16_t)(dirBuf[0x04+2*0x3F+n] | (dirBuf[0x04+3*0x3F+n] << 8));
          break;
        case IEC_FLV_BITFIRE_12PR1:
          l = (uint16_t)(dirBuf[0x04+n*4+2] | (dirBuf[0x04+n*4+3] << 8));
          break;
        default:
          l = (uint16_t)(dirBuf[n*4+2] | (dirBuf[n*4+3] << 8));
          break;
        }

      uint32_t total = (uint32_t)l + s.offset + 1;
      while( total >= 256 ) { bitfireIterateSector(s); total -= 256; }
      s.offset = (uint8_t) total;
    }
}


void IECBusHandler::bitfireDirEntry(BitfireSession &s, const uint8_t *dirBuf, uint8_t i,
                                    uint16_t &addr, uint16_t &length)
{
  switch( s.variant )
    {
    case IEC_FLV_BITFIRE_12:
    case IEC_FLV_BITFIRE_12PR2:
    case IEC_FLV_BITFIRE_13:
      addr   = (uint16_t)(dirBuf[0x04+0*0x3F+i] | (dirBuf[0x04+1*0x3F+i] << 8));
      length = (uint16_t)(dirBuf[0x04+2*0x3F+i] | (dirBuf[0x04+3*0x3F+i] << 8));
      break;

    case IEC_FLV_BITFIRE_12PR1:
      addr   = (uint16_t)(dirBuf[0x04+(i+1)*4+0] | (dirBuf[0x04+(i+1)*4+1] << 8));
      length = (uint16_t)(dirBuf[0x04+(i+1)*4+2] | (dirBuf[0x04+(i+1)*4+3] << 8));
      break;

    case IEC_FLV_BITFIRE_10:
    case IEC_FLV_BITFIRE_11:
      addr   = (uint16_t)(dirBuf[i*4+0] | (dirBuf[i*4+1] << 8));
      length = (uint16_t)(dirBuf[i*4+2] | (dirBuf[i*4+3] << 8));
      break;

    default:   // 0.x: track, sector, address, length
      s.track  = dirBuf[i*6+0];
      s.sector = dirBuf[i*6+1];
      s.offset = 0;
      addr   = (uint16_t)(dirBuf[i*6+2] | (dirBuf[i*6+3] << 8));
      length = (uint16_t)(dirBuf[i*6+4] | (dirBuf[i*6+5] << 8));
      break;
    }
}


bool IECBusHandler::bitfireLoadFile(BitfireSession &s, uint8_t *dirBuf, uint8_t file)
{
  if( file==BF_LOAD_NEXT_CMD ) file = s.nextFile;
  if( file >= BF_MAX_FILES ) return false;

  uint8_t blockDelay = bfBlockDelay(s.fileCrc);
  s.fileCrc = 0xFFFF;

  // make sure the directory sector holding this file is loaded, and adjust
  // the index to that sector
  uint8_t eps = (s.variant >= IEC_FLV_BITFIRE_10) ? BF_V1_DIR_ENTRIES : BF_V0_DIR_ENTRIES;
  uint8_t ds = 0;
  uint8_t idx = file;
  while( idx >= eps ) { idx -= eps; ds++; }
  if( s.dirSector != ds )
    {
      if( !bitfireLoadDir(dirBuf, ds) ) return false;
      s.dirSector = ds;
    }

  uint16_t addr, length;
  bitfireDirEntry(s, dirBuf, idx, addr, length);

  uint16_t flen;
  if( s.variant >= IEC_FLV_BITFIRE_10 )
    {
      if( s.variant >= IEC_FLV_BITFIRE_12PR1 ) addr += 0x100;
      flen = (uint16_t)(length + 1);
      if( file!=s.nextFile || s.nextFile==0 )
        bitfireIterateFile(s, dirBuf, idx);
    }
  else
    flen = (uint16_t)(length + 1);

  if( s.variant < IEC_FLV_BITFIRE_13 )
    delay(30);   // at least Incoherent Nightmare needs this

  for(uint8_t bi=0; ; bi++)
    {
      if( !m_currentDevice->epyxReadSector(s.track, s.sector, m_buffer) ) return false;

      if( blockDelay>0 ) delay(blockDelay);

      uint16_t blen = ((uint16_t)s.offset + flen > 0x100) ? (uint16_t)(0x100 - s.offset) : flen;

      // build the block header for this revision
      uint8_t hd[BF_MAX_HDR_LEN];
      uint8_t hlen = 0;
      for(uint8_t f=0; f<BF_MAX_HDR_LEN; f++)
        {
          uint8_t hf = s.hdr[f];
          bool done = false;

          switch( hf )
            {
            case BF_IMM0: hd[hlen++] = 0; break;
            case BF_LDLO: if( bi>0 ) break; hd[hlen++] = (uint8_t)(addr & 0xFF); break;
            case BF_BALO: hd[hlen++] = (uint8_t)(addr & 0xFF); break;
            case BF_LDHI: if( bi>0 ) break; hd[hlen++] = (uint8_t)(addr >> 8); break;
            case BF_BAHI:
            case BF_BARR: hd[hlen++] = (uint8_t)(addr >> 8); break;
            case BF_BIDX: hd[hlen++] = bi; break;
            case BF_BRDT:
              hd[hlen++] = (bi==0) ? (uint8_t)(0xFF << 2)
                         : (bi==1) ? (uint8_t)(0x02 << 2)
                                   : (uint8_t)(0x01 << 2);
              break;
            case BF_BLST: hd[hlen++] = (bi>0) ? 0x80 : 0x00; break;
            case BF_FNUM: if( bi==0 ) hd[hlen++] = file; break;
            case BF_BLEN: hd[hlen++] = (uint8_t)(blen & 0xFF); done = true; break;
            default: break;
            }

          if( done ) break;
        }

      noInterrupts();
      writePinCLK(LOW);

      bool ok = true;
      if( s.variant==IEC_FLV_BITFIRE_01 )
        {
          // 0.1 sends an ATN low pulse in place of the shifted first byte
          ok = fastWaitATN(LOW, 1000) && fastWaitATN(HIGH, 0);
        }

      for(uint8_t i=0; i<hlen && ok; i++)
        ok = clockedWriteByte(hd[i], 2000);

      // payload goes out in reverse order within the block
      for(uint16_t i=(uint16_t)s.offset + blen; i>s.offset && ok; i--)
        {
          ok = clockedWriteByte(m_buffer[i-1], 1000);
          s.fileCrc = iecCrc16Update(s.fileCrc, m_buffer[i-1]);
        }

      if( ok ) ok = fastWaitATN(HIGH, 0);

      writePinCLK(HIGH);
      writePinDATA(LOW);
      interrupts();

      if( !ok ) return false;

      s.offset = (uint8_t)((s.offset + blen) & 0xFF);
      if( s.offset==0 ) bitfireIterateSector(s);

      flen = (uint16_t)(flen - blen);
      if( flen==0 ) break;

      addr = (uint16_t)(addr + blen);
    }

  s.nextFile = (uint8_t)(file + 1);
  return true;
}


bool IECBusHandler::runBitfireLoader(uint8_t rxtx, uint8_t variant, uint8_t proto)
{
  if( proto > 6 ) return false;

  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  uint8_t dirBuf[256];
  BitfireSession s;
  memset(&s, 0, sizeof(s));
  s.variant    = variant;
  s.interleave = BF_INTERLEAVE;
  s.fileCrc    = 0xFFFF;
  s.hdr        = s_bfHdrFields[proto];

  if( !bitfireLoadDir(dirBuf, 0) ) goto done;
  s.dirSector = 0;

  if( !bitfireLoadDrivecode(variant) ) goto done;

  // wait for 0.7 and later to release ATN
  if( !fastWaitATN(HIGH, 0) ) goto done;

  while( true )
    {
      writePinCLK(HIGH);
      writePinDATA(HIGH);
      delayMicrosecondsISafe(2);

      uint8_t cmd;
      if( !receiveBitfireByte(rxtx, cmd) ) break;
      writePinDATA(LOW);

      if( cmd < 0xF0 )
        {
          // 0x80 and BF_EXEC_CMD hand over to custom drive code
          if( cmd==0x80 || cmd==BF_EXEC_CMD ) break;
          if( !bitfireLoadFile(s, dirBuf, cmd) ) break;
        }
      else
        {
          if( cmd==BF_RESET_CMD ) break;

          // A disk change: wait for the swap, then reload the directory of
          // whatever is now mounted.
          if( !waitForDiskChange() ) break;
          if( !bitfireLoadDir(dirBuf, 0) ) break;
          s.dirSector = 0;
          s.nextFile  = 0;
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
