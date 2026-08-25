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
// Sparkle. Ported from sd2iec's fl-sparkle.c (Ingo Korb, GPL v2).
//
// Sparkle sends BUNDLES -- runs of blocks laid out across the disk at a
// per-speedzone interleave, with the run's length carried inside the first
// block it sends. Almost nothing about it is fixed:
//
//   - The BAM sector at 18/0 holds the disk's parameters, and WHERE each
//     parameter sits depends on the loader revision. The revision itself is
//     deduced from byte values in that same sector, which is why the layout
//     table and the detection below have to be read together.
//   - 2.x and later obfuscate every byte, with three different encodings, and
//     2.x also stores its directory sector partially REVERSED. Which of the
//     two directory layouts a disk uses is decided by checking whether the
//     first entry decodes to something plausible.
//   - Four productions need behaviour no flag in the format expresses, so they
//     are recognised by a production id in the BAM: Median and Median Final
//     use a sector skew, Propaganda 30 restarts at sector 0 on a track change,
//     Aloft applies the sub-sector correction on every track, and Memento Mori
//     and reMETA send the bundle number inverted.
//
// The high-score saver (bundle 0x7E) is served: it writes blocks back to the
// disk, so it needs the sector write hook as well as the read one.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"
#include <string.h>

#if defined(IEC_FP_SPARKLE) && defined(IEC_IMPL_SOFTLOAD)

#define SPK_INIT_TRACK    18
#define SPK_BAM_SECTOR     0
#define SPK_DIR_START     17
#define SPK_SAVER_BUNDLE 0x7E
#define SPK_SAVE_FILE    0x7F
#define SPK_SEQ_BUNDLE   0x80
#define SPK_SKEW           2
#define SPK_BNDCNT_OFFS 0xFE
#define SPK_PRODID_LEN     3

// parameters held in the BAM sector
enum { SPK_DISKID, SPK_NEXTID, SPK_SAVER, SPK_IL0R, SPK_IL1R, SPK_IL2R,
       SPK_IL3R, SPK_PRODID, SPK_NUM_PARAMS };

// Where each parameter sits, per revision. 0 means the revision does not have
// it, and get_param answers 0 -- a safe default for all of them. Indexed by
// variant minus IEC_FLV_SPARKLE_10, so every SPARKLE variant needs a row.
static const uint8_t s_spkParams[5][SPK_NUM_PARAMS] = {
  /* DISKID NEXTID SAVER  IL0R   IL1R   IL2R   IL3R   PRODID */
  {  0xFF,  0xFD,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00 },   // 1.0
  {  0xFF,  0xFD,  0x00,  0xF8,  0xFA,  0xFB,  0xFC,  0x00 },   // 1.5
  {  0xFF,  0xFE,  0xF4,  0xF9,  0xFB,  0xFC,  0xFD,  0xF1 },   // 2.0
  {  0xFF,  0xFB,  0xF9,  0xFA,  0xFC,  0xFD,  0xFE,  0xF6 },   // 2.1
  {  0xFF,  0xFB,  0x00,  0xFA,  0xFC,  0xFD,  0xFE,  0xF7 }    // 3.2
};

// Productions needing behaviour the format cannot express.
static const uint8_t s_pidMedian[]      = { 0xBD, 0xE2, 0x0A };
static const uint8_t s_pidMedianFinal[] = { 0xBD, 0x8C, 0xD3 };
static const uint8_t s_pidPropaganda[]  = { 0x92, 0xD2, 0x6F };
static const uint8_t s_pidAloft[]       = { 0x81, 0x6F, 0x7C };

// bootstrap CRCs, which say which revision family the M-E belongs to
#define SPK_BOOT_10 0x656f
#define SPK_BOOT_15 0x36fe
#define SPK_BOOT_2x 0x1874
#define SPK_BOOT_32 0x6b82
#define SPK_BOOT_33 0x8ad2
#define SPK_BOOT_34 0x284c


static uint8_t spkDecodeLow(uint8_t v)
{
  switch( v & 0x09 )
    {
    case 0x00:
    case 0x09: return (uint8_t)(v ^ 0x0F);
    default:   return (uint8_t)(v ^ 0x06);
    }
}


static uint8_t spkDecodeHigh(uint8_t v)
{
  switch( v & 0x90 )
    {
    case 0x00:
    case 0x90: return (uint8_t)(v ^ 0xF0);
    default:   return (uint8_t)(v ^ 0x60);
    }
}


uint8_t IECBusHandler::sparkleDecode(uint8_t enc, uint8_t v)
{
  switch( enc )
    {
    case SPK_ENC_20:   return spkDecodeHigh(spkDecodeLow(v));
    case SPK_ENC_21:   return (uint8_t)(spkDecodeLow(v) ^ 0x70);
    case SPK_ENC_21FF: return (uint8_t)(spkDecodeLow(v) ^ 0xF0);
    default:           return v;   // 1.x is not encoded
    }
}


uint8_t IECBusHandler::sparkleParam(SparkleSession &s, const uint8_t *bam, uint8_t pm)
{
  uint8_t i = s_spkParams[s.variant - IEC_FLV_SPARKLE_10][pm];
  return (i!=0) ? bam[i] : 0;
}


// Decode a directory sector in place. 2.x stores it partially reversed, which
// is why the two halves are swapped as they are decoded.
void IECBusHandler::sparkleDecodeBlock(SparkleSession &s, uint8_t *data)
{
  if( s.enc==SPK_ENC_NONE ) return;

  if( s.dirReversed )
    {
      for(uint16_t i=0; i<=0x80; i++)
        {
          uint8_t j = (uint8_t)(0 - (uint8_t)i);
          uint8_t tmp = sparkleDecode(s.enc, data[i]);
          data[i] = sparkleDecode(s.enc, data[j]);
          data[j] = tmp;
        }
    }
  else
    {
      for(uint16_t i=0; i<256; i++)
        data[i] = sparkleDecode(s.enc, data[i]);
    }
}


bool IECBusHandler::sparkleLoadDir(SparkleSession &s, uint8_t *dirBuf, uint8_t dirIndex)
{
  if( dirIndex > 1 ) return false;
  if( s.currentDir == dirIndex ) return true;

  if( !m_currentDevice->epyxReadSector(SPK_INIT_TRACK, (uint8_t)(SPK_DIR_START + dirIndex), dirBuf) )
    return false;

  s.currentDir = dirIndex;

  if( s.enc==SPK_ENC_NONE ) return true;   // 1.x is not encoded

  if( !s.dirLayoutKnown )
    {
      // The first entry should decode to sector 0 with a count of 21. If it
      // does, this is the older plain layout that some 2.0 pre-releases use
      // (Memento Mori, reMETA) -- and those also invert the bundle number.
      if( sparkleDecode(s.enc, dirBuf[1])==0 && sparkleDecode(s.enc, dirBuf[2])==21 )
        { s.dirReversed = false; s.bundleInv = true; }
      else
        s.dirReversed = true;

      s.dirLayoutKnown = true;
    }

  sparkleDecodeBlock(s, dirBuf);
  return true;
}


void IECBusHandler::sparkleAdvanceSector(SparkleSession &s, uint8_t ds)
{
  s.sector = (uint8_t)(s.sector + ds);

  if( s.sector >= s.numSectors )
    {
      s.sector = (uint8_t)(s.sector - s.numSectors);

      // tracks 1-17 correct by one on overflow; Aloft does it on every track
      if( (s.fullSubsct || s.track < 18) && s.sector > 0 )
        s.sector--;
    }
}


// Move to the next usable sector on this track, skipping ones already used.
// Returns how many remain.
uint8_t IECBusHandler::sparkleIterateSector(SparkleSession &s)
{
  s.used[s.sector >> 3] |= (uint8_t)(1 << (s.sector & 7));

  if( --s.remaining > 0 || !s.hasSkew )
    sparkleAdvanceSector(s, s.currentIl);

  if( s.remaining > 0 )
    while( s.used[s.sector >> 3] & (1 << (s.sector & 7)) )
      sparkleAdvanceSector(s, 1);

  return s.remaining;
}


void IECBusHandler::sparkleTrackChanged(SparkleSession &s)
{
  if( s.track < 18 )      { s.numSectors = 21; s.currentIl = s.interleave[0]; }
  else if( s.track < 25 ) { s.numSectors = 19; s.currentIl = s.interleave[1]; }
  else if( s.track < 31 ) { s.numSectors = 18; s.currentIl = s.interleave[2]; }
  else                    { s.numSectors = 17; s.currentIl = s.interleave[3]; }

  s.remaining = s.numSectors;
  memset(s.used, 0, sizeof(s.used));
}


// Read the BAM sector and set up everything the disk decides. On the first
// call the loader revision and the byte encoding are deduced from it; later
// calls are disk flips and must find the expected side and production ids.
bool IECBusHandler::sparkleInitDisk(SparkleSession &s, uint8_t *dirBuf, uint16_t bootCrc)
{
  if( !m_currentDevice->epyxReadSector(SPK_INIT_TRACK, SPK_BAM_SECTOR, dirBuf) )
    return false;

  if( s.variant==IEC_FLV_NONE )
    {
      if( bootCrc!=SPK_BOOT_32 && bootCrc!=SPK_BOOT_33 && bootCrc!=SPK_BOOT_34 )
        {
          switch( dirBuf[0xF9] & 0xC0 )
            {
            case 0x00:
              {
                // 1.x has [0xF8] == -[0xF9], or 0 for 1.0 which has no
                // custom interleave at all
                uint8_t i = dirBuf[0xF8];
                if( i == (uint8_t)(0 - dirBuf[0xF9]) )
                  {
                    s.enc = SPK_ENC_NONE;
                    if( i!=0 )
                      s.variant = IEC_FLV_SPARKLE_15;
                    else
                      {
                        s.variant = IEC_FLV_SPARKLE_10;
                        s.interleave[0] = 4;
                        s.interleave[1] = 3;
                        s.interleave[2] = 3;
                        s.interleave[3] = 3;
                      }
                  }
                else
                  {
                    s.variant = IEC_FLV_SPARKLE_20;
                    // a side id of 0x10 or more is not expected, which is what
                    // tells the two 2.x encodings apart
                    s.enc = ((dirBuf[0xFE] & 0xC0)==0xC0) ? SPK_ENC_20 : SPK_ENC_21FF;
                  }
                break;
              }

            case 0x80: s.variant = IEC_FLV_SPARKLE_20; s.enc = SPK_ENC_21; break;
            case 0x40: s.variant = IEC_FLV_SPARKLE_21; s.enc = SPK_ENC_21; break;
            default:   return false;
            }
        }
      else
        {
          // every 3.x uses the newer encoding; < 3.2 is told apart by its
          // saver flag and a non-zero P2
          s.enc = SPK_ENC_21;
          if( (dirBuf[0xF9] & 0x7D)==0x7D && dirBuf[0xF6] )
            s.variant = IEC_FLV_SPARKLE_21;
          else
            s.variant = IEC_FLV_SPARKLE_32;
        }

      uint8_t pidOffs = s_spkParams[s.variant - IEC_FLV_SPARKLE_10][SPK_PRODID];
      if( pidOffs!=0 )
        {
          // the production id is only ever compared, never decoded
          memcpy(s.prodId, dirBuf+pidOffs, SPK_PRODID_LEN);

          if( memcmp(s.prodId, s_pidMedian, SPK_PRODID_LEN)==0 ||
              memcmp(s.prodId, s_pidMedianFinal, SPK_PRODID_LEN)==0 )
            s.hasSkew = true;
          else
            {
              s.hasSkew    = false;
              s.hasNsreset = (memcmp(s.prodId, s_pidPropaganda, SPK_PRODID_LEN)==0);
              s.fullSubsct = (memcmp(s.prodId, s_pidAloft, SPK_PRODID_LEN)==0);
            }
        }
    }
  else
    {
      // a flip: side and production ids both have to match, and sd2iec waits
      // for a disk change when they do not. Nothing at this layer reports one,
      // so a mismatch ends the session.
      if( s.nextId != sparkleDecode(s.enc, sparkleParam(s, dirBuf, SPK_DISKID)) )
        return false;

      uint8_t pidOffs = s_spkParams[s.variant - IEC_FLV_SPARKLE_10][SPK_PRODID];
      if( pidOffs!=0 && memcmp(s.prodId, dirBuf+pidOffs, SPK_PRODID_LEN)!=0 )
        return false;
    }

  // interleaves are stored as two's complement
  if( s.variant != IEC_FLV_SPARKLE_10 )
    for(uint8_t i=SPK_IL0R; i<=SPK_IL3R; i++)
      s.interleave[i-SPK_IL0R] = (uint8_t)(~sparkleDecode(s.enc, sparkleParam(s, dirBuf, i)) + 1);

  s.nextId   = sparkleDecode(s.enc, sparkleParam(s, dirBuf, SPK_NEXTID));
  s.hasSaver = (sparkleDecode(s.enc, sparkleParam(s, dirBuf, SPK_SAVER))==2);

  if( s.variant >= IEC_FLV_SPARKLE_20 )
    {
      s.currentDir = 0xFF;   // force a reload
      if( !sparkleLoadDir(s, dirBuf, 0) ) return false;
    }
  else
    {
      // 1.x has no directory and always starts at 1/0
      s.track = 1;
      s.sector = 0;
      sparkleTrackChanged(s);
    }

  return true;
}


// Look up a bundle in the directory and position on its first block.
bool IECBusHandler::sparkleFindDirEntry(SparkleSession &s, uint8_t *dirBuf, uint8_t bundle,
                                        uint8_t &bptr)
{
  if( !sparkleLoadDir(s, dirBuf, (uint8_t)(bundle >> 6)) ) return false;

  const uint8_t *e = dirBuf + (bundle & 0x3F) * 4;
  s.track  = e[0];
  s.sector = e[1];
  uint8_t sctr = e[2];
  bptr = e[3];

  if( (s.track & 0x40) && s.variant >= IEC_FLV_SPARKLE_32 )
    {
      // a custom-code plugin; assume it is the high-score saver
      s.track = (uint8_t)(s.track & ~0x40);
      s.saveActive = true;
    }

  sparkleTrackChanged(s);
  while( s.remaining > sctr ) sparkleIterateSector(s);

  return true;
}


// One byte, clocked by the computer on the request line, least significant bit
// first, with the data line's sense inverted.
bool IECBusHandler::sparkleReadByte(uint8_t &data, uint32_t timeoutMs)
{
  uint8_t b = 0;
  bool prevClk = readPinCLK();

  for(uint8_t i=8; i!=0; i--)
    {
      uint32_t start = micros();
      while( readPinCLK()==prevClk )
        {
          if( !isResetPinIdle() ) return false;
          if( timeoutMs!=0 && (micros()-start) > timeoutMs*1000UL ) return false;
        }

      delayMicrosecondsISafe(2);
      prevClk = readPinCLK();
      b = (uint8_t)((b >> 1) | (readPinDATA() ? 0 : 0x80));
    }

  data = b;
  return true;
}


bool IECBusHandler::sparkleSendBundle(SparkleSession &s, uint8_t *dirBuf, uint8_t bundle)
{
  uint8_t bptr = 0;
  bool    eob = false;

  if( bundle != SPK_SEQ_BUNDLE )
    {
      if( s.variant >= IEC_FLV_SPARKLE_20 )
        if( !sparkleFindDirEntry(s, dirBuf, bundle, bptr) ) return false;

      s.bundleLen = 1;   // the first block carries the real length
    }

  do
    {
      if( !m_currentDevice->epyxReadSector(s.track, s.sector, m_buffer) ) return false;

      if( s.variant != IEC_FLV_SPARKLE_10 )
        {
          if( --s.bundleLen == 0 )
            {
              eob = true;

              uint8_t i = (s.variant < IEC_FLV_SPARKLE_32) ? 0x01 : 0xFF;
              s.bundleLen = sparkleDecode(s.enc, m_buffer[i]);
              m_buffer[i] = 0;

              if( bundle & 0x7F )
                {
                  m_buffer[0] = 0;
                  i = (s.variant < IEC_FLV_SPARKLE_32) ? 0xFF : 0x01;
                  m_buffer[i] = sparkleDecode(s.enc, bptr);
                }
            }
        }
      else
        {
          // 1.0, whose only known user is "OMG Got Balls!"
          switch( --s.bundleLen )
            {
            case 0: s.bundleLen = m_buffer[0xFF]; break;
            case 1: eob = true; break;
            default: break;
            }
        }

      if( !fastWaitATN(LOW, 1000) ) return false;

      if( s.variant != IEC_FLV_SPARKLE_10 )
        {
          writePinDATA(HIGH);
          while( !readPinDATA() )
            if( !isResetPinIdle() ) return false;
          delayMicrosecondsISafe(2);
          if( readPinATN() ) return false;   // host reset
        }

      noInterrupts();
      writePinCLK(LOW);
      writePinDATA(s.variant == IEC_FLV_SPARKLE_10);   // only 1.0 sets ATNA
      bool ok = fastWaitATN(HIGH, 0);

      for(uint16_t i=0; i<256 && ok; i++)
        ok = clockedWriteByte(m_buffer[i], 1000);

      if( ok ) ok = fastWaitATN(HIGH, 0);
      interrupts();
      if( !ok ) return false;

      writePinCLK(HIGH);
      writePinDATA(s.variant != IEC_FLV_SPARKLE_10);

      if( sparkleIterateSector(s)==0 )
        {
          // end of track: carry on, skipping the directory track
          while( ++s.track == SPK_INIT_TRACK ) s.sector += 2;

          sparkleTrackChanged(s);

          if( s.hasSkew )
            {
              s.sector -= SPK_SKEW;
              if( s.track==19 ) s.sector -= 8;   // -6, less the 2 added above
              if( s.sector & 0x80 ) s.sector += s.numSectors;
            }
          else if( s.hasNsreset )
            s.sector = 0;
        }
    }
  while( !eob );

  // this can take a long time on 1.0
  return fastWaitATN(HIGH, 0);
}


// The high-score saver: receive one block and write it back to the disk.
bool IECBusHandler::sparkleHandleSave(SparkleSession &s)
{
  if( s.remaining==0 ) return false;

  if( !fastWaitATN(LOW, 1000) ) return false;

  writePinDATA(HIGH);
  while( !readPinDATA() )
    if( !isResetPinIdle() ) return false;

  noInterrupts();
  bool ok = true;
  for(uint16_t i=0; i<256 && ok; i++)
    ok = sparkleReadByte(m_buffer[i], 1000);
  interrupts();
  if( !ok ) return false;

  if( !m_currentDevice->epyxWriteSector(s.track, s.sector, m_buffer) ) return false;

  sparkleIterateSector(s);
  return true;
}


bool IECBusHandler::runSparkleLoader(const uint8_t *cmd, uint8_t cmdLen)
{
  uint16_t crc = 0xFFFF;
  for(uint8_t i=5; i<cmdLen; i++) crc = iecCrc16Update(crc, cmd[i]);

  if( !((cmdLen==0x25 && crc==SPK_BOOT_34) ||
        (cmdLen==0x26 && crc==SPK_BOOT_33) ||
        (cmdLen==0x28 && crc==SPK_BOOT_32) ||
        (cmdLen==0x22 && crc==SPK_BOOT_2x) ||
        (cmdLen==0x28 && crc==SPK_BOOT_15) ||
        (cmdLen==0x23 && crc==SPK_BOOT_10)) )
    return false;

  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  uint8_t dirBuf[256];
  SparkleSession s;
  memset(&s, 0, sizeof(s));
  s.variant    = IEC_FLV_NONE;
  s.enc        = SPK_ENC_NONE;
  s.currentDir = 0xFF;

  if( !sparkleInitDisk(s, dirBuf, crc) ) goto done;

  {
  uint8_t bundle = 0;
  writePinDATA(LOW);   // drive ready

  while( true )
    {
      if( !fastWaitATN(LOW, 1000) ) break;

      if( s.variant != IEC_FLV_SPARKLE_10 )
        {
          writePinDATA(HIGH);

          while( !readPinDATA() )
            if( readPinATN() || !isResetPinIdle() ) goto done;

          if( s.variant >= IEC_FLV_SPARKLE_20 )
            {
              delayMicrosecondsISafe(2);

              if( !readPinCLK() )
                {
                  // the computer held CLK: it is asking for a random bundle
                  noInterrupts();
                  writePinDATA(LOW);
                  bool ok = sparkleReadByte(bundle, bundle!=0 ? 90 : 0);
                  interrupts();
                  if( !ok ) goto done;

                  writePinDATA(HIGH);

                  // Memento Mori and every known reMETA send it inverted
                  if( s.bundleInv ) bundle = (uint8_t) ~bundle;

                  if( bundle & 0x80 )
                    {
                      if( bundle==0xFF ) goto done;   // reset

                      s.nextId = (uint8_t)(bundle & 0x7F);
                      if( s.variant >= IEC_FLV_SPARKLE_32 ) s.nextId <<= 1;
                      if( !sparkleInitDisk(s, dirBuf, crc) ) goto done;
                      bundle = 0;
                      continue;
                    }
                }
            }
        }

      if( s.saveActive )
        {
          if( bundle != 0 )
            { if( !sparkleHandleSave(s) ) goto done; }
          else
            s.saveActive = false;   // the computer must now ask for a bundle
        }
      else if( s.bundleLen==0 && bundle==SPK_SEQ_BUNDLE )
        {
          if( s.nextId & 0x80 ) goto done;   // no more disks
          if( !sparkleInitDisk(s, dirBuf, crc) ) goto done;
          bundle = 0;
        }
      else
        {
          if( !sparkleSendBundle(s, dirBuf, bundle) ) goto done;

          if( s.variant < IEC_FLV_SPARKLE_20 )
            {
              // 1.x counts the bundles down and flips when it runs out
              if( --dirBuf[SPK_BNDCNT_OFFS] == 0 )
                {
                  if( s.nextId==0 ) goto done;
                  if( !sparkleInitDisk(s, dirBuf, crc) ) goto done;
                  bundle = 0;
                  continue;
                }
            }

          if( bundle==SPK_SAVER_BUNDLE &&
              (s.hasSaver || s.variant >= IEC_FLV_SPARKLE_32) )
            {
              uint8_t bptr;
              if( !sparkleFindDirEntry(s, dirBuf, SPK_SAVE_FILE, bptr) ) goto done;
              if( s.variant < IEC_FLV_SPARKLE_32 ) s.saveActive = true;
            }

          bundle = SPK_SEQ_BUNDLE;
        }
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
