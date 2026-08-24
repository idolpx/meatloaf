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
// GEOS and Wheels session loops. Ported from sd2iec's fl-geos.c (Ingo Korb,
// GPL v2). The byte layer they sit on is in geos.cpp.
//
// Three things here are easy to get wrong and are called out where they
// happen:
//
//   - Every data block travels BACKWARDS. The last byte of the buffer goes out
//     first, and a received block fills from the end towards the front.
//   - A command block is a 16-bit ADDRESS naming a routine inside the code the
//     computer uploaded, so the switch reads as a list of addresses rather
//     than of opcodes. The values are sd2iec's, collected from real GEOS
//     versions; there is nothing to derive them from.
//   - GEOS stage 1 XOR-encrypts every sector chain after the first with a
//     256-byte key that exists only inside the upload, which is why detection
//     has to capture it as it goes past (see fastload.h).
//
// Three Wheels operations ask for things this layer cannot answer -- the free
// block count of a native partition, and the current partition/directory
// pointer. Each is answered with a documented stand-in rather than left to
// fail; those are the first places to look if Wheels misbehaves on a native
// partition.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

#if defined(IEC_FP_GEOS) && defined(IEC_IMPL_SOFTLOAD)

bool IECBusHandler::geosWaitCLK(bool state)
{
  while( readPinCLK()!=state )
    {
      if( !isResetPinIdle() ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        { interrupts(); noInterrupts(); timer_reset(); }
#endif
    }

  return true;
}


// Send one byte after waiting for CLK high, leaving DATA asserted ("busy").
bool IECBusHandler::geosTransmitByteWait(uint8_t rxtx, uint8_t byte)
{
  if( !geosWaitCLK(HIGH) ) return false;
  writePinDATA(HIGH);

  bool ok = transmitGeosByte(rxtx, byte);

  writePinCLK(HIGH);
  writePinDATA(LOW);
  delayMicrosecondsISafe(25);   // sd2iec calls this an educated guess

  return ok;
}


// Blocks travel backwards: the LAST byte of the buffer goes out first.
bool IECBusHandler::geosTransmitBuffer(uint8_t rxtx, const uint8_t *data, uint16_t len)
{
  if( !geosWaitCLK(HIGH) ) return false;
  writePinDATA(HIGH);

  bool ok = true;
  for(uint16_t i=len; i>0 && ok; i--)
    ok = transmitGeosByte(rxtx, data[i-1]);

  writePinCLK(HIGH);
  writePinDATA(LOW);
  delayMicrosecondsISafe(15);

  return ok;
}


bool IECBusHandler::geosReceiveBuffer(uint8_t rxtx, uint8_t *data, uint16_t len)
{
  if( !geosWaitCLK(HIGH) ) return false;
  writePinDATA(HIGH);

  bool ok = true;
  for(uint16_t i=len; i>0 && ok; i--)
    ok = receiveGeosByte(rxtx, data[i-1]);

  writePinDATA(LOW);
  return ok;
}


// A length byte followed by that many data bytes. A length of 0 means 256.
bool IECBusHandler::geosReceiveLenBlock(uint8_t rxtx, uint8_t *data)
{
  if( !geosWaitCLK(HIGH) ) { data[0] = 0; data[1] = 0; return false; }

  writePinDATA(HIGH);
  uint8_t lenByte;
  bool ok = receiveGeosByte(rxtx, lenByte);
  writePinDATA(LOW);
  if( !ok ) { data[0] = 0; data[1] = 0; return false; }

  uint16_t length = (lenByte==0) ? 256 : lenByte;
  return geosReceiveBuffer(rxtx, data, length);
}


// 1 is "job done", 8 is write protect, anything else a generic failure. This
// layer cannot tell write protection apart from any other write error, so a
// failed write always reports 2.
bool IECBusHandler::geosTransmitStatus(uint8_t rxtx, bool ok)
{
  return geosTransmitByteWait(rxtx, 1) && geosTransmitByteWait(rxtx, ok ? 1 : 2);
}


bool IECBusHandler::runGeosLoader(uint8_t rxtx, uint8_t variant)
{
  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  uint8_t cmd[256];
  bool    lastOk = true;

  // initial handshake
  delay(1);
  writePinDATA(LOW);
  if( !geosWaitCLK(LOW) ) { setATNInterruptEnabled(atnInterruptWasEnabled); return true; }

  bool running = true;
  while( running )
    {
      noInterrupts();
      bool ok = geosReceiveLenBlock(rxtx, cmd);
      interrupts();
      if( !ok ) break;

      uint16_t c = (uint16_t)(cmd[0] | (cmd[1] << 8));
      uint8_t  track = cmd[2], sector = cmd[3];

      switch( c )
        {
        case 0x0320:   // 1541 stage 3 transmit
          noInterrupts();
          ok = geosTransmitBuffer(rxtx, m_buffer, 256) && geosTransmitStatus(rxtx, lastOk);
          interrupts();
          break;

        case 0x031f:   // 1571, 1541 stage 2 status, 1581 transmit
          noInterrupts();
          if( variant==IEC_FLV_GEOS_S23_1581 )
            ok = geosTransmitBuffer(rxtx, m_buffer, (track & 0x80) ? 2 : 256);
          if( ok ) ok = geosTransmitStatus(rxtx, lastOk);
          interrupts();
          break;

        case 0x0325:   // 1541 stage 3 status
        case 0x032b:   // 1581 status
          noInterrupts();
          ok = geosTransmitStatus(rxtx, lastOk);
          interrupts();
          break;

        case 0x0000:   // internal quit
        case 0x0412:   // 1541 stage 2 quit
        case 0x0420:   // 1541 stage 3 quit
        case 0x0457:   // 1581 quit
        case 0x0475:   // 1571 stage 3 quit
          geosWaitCLK(HIGH);
          writePinDATA(HIGH);
          setATNInterruptEnabled(atnInterruptWasEnabled);
          return true;

        case 0x0432:   // 1541 stage 2 transmit
          noInterrupts();
          if( !lastOk )
            ok = geosTransmitStatus(rxtx, false);
          else
            ok = geosTransmitByteWait(rxtx, 0) && geosTransmitBuffer(rxtx, m_buffer, 256);
          interrupts();
          break;

        case 0x0439:   // 1541 stage 3 set address
        case 0x04a5:   // 1571 stage 3 set address
          // sd2iec changes the drive's device number here. Meatloaf's device
          // number is configuration rather than something a loader should
          // move, so this is accepted and ignored.
          break;

        case 0x049b:   // 1581 initialize
        case 0x04b9:   // 1581 flush
        case 0x04dc:   // 1541 stage 3 initialize
        case 0x0504:   // 1541 stage 2 initialize
        case 0x057e:   // 1571 initialize
          break;       // nothing here needs doing

        case 0x057c:   // 1541 stage 2/3 write
          noInterrupts();
          ok = geosReceiveLenBlock(rxtx, m_buffer);
          interrupts();
          if( ok ) lastOk = m_currentDevice->epyxWriteSector(track, sector, m_buffer);
          break;

        case 0x058e:   // 1541 stage 2/3 read
        case 0x04cc:   // 1581 read
          lastOk = m_currentDevice->epyxReadSector(track & 0x7F, sector, m_buffer);
          break;

        case 0x04af:   // 1571 read and send
          lastOk = m_currentDevice->epyxReadSector(track, sector, m_buffer);
          noInterrupts();
          ok = geosTransmitBuffer(rxtx, m_buffer, 256) && geosTransmitStatus(rxtx, lastOk);
          interrupts();
          break;

        case 0x047c:   // 1581 write
        case 0x05fe:   // 1571 write
          noInterrupts();
          ok = geosReceiveBuffer(rxtx, m_buffer, 256);
          interrupts();
          if( ok ) lastOk = m_currentDevice->epyxWriteSector(track, sector, m_buffer);
          if( ok )
            {
              noInterrupts();
              ok = geosTransmitStatus(rxtx, lastOk);
              interrupts();
            }
          break;

        default:
          // an address we do not know: leave rather than guess
          running = false;
          break;
        }

      if( !ok ) break;
    }

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}


// Stage 1 sends whole sector chains. Every chain after the first is XOR-ed
// with the key detection captured out of the upload -- without it the computer
// receives noise, so a missing key means declining rather than sending
// something that cannot work.
bool IECBusHandler::geosSendChain(uint8_t rxtx, uint8_t track, uint8_t sector, const uint8_t *key)
{
  while( true )
    {
      if( !m_currentDevice->epyxReadSector(track, sector, m_buffer) )
        return false;

      if( key!=NULL )
        for(uint16_t i=0; i<254; i++)
          m_buffer[i+2] ^= key[i];

      uint8_t nextTrack  = m_buffer[0];
      uint8_t nextSector = m_buffer[1];

      // on the last block the link is 0 and the second byte is the index of
      // the last used byte
      uint8_t bytes = (nextTrack==0) ? (uint8_t)(nextSector - 1) : 254;

      noInterrupts();
      bool ok = geosTransmitByteWait(rxtx, bytes)
             && geosTransmitBuffer(rxtx, m_buffer+2, bytes);
      interrupts();
      if( !ok ) return false;

      if( nextTrack==0 ) break;
      track  = nextTrack;
      sector = nextSector;
    }

  noInterrupts();
  bool ok = geosTransmitByteWait(rxtx, 0);
  interrupts();
  return ok;
}


bool IECBusHandler::runGeosStage1Loader(uint8_t rxtx, uint8_t variant, const uint8_t *key)
{
  // Which chains to send is fixed per GEOS version; these are sd2iec's.
  static const uint8_t chains64 [] = { 19,13, 20,15, 20,17, 0 };
  static const uint8_t chains128[] = { 19,12, 20,15, 23,6, 24,4, 0 };

  const uint8_t *chain = (variant==IEC_FLV_GEOS_S1_128) ? chains128 : chains64;

  // No key means the upload never carried one past the capture window, and
  // every chain after the first would go out as noise.
  if( key==NULL ) return false;

  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  delay(1);
  writePinDATA(LOW);
  if( !geosWaitCLK(LOW) ) { setATNInterruptEnabled(atnInterruptWasEnabled); return true; }

  const uint8_t *useKey = NULL;   // the first chain goes out in the clear
  while( *chain != 0 )
    {
      uint8_t track  = *chain++;
      uint8_t sector = *chain++;

      if( !geosSendChain(rxtx, track, sector, useKey) ) break;

      useKey = key;
    }

  writePinDATA(HIGH);
  writePinCLK(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}


#ifdef IEC_FP_WHEELS

// Wheels 4.4 at 1581 speed adds a second handshake after every block: it
// releases DATA, waits for CLK to fall, and only then asserts DATA again.
bool IECBusHandler::wheelsTransmitBuffer(uint8_t rxtx, uint8_t variant, const uint8_t *data, uint16_t len)
{
  if( variant==IEC_FLV_WHEELS44_S2_1581 )
    {
      if( !geosWaitCLK(HIGH) ) return false;
      writePinDATA(HIGH);

      bool ok = true;
      for(uint16_t i=len; i>0 && ok; i--)
        ok = transmitGeosByte(rxtx, data[i-1]);

      writePinCLK(HIGH);
      writePinDATA(HIGH);
      delayMicrosecondsISafe(5);
      if( !geosWaitCLK(LOW) ) return false;
      writePinDATA(LOW);
      delayMicrosecondsISafe(15);
      return ok;
    }

  if( !geosTransmitBuffer(rxtx, data, len) ) return false;
  return geosWaitCLK(LOW);
}


bool IECBusHandler::wheelsTransmitByteWait(uint8_t rxtx, uint8_t variant, uint8_t byte)
{
  if( variant==IEC_FLV_WHEELS44_S2_1581 )
    {
      if( !geosWaitCLK(HIGH) ) return false;
      writePinDATA(HIGH);
      bool ok = transmitGeosByte(rxtx, byte);
      writePinCLK(HIGH);
      writePinDATA(HIGH);
      delayMicrosecondsISafe(5);
      if( !geosWaitCLK(LOW) ) return false;
      writePinDATA(LOW);
      delayMicrosecondsISafe(15);
      return ok;
    }

  if( !geosTransmitByteWait(rxtx, byte) ) return false;
  delayMicrosecondsISafe(15);
  return geosWaitCLK(LOW);
}


bool IECBusHandler::wheelsReceiveBuffer(uint8_t rxtx, uint8_t variant, uint8_t *data, uint16_t len)
{
  if( !geosWaitCLK(HIGH) ) return false;
  writePinDATA(HIGH);

  bool ok = true;
  for(uint16_t i=len; i>0 && ok; i--)
    ok = receiveGeosByte(rxtx, data[i-1]);

  if( variant==IEC_FLV_WHEELS44_S2 || variant==IEC_FLV_WHEELS44_S2_1581 )
    if( !geosWaitCLK(LOW) ) return false;

  writePinDATA(LOW);
  return ok;
}


bool IECBusHandler::runWheelsStage1Loader(uint8_t rxtx, uint8_t variant)
{
  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  delay(2);
  if( !geosWaitCLK(LOW) ) { setATNInterruptEnabled(atnInterruptWasEnabled); return true; }
  writePinDATA(LOW);

  // the file to send is fixed per version
  const char *name = (variant==IEC_FLV_WHEELS_S1_128) ? "128SYSTEM1" : "SYSTEM1";

  m_currentDevice->listen(0xF0);
  bool ok = true;
  for(const char *p = name; *p && ok; p++)
    {
      int8_t w;
      while( (w = m_currentDevice->canWrite())<0 )
        if( !isResetPinIdle() ) { ok = false; break; }

      if( ok && w>0 )
        m_currentDevice->write((uint8_t)*p, p[1]==0);
      else
        ok = false;
    }
  m_currentDevice->unlisten();

  while( ok )
    {
      m_currentDevice->talk(0);
      m_inTask = false;
      uint8_t n = m_currentDevice->read(m_buffer, 254);
      m_inTask = true;
      if( n==0 ) break;

      // the protocol moves whole 256-byte sectors; a short read is padded
      for(uint16_t i=n; i<256; i++) m_buffer[i] = 0;

      noInterrupts();
      ok = wheelsTransmitBuffer(rxtx, variant, m_buffer, 256);
      interrupts();

      if( n < 254 ) break;
    }

  m_currentDevice->listen(0xE0);
  m_currentDevice->unlisten();

  geosWaitCLK(HIGH);
  writePinDATA(HIGH);
  writePinCLK(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}


bool IECBusHandler::runWheelsStage2Loader(uint8_t rxtx, uint8_t variant)
{
  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  delay(1);
  if( !geosWaitCLK(LOW) ) { setATNInterruptEnabled(atnInterruptWasEnabled); return true; }
  writePinDATA(LOW);
  writePinCLK(HIGH);
  delayMicrosecondsISafe(3);

  bool lastOk = true;
  bool running = true;

  while( running )
    {
      if( !geosWaitCLK(HIGH) ) break;

      uint8_t cmd[4];
      noInterrupts();
      bool ok = wheelsReceiveBuffer(rxtx, variant, cmd, 4);
      interrupts();
      if( !ok ) break;

      // cmd[0]/cmd[1] are an address in the uploaded code; only its low byte
      // distinguishes the operations
      switch( cmd[0] )
        {
        case 0x03:   // QUIT
          geosWaitCLK(HIGH);
          writePinDATA(HIGH);
          setATNInterruptEnabled(atnInterruptWasEnabled);
          return true;

        case 0x06:   // WRITE
          noInterrupts();
          ok = wheelsReceiveBuffer(rxtx, variant, m_buffer, 256);
          interrupts();
          if( ok ) lastOk = m_currentDevice->epyxWriteSector(cmd[2], cmd[3], m_buffer);
          if( ok )
            {
              noInterrupts();
              ok = wheelsTransmitByteWait(rxtx, variant, lastOk ? 1 : 2);
              interrupts();
            }
          break;

        case 0x09:   // READ
        case 0x0c:   // READLINK -- the same, but only the two link bytes
          {
            uint16_t bytes = (cmd[0]==0x0c) ? 2 : 256;
            lastOk = m_currentDevice->epyxReadSector(cmd[2], cmd[3], m_buffer);
            noInterrupts();
            ok = wheelsTransmitBuffer(rxtx, variant, m_buffer, bytes)
              && wheelsTransmitByteWait(rxtx, variant, lastOk ? 1 : 2);
            interrupts();
            break;
          }

        case 0x0f:   // STATUS
          noInterrupts();
          ok = wheelsTransmitByteWait(rxtx, variant, lastOk ? 1 : 2);
          interrupts();
          break;

        case 0x12:   // NATIVE_FREE
          {
            // sd2iec answers with the real free block count and notes that it
            // deliberately ignores the limits the computer sets. Nothing at
            // this layer knows the free count of a native partition, so this
            // reports the maximum -- erring the same way that comment does,
            // rather than claiming a full disk.
            uint8_t freeBlocks[2] = { 0xFF, 0xFF };
            noInterrupts();
            ok = wheelsTransmitBuffer(rxtx, variant, freeBlocks, 2)
              && wheelsTransmitByteWait(rxtx, variant, 1);
            interrupts();
            break;
          }

        case 0x15:   // GET_CURRENT_PART_DIR
          {
            // No partition/directory pointer exists at this layer; answer with
            // the standard CBM directory block and partition 1.
            uint8_t dir[3] = { 18, 1, 1 };
            noInterrupts();
            ok = wheelsTransmitBuffer(rxtx, variant, dir, 3);
            interrupts();
            break;
          }

        case 0x18:   // SET_CURRENT_PART_DIR
          {
            uint8_t dir[3];
            noInterrupts();
            ok = wheelsReceiveBuffer(rxtx, variant, dir, 3);
            interrupts();
            // accepted and ignored -- see GET_CURRENT_PART_DIR
            break;
          }

        case 0x1b:   // CHECK_CHANGE
          // Nothing here tracks disk changes, so always report "unchanged".
          noInterrupts();
          ok = wheelsTransmitByteWait(rxtx, variant, 0);
          interrupts();
          if( ok ) ok = geosWaitCLK(LOW);
          break;

        default:
          running = false;
          break;
        }

      if( !ok ) break;
      if( running && !geosWaitCLK(LOW) ) break;
    }

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}

#endif // IEC_FP_WHEELS

#endif
