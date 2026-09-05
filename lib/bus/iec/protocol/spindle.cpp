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
// Spindle 2.x. Ported from sd2iec's fl-spindle.c (Ingo Korb, GPL v2).
//
// Spindle does not ask for files or for blocks. It sends a three-byte COMMAND
// that is a bitmap of which sectors of the current track it wants, and the
// drive sends each of them in turn; the NEXT command arrives inside the data,
// carried in the checksum byte and the first two bytes of a block. The track
// only ever advances by one, and never onto the directory track.
//
// Which 2.x it is cannot be told from the M-E: 2.1, 2.2 and 2.3 upload the
// same code. The version comes from hashing the init sector at 18/17, minus
// the three fields that differ per release (side id, next side id, initial
// command) -- which is also where the initial command and the id of the next
// disk side come from. 2.3 stores that next id backwards.
//
// The bits of a byte are shuffled on the way out (order 5 7 4 6 0 2 1 3) and
// the byte is inverted; a block is 256 bytes plus an XOR checksum. 2.1 uses
// CLK where 2.2 and later use DATA and vice versa, which is why the ready
// signalling below is written in terms of "this version's" line rather than a
// fixed one.
//
// Spindle 3.x is NOT implemented. It adds asynchronous jobs -- the computer
// sends a 7-bit job number clocked on ATN and the drive services requests out
// of order from a table in sector 6 -- which is a different loader wearing the
// same name. It is detected and declined.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"
#include <string.h>

#if defined(IEC_FP_SPINDLE) && defined(IEC_IMPL_SOFTLOAD)

#define SP_INIT_TRACK   18
#define SP_INIT_SECTOR  17
#define SP_MAX_SECTORS  21
#define SP_SIDE_ID_LEN   3

#define SP_CMD2_COMMAND  (1<<7)
#define SP_CMD2_EOF1     (1<<6)
#define SP_CMD2_NEXTTRACK (1<<5)
#define SP_CMD2_RESET    (1<<5)

// 2.x sends its bits shuffled: this is the order they leave in.
static const uint8_t s_spEncoding[8] = {
  1<<3, 1<<1, 1<<2, 1<<0, 1<<6, 1<<4, 1<<7, 1<<5
};


// Build a command asking for exactly one sector.
static void spFakeCommand(uint8_t cmd[3], uint8_t sector)
{
  cmd[0] = 0; cmd[1] = 0; cmd[2] = 0;
  cmd[(sector+3) >> 3] |= (uint8_t)(0x80 >> ((sector+3) & 7));
}


// Next sector the command asks for, at or after "s". SP_MAX_SECTORS if none.
static uint8_t spNextSector(const uint8_t cmd[3], uint8_t s)
{
  while( s < SP_MAX_SECTORS )
    {
      if( cmd[(s+3) >> 3] & (0x80 >> ((s+3) & 7)) ) break;
      s++;
    }

  return s;
}


// The shuffled, inverted variant of the clocked byte writer.
bool IECBusHandler::spindleWriteByte(uint8_t b, uint32_t timeoutMs)
{
  uint8_t v = (uint8_t) ~b;

  for(uint8_t i=0; i<8; i+=2)
    {
      uint8_t o = 0;
      if( v & s_spEncoding[i]   ) o |= 0x01;
      if( v & s_spEncoding[i+1] ) o |= 0x02;

      if( i & 2 )
        { if( !fastWaitATN(LOW, timeoutMs) ) return false; }
      else
        { if( !fastWaitATN(HIGH, 0) ) return false; }

      writePinCLK (o & 1);
      writePinDATA(o & 2);
    }

  return true;
}


// Hash the init sector to tell 2.1, 2.2 and 2.3 apart. The three fields that
// differ per release live at 0xF7 and above and are excluded.
uint8_t IECBusHandler::spindleDetectVersion(const uint8_t *initSector)
{
  uint16_t crc = 0xFFFF;
  for(uint8_t i=0; i<0xF7; i++)
    crc = iecCrc16Update(crc, initSector[i]);

  switch( crc )
    {
    case 0x889e: return IEC_FLV_SPINDLE_21;
    case 0xd126: return IEC_FLV_SPINDLE_22;
    case 0x7ee2: return IEC_FLV_SPINDLE_23;
    default:     return IEC_FLV_NONE;
    }
}


// Send the block in m_buffer plus its checksum, retrying until the computer
// acknowledges. The checksum doubles as the first byte of the next command
// when its top bit is set.
bool IECBusHandler::spindleSendBlock(uint8_t variant, uint8_t *nextCmd)
{
  uint8_t cs = 0;

  while( true )
    {
      // 2.1 signals ready on CLK, later versions on DATA
      writePinCLK (variant==IEC_FLV_SPINDLE_21);
      writePinDATA(variant!=IEC_FLV_SPINDLE_21);

      // can stall for a long time, so no timeout
      if( !fastWaitATN(LOW, 0) ) return false;

      noInterrupts();
      cs = 0;
      bool ok = true;
      for(uint16_t i=0; i<=0x100 && ok; i++)
        {
          uint8_t b;
          if( i<0x100 ) { b = m_buffer[i]; cs ^= b; }
          else          { b = cs; }

          ok = spindleWriteByte(b, 1000);
        }

      // the last bit pair is still unacknowledged when the loop ends
      if( ok ) ok = fastWaitATN(HIGH, 0);
      interrupts();
      if( !ok ) return false;

      writePinCLK(HIGH);
      writePinDATA(HIGH);

      // wait up to 10ms for the computer to acknowledge; if it does not, the
      // whole block goes again
      uint32_t start = micros();
      bool acked = false;
      while( (micros()-start) < 10000 )
        {
          if( readPinCLK() || readPinDATA() ) { acked = true; break; }
          if( !isResetPinIdle() ) return false;
        }

      if( acked ) break;
    }

  if( cs & 0x80 )
    {
      nextCmd[0] = cs;
      nextCmd[1] = m_buffer[0];
      nextCmd[2] = (uint8_t)(m_buffer[0] ^ m_buffer[1]);
    }

  return true;
}


bool IECBusHandler::runSpindleLoader(const uint8_t *cmd, uint8_t cmdLen)
{
  if( cmdLen!=0x17 && cmdLen!=0x29 ) return false;

  uint16_t crc = 0xFFFF;
  for(uint8_t i=5; i<cmdLen-2; i++) crc = iecCrc16Update(crc, cmd[i]);

  uint8_t variant;
  switch( crc )
    {
    case 0x6027:
      // 3.x is a different loader wearing the same name; see spindle3.cpp
      return runSpindleV3Loader();
    case 0xe438: variant = IEC_FLV_NONE;        break;  // 2.x, version found later
    case 0x2c76: variant = IEC_FLV_SPINDLE_23;  break;  // 2.3 with custom drivecode
    default:     return false;
    }

  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  uint8_t curCmd[3], nextCmd[3], nextId[SP_SIDE_ID_LEN];
  memset(nextCmd, 0, sizeof(nextCmd));
  memset(nextId, 0, sizeof(nextId));

  uint8_t track = SP_INIT_TRACK;
  bool    initDone = false;
  spFakeCommand(curCmd, SP_INIT_SECTOR);

  while( true )
    {
      uint8_t sector = 0;
      bool leave = false;

      while( true )
        {
          sector = spNextSector(curCmd, sector);
          if( sector==SP_MAX_SECTORS ) break;

          if( !m_currentDevice->epyxReadSector(track, sector, m_buffer) ) { leave = true; break; }

          // Either line low means the computer is in EOF1: answer by pulling
          // both, wait for its ATN pulse, then release. 2.1 puts EOF1 on CLK
          // and EOF2 on DATA, later versions the other way round, so both are
          // simply tested and both driven.
          if( !readPinCLK() || !readPinDATA() )
            {
              writePinCLK(LOW);
              writePinDATA(LOW);

              if( !fastWaitATN(LOW, 0) || !fastWaitATN(HIGH, 0) ) { leave = true; break; }

              writePinCLK(HIGH);
              writePinDATA(HIGH);

              // 2.1 always releases both lines after acknowledging EOF2, so
              // this reset check cannot work there -- a reset during EOF1 on
              // 2.1 hangs until the computer is reset a second time, exactly
              // as it does in sd2iec.
              if( variant!=IEC_FLV_NONE && variant!=IEC_FLV_SPINDLE_21 )
                {
                  delayMicrosecondsISafe(2);
                  if( readPinDATA() ) { leave = true; break; }
                }
            }

          if( track==SP_INIT_TRACK )
            {
              if( sector!=SP_INIT_SECTOR ) { leave = true; break; }

              if( initDone )
                {
                  // a disk flip: the new side has to carry the id we expect,
                  // and if it does not we wait for the user to swap again
                  if( memcmp(nextId, m_buffer+0xF7, SP_SIDE_ID_LEN)!=0 )
                    {
                      if( !waitForDiskChange() ) { leave = true; break; }
                      continue;
                    }

                  m_buffer[0xFD] |= SP_CMD2_EOF1;
                }
              else
                {
                  if( variant==IEC_FLV_NONE )
                    {
                      variant = spindleDetectVersion(m_buffer);
                      if( variant==IEC_FLV_NONE ) { leave = true; break; }
                    }

                  // every 2.x but 2.1 starts with EOF1 on the first disk
                  if( variant!=IEC_FLV_SPINDLE_21 ) m_buffer[0xFD] |= SP_CMD2_EOF1;
                  initDone = true;
                }

              for(uint8_t i=0; i<3; i++)
                {
                  nextCmd[i] = m_buffer[0xFD+i];
                  // 2.3 stores the next side id backwards
                  nextId[i] = (variant < IEC_FLV_SPINDLE_23) ? m_buffer[0xFA+i]
                                                             : m_buffer[0xFC-i];
                }

              track = 1;   // the first real command is on track 1
              break;       // nothing is sent for the init sector
            }

          if( !spindleSendBlock(variant, nextCmd) ) { leave = true; break; }

          sector++;
        }

      if( leave ) break;

      // No sector bits set at all means a special command rather than a load.
      if( !((nextCmd[0] & 0x1F) || nextCmd[1] || nextCmd[2]) )
        {
          if( nextCmd[0] & SP_CMD2_RESET )
            {
              // 2.3's custom-drivecode variant reuses this bit to mean "load
              // drive code", which is not supported either way, so both end
              // the session.
              writePinCLK(LOW);
              writePinDATA(LOW);
              fastWaitATN(LOW, 1000);
              break;
            }

          // a flip: ask for the init sector again and force EOF1
          track = SP_INIT_TRACK;
          spFakeCommand(nextCmd, SP_INIT_SECTOR);
          nextCmd[0] |= SP_CMD2_EOF1;
        }

      if( nextCmd[0] & SP_CMD2_NEXTTRACK )
        while( ++track == SP_INIT_TRACK ) ;   // advance, skipping the directory

      if( nextCmd[0] & SP_CMD2_EOF1 )
        {
          if( variant!=IEC_FLV_SPINDLE_21 )
            {
              writePinDATA(LOW);
              if( !fastWaitATN(LOW, 2000) ) break;
            }
          else
            writePinCLK(LOW);
        }

      memcpy(curCmd, nextCmd, sizeof(curCmd));
    }

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}

#endif
