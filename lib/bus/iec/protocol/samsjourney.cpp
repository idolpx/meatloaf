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
// Sam's Journey. Ported from sd2iec's fl-samsjourney.c (Ingo Korb, GPL v2).
//
// The game keeps its data in files whose names are two hex digits, and asks
// for them either by that name byte or by a track/sector pair which is just
// the same byte in disguise (track 1-16 in the high nibble, sector 0-15 in the
// low one). So every command reduces to a file name, and the loader never
// touches the sector hooks.
//
// Sending is clocked by ATN, not by CLK: each of the four steps waits for the
// next ATN transition and then puts two bits on CLK and DATA. Receiving is
// fully handshaked, like N0SDOS's but releasing both lines at the START of
// each bit rather than at the end. Neither direction has absolute timing.
//
// A block goes out as a length byte (data length + 2), a continue marker, and
// then the data. Marker 0 means more follows, 1 means this was the last block,
// and 0xFF is an error.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"
#include <string.h>

#if defined(IEC_FP_SAMSJOURNEY) && defined(IEC_IMPL_SOFTLOAD)

static const char s_hexchars[17] = "0123456789ABCDEF";


bool RAMFUNC(IECBusHandler::samsWaitATN)(bool state)
{
  while( readPinATN()!=state )
    {
      if( !isResetPinIdle() ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        { interrupts(); noInterrupts(); timer_reset(); }
#endif
    }

  return true;
}


// Receive one byte, least significant bit first. Returns false when the
// computer asserts ATN, which ends the session.
bool RAMFUNC(IECBusHandler::receiveSamsByte)(uint8_t &data)
{
  uint8_t value = 0;

  timer_init();
  timer_reset();
  timer_start();

  for(uint8_t i=0; i<8; i++)
    {
      writePinCLK(HIGH);
      writePinDATA(HIGH);
      delayMicrosecondsISafe(2);

      bool clk, dat;
      while( true )
        {
          if( !readPinATN() || !isResetPinIdle() ) return false;
          clk = readPinCLK();
          dat = readPinDATA();
          if( !clk || !dat ) break;
#ifdef ESP_PLATFORM
          if( !timer_less_than(IWDT_FEED_TIME) )
            { interrupts(); noInterrupts(); timer_reset(); }
#endif
        }

      value >>= 1;
      if( !dat ) value |= 0x80;

      // acknowledge on the line the computer did NOT use
      if( dat ) writePinDATA(LOW); else writePinCLK(LOW);
      delayMicrosecondsISafe(2);

      while( true )
        {
          if( !readPinATN() || !isResetPinIdle() ) return false;
          if( readPinCLK() || readPinDATA() ) break;
#ifdef ESP_PLATFORM
          if( !timer_less_than(IWDT_FEED_TIME) )
            { interrupts(); noInterrupts(); timer_reset(); }
#endif
        }
    }

  data = value;
  return true;
}


// Four ATN transitions carry eight bits. The byte is inverted first, so a set
// bit leaves its line released.
bool RAMFUNC(IECBusHandler::transmitSamsByte)(uint8_t value)
{
  uint8_t v = ~value;

  timer_init();
  timer_reset();
  timer_start();

  if( !samsWaitATN(HIGH) ) return false;
  writePinCLK (v & 0x80);
  writePinDATA(v & 0x20);

  if( !samsWaitATN(LOW) ) return false;
  writePinCLK (v & 0x40);
  writePinDATA(v & 0x10);

  if( !samsWaitATN(HIGH) ) return false;
  writePinCLK (v & 0x08);
  writePinDATA(v & 0x02);

  if( !samsWaitATN(LOW) ) return false;
  writePinCLK (v & 0x04);
  writePinDATA(v & 0x01);

  return true;
}


bool IECBusHandler::transmitSamsBlock(uint8_t marker, uint8_t length, const uint8_t *data)
{
  noInterrupts();

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  bool ok = samsWaitATN(LOW);

  if( ok )
    {
      writePinCLK(LOW);
      writePinDATA(LOW);

      ok = transmitSamsByte((uint8_t)(length + 2)) && transmitSamsByte(marker);
      for(uint8_t i=0; i<length && ok; i++)
        ok = transmitSamsByte(data[i]);

      if( ok ) ok = samsWaitATN(HIGH);

      writePinCLK(LOW);
      writePinDATA(LOW);
    }

  interrupts();
  return ok;
}


// Open a file whose name is the two hex digits of "name". "replace" prefixes
// the CBM "@:" so a save overwrites.
bool IECBusHandler::samsOpenByName(uint8_t channel, uint8_t name, bool replace)
{
  char cmd[4];
  uint8_t n = 0;
  if( replace ) { cmd[n++] = '@'; cmd[n++] = ':'; }
  cmd[n++] = s_hexchars[name >> 4];
  cmd[n++] = s_hexchars[name & 0x0F];

  m_currentDevice->listen(0xF0 | (channel & 0x0F));
  bool ok = true;
  for(uint8_t i=0; i<n && ok; i++)
    {
      int8_t w;
      while( (w = m_currentDevice->canWrite())<0 )
        if( !isResetPinIdle() ) { ok = false; break; }

      if( ok && w>0 )
        m_currentDevice->write(cmd[i], i==n-1);
      else
        ok = false;
    }
  m_currentDevice->unlisten();

  return ok;
}


bool IECBusHandler::samsReadFile(uint8_t name)
{
  if( !samsOpenByName(0, name, false) )
    return transmitSamsBlock(0xFF, 0, NULL);

  bool ok = true;
  while( ok )
    {
      m_currentDevice->talk(0);
      m_inTask = false;
      uint8_t n = m_currentDevice->read(m_buffer, 254);
      m_inTask = true;

      bool last = (n < 254);
      ok = transmitSamsBlock(last ? 1 : 0, n, m_buffer);
      if( last ) break;
    }

  m_currentDevice->listen(0xE0);
  m_currentDevice->unlisten();
  return ok;
}


bool IECBusHandler::samsWriteFile(uint8_t name, bool &aborted)
{
  aborted = true;

  if( !samsOpenByName(1, name, true) )
    { aborted = false; return transmitSamsBlock(0xFF, 0, NULL); }

  // success marker: an empty block
  if( !transmitSamsBlock(0, 0, NULL) ) return false;

  bool ok = true;
  while( ok )
    {
      uint8_t length;
      noInterrupts();
      ok = receiveSamsByte(length);
      interrupts();
      if( !ok ) break;

      if( length==0 ) { aborted = false; break; }

      while( length-- > 0 )
        {
          uint8_t b;
          noInterrupts();
          ok = receiveSamsByte(b);
          interrupts();
          if( !ok ) break;

          m_currentDevice->listen(0xE1);
          int8_t w;
          while( (w = m_currentDevice->canWrite())<0 )
            if( !isResetPinIdle() ) { ok = false; break; }

          if( ok && w>0 )
            m_currentDevice->write(b, false);
          else
            ok = false;
          m_currentDevice->unlisten();
        }
    }

  m_currentDevice->listen(0xE1);
  m_currentDevice->unlisten();
  return ok;
}


// sd2iec walks its own directory structures here, which nothing at this layer
// can do. The listing is therefore read the way any other client reads it --
// open "$" and parse the BASIC lines it produces -- and only PRG entries are
// reported, which is what the game stores.
//
// Entries go out one behind: each new one is sent with marker 0 and the LAST
// is sent with marker 1, which is how the computer knows the listing ended.
// A directory with no PRG at all still sends one entry, {0xFF, 0, 0}, because
// the protocol has no way to say "nothing here".
bool IECBusHandler::samsScanDirectory()
{
  // open "$" on channel 0
  m_currentDevice->listen(0xF0);
  bool ok = true;
  int8_t w;
  while( (w = m_currentDevice->canWrite())<0 )
    if( !isResetPinIdle() ) { ok = false; break; }
  if( ok && w>0 ) m_currentDevice->write('$', true); else ok = false;
  m_currentDevice->unlisten();

  if( !ok ) return transmitSamsBlock(0xFF, 0, NULL);

  uint8_t entry[3] = { 0xFF, 0, 0 };
  bool    havePending = false;

  // Parse the listing a line at a time. Each BASIC line ends with a NUL, and
  // an entry line carries its name in quotes followed by its type.
  char    line[64];
  uint8_t linePos = 0;
  bool    more = true;

  while( more && ok )
    {
      m_currentDevice->talk(0);
      m_inTask = false;
      uint8_t n = m_currentDevice->read(m_buffer, 254);
      m_inTask = true;
      if( n < 254 ) more = false;

      for(uint8_t i=0; i<n; i++)
        {
          uint8_t c = m_buffer[i];
          if( c!=0 )
            {
              if( linePos < sizeof(line)-1 ) line[linePos++] = (char) c;
              continue;
            }

          line[linePos] = 0;

          // an entry line looks like: <blocks> "NAME"<pad> PRG
          const char *q1 = strchr(line, '"');
          const char *q2 = q1 ? strchr(q1+1, '"') : NULL;
          if( q2 && strstr(q2+1, "PRG")!=NULL )
            {
              if( havePending )
                { ok = transmitSamsBlock(0, sizeof(entry), entry); if( !ok ) break; }

              entry[0] = samsHexPairToByte(q1+1, (uint8_t)(q2-q1-1));
              entry[1] = (uint8_t)((entry[0] >> 4) + 1);
              entry[2] = (uint8_t)(entry[0] & 0x0F);
              havePending = true;
            }

          linePos = 0;
        }
    }

  m_currentDevice->listen(0xE0);
  m_currentDevice->unlisten();

  if( !ok ) return false;

  // the last entry (or the "nothing found" placeholder) ends the listing
  return transmitSamsBlock(1, sizeof(entry), entry);
}


// The game's file names are two hex digits. Anything else answers 0xFF, which
// is what sd2iec's hex2bin does for a name it cannot parse.
uint8_t IECBusHandler::samsHexPairToByte(const char *s, uint8_t len)
{
  if( len!=2 ) return 0xFF;

  uint8_t v = 0;
  for(uint8_t i=0; i<2; i++)
    {
      char c = s[i];
      uint8_t d;
      if( c>='0' && c<='9' )      d = (uint8_t)(c - '0');
      else if( c>='A' && c<='F' ) d = (uint8_t)(c - 'A' + 10);
      else if( c>='a' && c<='f' ) d = (uint8_t)(c - 'a' + 10);
      else return 0xFF;
      v = (uint8_t)((v << 4) | d);
    }

  return v;
}


bool IECBusHandler::runSamsJourneyLoader()
{
  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  // let the preceding IEC transaction finish before touching the lines
  delay(1);

  uint8_t args[4] = {0, 0, 0, 0};
  bool running = true;

  while( running )
    {
      uint8_t command, argLen;

      noInterrupts();
      bool ok = receiveSamsByte(command) && receiveSamsByte(argLen);
      interrupts();
      if( !ok ) break;

      for(uint8_t i=0; i<argLen && ok; i++)
        {
          uint8_t b;
          noInterrupts();
          ok = receiveSamsByte(b);
          interrupts();
          if( i<sizeof(args) ) args[i] = b;
        }
      if( !ok ) break;

      bool aborted = false;
      switch( command )
        {
        case 1:
          running = samsScanDirectory();
          break;

        case 2:
          running = samsReadFile(args[0]);
          break;

        case 3:
          running = samsWriteFile(args[0], aborted) && !aborted;
          break;

        case 0x82:
          running = samsReadFile(samsNameFromTS(args[0], args[1]));
          break;

        case 0x83:
          running = samsWriteFile(samsNameFromTS(args[0], args[1]), aborted) && !aborted;
          break;

        default:
          running = transmitSamsBlock(0xFF, 0, NULL);
          break;
        }
    }

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}


// A track/sector pair is just the name byte split across two values: track
// 1-16 in the high nibble, sector 0-15 in the low one.
uint8_t IECBusHandler::samsNameFromTS(uint8_t track, uint8_t sector)
{
  if( track==0 || track>16 || sector>15 ) return 0xFF;
  return (uint8_t)(((track - 1) << 4) | sector);
}

#endif
