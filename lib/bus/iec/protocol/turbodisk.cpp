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
// Turbodisk. Ported from sd2iec's fl-turbodisk.c (Ingo Korb, GPL v2).
//
// Turbodisk is clocked by the drive, not by the computer: after a handshake
// the receiver samples CLK and DATA at fixed times, two bits at a time, most
// significant pair first. The times below are sd2iec's, converted from its
// 100ns units to microseconds (fastloader-ll.c, turbodisk_byte_def).
//
// Two forms exist and they are NOT interchangeable -- the receiver decides
// which one it is running. The single-byte form re-handshakes for every byte;
// the block form handshakes once and then streams 254 bytes back to back.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

// ------------------------------------  Turbodisk support routines  ------------------------------------

#ifdef IEC_FP_TURBODISK


#define TURBODISK_BYTE_PAIR0   31
#define TURBODISK_BYTE_PAIR1   60
#define TURBODISK_BYTE_PAIR2   89
#define TURBODISK_BYTE_PAIR3  118
#define TURBODISK_BYTE_END    147

// block form: 43us to the first pair, 29us between pairs, 51us from the last
// pair of one byte to the first pair of the next, 11us of tail
#define TURBODISK_BLOCK_FIRST  43
#define TURBODISK_BLOCK_PAIR   29
#define TURBODISK_BLOCK_GAP    51


// wait for the receiver's handshake: it pulls DATA low, we release CLK, it
// releases DATA again. Returns false if ATN is asserted while waiting, which
// means the computer has given up on us.
bool RAMFUNC(IECBusHandler::waitTurbodiskHandshake)()
{
  if( !waitPinDATA(LOW, 0) ) return false;
  writePinCLK(HIGH);
  if( !waitPinDATA(HIGH, 0) ) return false;
  return true;
}


bool RAMFUNC(IECBusHandler::transmitTurbodiskByte)(uint8_t data)
{
  if( !waitTurbodiskHandshake() ) return false;

  timer_init();
  timer_reset();
  timer_start();

  timer_wait_until(TURBODISK_BYTE_PAIR0);
  writePinCLK (data & bit(7));
  writePinDATA(data & bit(6));

  timer_wait_until(TURBODISK_BYTE_PAIR1);
  writePinCLK (data & bit(5));
  writePinDATA(data & bit(4));

  timer_wait_until(TURBODISK_BYTE_PAIR2);
  writePinCLK (data & bit(3));
  writePinDATA(data & bit(2));

  timer_wait_until(TURBODISK_BYTE_PAIR3);
  writePinCLK (data & bit(1));
  writePinDATA(data & bit(0));

  // leave with CLK low ("not ready") and DATA released
  timer_wait_until(TURBODISK_BYTE_END);
  writePinCLK(LOW);
  writePinDATA(HIGH);
  timer_wait_until(TURBODISK_BYTE_END+5);

  return true;
}


// stream "len" bytes after a single handshake. All times are measured from
// that handshake so a late edge does not accumulate into the next one.
bool RAMFUNC(IECBusHandler::transmitTurbodiskBuffer)(const uint8_t *data, uint8_t len)
{
  if( !waitTurbodiskHandshake() ) return false;

  timer_init();
  timer_reset();
  timer_start();

  uint32_t t = TURBODISK_BLOCK_FIRST;
  while( len-- )
    {
      uint8_t byte = *data++;
      for(uint8_t pair=0; pair<4; pair++)
        {
          timer_wait_until(t);
          writePinCLK (byte & 0x80);
          writePinDATA(byte & 0x40);
          byte <<= 2;
          t += (pair==3) ? TURBODISK_BLOCK_GAP : TURBODISK_BLOCK_PAIR;
        }
    }

  // t is already one gap past the final pair, which is where the tail goes
  timer_wait_until(t);
  writePinCLK(LOW);
  writePinDATA(HIGH);
  timer_wait_until(t+5);

  return true;
}


// Serve one block. Called repeatedly from handleFastLoadProtocols() until it
// returns false, so the bus is only held for the length of a single block
// rather than for the whole file -- sd2iec blocks for the entire transfer
// because an AVR has no other work to get back to.
bool RAMFUNC(IECBusHandler::transmitTurbodiskBlock)()
{
  bool first = (m_currentDevice->m_flFlags & S_TURBODISK_FIRST)!=0;

  // On the first block the two load-address bytes are sent ahead of the block
  // proper, so that block carries 256 bytes of the file rather than 254.
  //
  // Note sd2iec sends the address bytes and then still streams a full 254
  // bytes from the same 254-byte sector buffer, i.e. its last two bytes come
  // from past the end of that buffer. Reading 256 bytes here sends the file's
  // own bytes in those two positions instead. NOT hardware verified -- if a
  // Turbodisk load comes back corrupt from byte 254 onwards, this is the
  // first thing to change.
  //
  // A second case is untested for the same reason: a file whose length lands
  // exactly on a block boundary ends with a final block carrying a count of 1
  // and no data bytes. sd2iec cannot emit that shape, because its
  // lastused/position bookkeeping never leaves an empty final buffer, so
  // whether the receiver accepts it is unknown. If a load of a file whose
  // size is 2 + a multiple of 254 hangs at the end, this is why.
  uint16_t want = first ? 256 : 254;

  // IECDevice::read() takes a uint8_t length, so 256 needs two calls
  m_currentDevice->talk(0);
  m_inTask = false;
  uint16_t n = m_currentDevice->read(m_buffer, want>255 ? 255 : (uint8_t) want);
  if( n==255 ) n += m_currentDevice->read(m_buffer+255, 1);
  m_inTask = true;
  if( (m_flags & P_ATN) || !readPinATN() ) return false;

  bool last = (n < want);

  noInterrupts();

  // status byte: 0 means this is the final block, 1 means more follow
  if( !transmitTurbodiskByte(last ? 0 : 1) ) { interrupts(); return false; }

  const uint8_t *p = m_buffer;
  if( first )
    {
      if( !transmitTurbodiskByte(*p++) ) { interrupts(); return false; }
      if( !transmitTurbodiskByte(*p++) ) { interrupts(); return false; }
      n = (n>=2) ? n-2 : 0;
      m_currentDevice->m_flFlags &= ~S_TURBODISK_FIRST;
    }

  if( last )
    {
      // final block is sent byte by byte, preceded by a count. The count is
      // one more than the number of bytes that follow -- sd2iec sends
      // "lastused - position + 2", which works out to that.
      if( !transmitTurbodiskByte(n+1) ) { interrupts(); return false; }
      for(uint8_t i=0; i<n; i++)
        if( !transmitTurbodiskByte(p[i]) ) { interrupts(); return false; }
    }
  else if( !transmitTurbodiskBuffer(p, 254) )
    { interrupts(); return false; }

  interrupts();

  return !last;
}


bool IECBusHandler::startTurbodiskLoad(const uint8_t *cmd, uint8_t cmdLen)
{
  // The M-E carries the name of the file to load: a length byte at offset 9
  // and the name itself from offset 10.
  if( cmdLen<10 ) return false;
  uint8_t n = cmd[9];
  if( n==0 || n>cmdLen-10 ) return false;

  // signal "not ready" while the file is being opened
  writePinCLK(LOW);

  // initiate DOS OPEN command in the device (open channel #0)
  m_currentDevice->listen(0xF0);
  for(uint8_t i=0; i<n; i++)
    {
      int8_t ok;
      while( (ok = m_currentDevice->canWrite())<0 )
        if( !readPinATN() ) return false;

      if( ok==0 ) return false;
      m_currentDevice->write(cmd[10+i], i<n-1);
    }
  m_currentDevice->unlisten();

  m_currentDevice->m_flFlags |= S_TURBODISK_FIRST;
  m_currentDevice->fastLoadRequest(IEC_FP_TURBODISK, IEC_FL_PROT_LOAD);
  return true;
}

#endif


#ifdef IEC_IMPL_SOFTLOAD

bool IECBusHandler::runFastLoader(IECDevice *dev, uint8_t variant, uint8_t param, uint8_t rxtx, const uint8_t *cmd, uint8_t cmdLen, const uint8_t *captured)
{
  m_currentDevice = dev;
  (void) param;
  (void) rxtx;
  (void) captured;

  switch( variant )
    {
#ifdef IEC_FP_TURBODISK
    case IEC_FLV_TURBODISK:
      return startTurbodiskLoad(cmd, cmdLen);
#endif

#if defined(IEC_FP_GIJOE) && defined(IEC_IMPL_SOFTLOAD)
    case IEC_FLV_GI_JOE:
      return runGIJoeLoader();
#endif

#if defined(IEC_FP_NIPPON) && defined(IEC_IMPL_SOFTLOAD)
    case IEC_FLV_NIPPON:
      return runNipponLoader();
#endif

#if defined(IEC_FP_ULOAD3) && defined(IEC_IMPL_SOFTLOAD)
    case IEC_FLV_ULOAD3:
      return runULoad3Loader();
#endif

#if defined(IEC_FP_ELOAD1) && defined(IEC_IMPL_SOFTLOAD)
    case IEC_FLV_ELOAD1:
      return runELoad1Loader();
#endif

#if defined(IEC_FP_MMZAK) && defined(IEC_IMPL_SOFTLOAD)
    case IEC_FLV_MMZAK:
      return runMMZakLoader();
#endif

#if defined(IEC_FP_N0SDOS) && defined(IEC_IMPL_SOFTLOAD)
    case IEC_FLV_N0SDOS_FILEREAD:
      return runN0SDOSLoader();
#endif

#if defined(IEC_FP_SAMSJOURNEY) && defined(IEC_IMPL_SOFTLOAD)
    case IEC_FLV_SAMSJOURNEY:
      return runSamsJourneyLoader();
#endif

#if defined(IEC_FP_FC3) && defined(IEC_IMPL_SOFTLOAD)
    case IEC_FLV_FC3_OLDFREEZED:
      return runFC3OldFreezeLoader(rxtx);
#endif

    // A "bus silence" request. The loader is uploading this to EVERY drive on
    // the bus so that only the one it is talking to answers -- sd2iec puts the
    // whole drive to sleep here, and the equivalent on a board hosting multiple
    // virtual drives is to give this one the bus alone until RESET.
    case IEC_FLV_KRILL_SLEEP:
    case IEC_FLV_SPINDLE_SLEEP:
    case IEC_FLV_BITFIRE_SLEEP:
    case IEC_FLV_TRANSWARP_SLEEP:
      setBusExclusive(dev);
      return true;

#if defined(IEC_FP_KRILL) && defined(IEC_IMPL_SOFTLOAD)
    case IEC_FLV_KRILL_R58PRE: case IEC_FLV_KRILL_R58:
    case IEC_FLV_KRILL_R146:   case IEC_FLV_KRILL_R159:
    case IEC_FLV_KRILL_R164:   case IEC_FLV_KRILL_R184:
    case IEC_FLV_KRILL_R186:   case IEC_FLV_KRILL_R192:
      return runKrillLoader(rxtx, variant, cmd, cmdLen);
#endif

#if defined(IEC_FP_BITFIRE) && defined(IEC_IMPL_SOFTLOAD)
    case IEC_FLV_BITFIRE_01:  case IEC_FLV_BITFIRE_03:  case IEC_FLV_BITFIRE_04:
    case IEC_FLV_BITFIRE_06:  case IEC_FLV_BITFIRE_07PRE:
    case IEC_FLV_BITFIRE_07DBG: case IEC_FLV_BITFIRE_07:
    case IEC_FLV_BITFIRE_10:  case IEC_FLV_BITFIRE_11:
    case IEC_FLV_BITFIRE_12PR1: case IEC_FLV_BITFIRE_12PR2:
    case IEC_FLV_BITFIRE_12:  case IEC_FLV_BITFIRE_13:
      // the handler table's parameter selects the block header layout
      return runBitfireLoader(rxtx, variant, param);
#endif

#if defined(IEC_FP_BOOZE) && defined(IEC_IMPL_SOFTLOAD)
    case IEC_FLV_BOOZE:
      return runBoozeLoader();
#endif

#if defined(IEC_FP_DREAMLOAD) && defined(IEC_IMPL_SOFTLOAD)
    case IEC_FLV_DREAMLOAD:
    case IEC_FLV_DREAMLOAD_OLD:
      return runDreamloadLoader();
#endif

#if defined(IEC_FP_GEOS) && defined(IEC_IMPL_SOFTLOAD)
    case IEC_FLV_GEOS_S23_1541:
    case IEC_FLV_GEOS_S23_1571:
    case IEC_FLV_GEOS_S23_1581:
      return runGeosLoader(rxtx, variant);

    case IEC_FLV_GEOS_S1_64:
    case IEC_FLV_GEOS_S1_128:
      // stage 1 needs the decryption key detection lifted out of the upload
      return runGeosStage1Loader(rxtx, variant, captured);
#endif

#if defined(IEC_FP_WHEELS) && defined(IEC_IMPL_SOFTLOAD)
    case IEC_FLV_WHEELS_S1_64:
    case IEC_FLV_WHEELS_S1_128:
      return runWheelsStage1Loader(rxtx, variant);

    case IEC_FLV_WHEELS_S2:
    case IEC_FLV_WHEELS44_S2:
    case IEC_FLV_WHEELS44_S2_1581:
      return runWheelsStage2Loader(rxtx, variant);
#endif

#if defined(IEC_FP_ULTRABOOT) && defined(IEC_IMPL_SOFTLOAD)
    case IEC_FLV_NONE:
      // A catch-all row: the M-E address matched but no CRC identified a
      // loader, so whoever handles it identifies itself from the command.
      // Each identifies itself from the command; the first one that claims
      // it wins.
      if( runUltrabootLoader(cmd, cmdLen) ) return true;
#if defined(IEC_FP_SPINDLE) && defined(IEC_IMPL_SOFTLOAD)
      if( runSpindleLoader(cmd, cmdLen) ) return true;
#endif
#if defined(IEC_FP_SPARKLE) && defined(IEC_IMPL_SOFTLOAD)
      if( runSparkleLoader(cmd, cmdLen) ) return true;
#endif
      return false;
#endif

    default:
      // detected but not served here -- the caller lets the M-E through so the
      // computer falls back to the standard protocol
      (void) cmd;
      (void) cmdLen;
      return false;
    }
}

#endif
