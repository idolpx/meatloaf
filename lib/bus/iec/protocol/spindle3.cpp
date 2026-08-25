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
// Spindle 3.x. Ported from sd2iec's fl-spindle.c (Ingo Korb, GPL v2).
//
// A different loader from Spindle 2.x, sharing only the name and the
// three-byte sector bitmap. Three things make it its own thing:
//
//   - A sector is not sent whole. It is cut into UNITS, each preceded by its
//     length, and they are walked BACKWARDS from the end of the buffer.
//   - A "continuation record" at the end of a sector carries the next command
//     plus a set of units that are POSTPONED to the end of the job. When there
//     are none, a dummy unit is added so the send path stays uniform.
//   - It is asynchronous. The computer can interrupt a transfer to ask for a
//     job by number -- seven bits, clocked on ATN with CLK carrying the data --
//     which the drive looks up in a track/sector table in sector 6. The track
//     stored there is a HALF-track, so it is halved on the way out.
//
// Bytes are shuffled on the way out like 2.x but with a different order, and
// XOR-ed with 0x7F rather than inverted.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"
#include <string.h>

#if defined(IEC_FP_SPINDLE) && defined(IEC_IMPL_SOFTLOAD)

#define SP3_INIT_TRACK    18
#define SP3_INIT_SECTOR   17
#define SP3_FLIP_SECTOR    5
#define SP3_ASYNC_SECTOR   6
#define SP3_MAX_SECTORS   21
#define SP3_SIDE_ID_LEN    3
#define SP3_PP_LEN       (0x60-3)

#define SP3_CMD_NEWJOB    (1<<7)
#define SP3_CMD_NEXTTRACK (1<<6)
#define SP3_CMD_ONDEMAND  (1<<5)

#define SP3_FLAG_FULLSECT (1<<7)
#define SP3_FLAG_CONTREC  (1<<6)

// 3.x shuffles its bits in a different order from 2.x
static const uint8_t s_sp3Encoding[8] = {
  1<<3, 1<<1, 1<<2, 1<<0, 1<<4, 1<<5, 1<<6, 1<<7
};

struct Spindle3Quirk { uint16_t crc; uint8_t blockDelayMs; };
static const Spindle3Quirk s_sp3Quirks[] = {
  { 0xebd1, 40 },   // mojo / 26th job on side 4 (CR at 0x19/0x06)
  { 0, 0 }
};


static void sp3FakeCommand(uint8_t cmd[3], uint8_t sector)
{
  cmd[0] = 0; cmd[1] = 0; cmd[2] = 0;
  cmd[(sector+3) >> 3] |= (uint8_t)(0x80 >> ((sector+3) & 7));
}


static uint8_t sp3NextSector(const uint8_t cmd[3], uint8_t s)
{
  while( s < SP3_MAX_SECTORS )
    {
      if( cmd[(s+3) >> 3] & (0x80 >> ((s+3) & 7)) ) break;
      s++;
    }

  return s;
}


bool IECBusHandler::spindleV3WriteByte(uint8_t b, uint32_t timeoutMs)
{
  for(uint8_t i=0; i<8; i+=2)
    {
      uint8_t o = 0;
      if( b & s_sp3Encoding[i]   ) o |= 0x01;
      if( b & s_sp3Encoding[i+1] ) o |= 0x02;

      if( i & 2 )
        { if( !fastWaitATN(LOW, timeoutMs) ) return false; }
      else
        { if( !fastWaitATN(HIGH, 0) ) return false; }

      writePinCLK (o & 1);
      writePinDATA(o & 2);
    }

  return true;
}


// A SEVEN-bit job number, clocked on ATN with CLK carrying the data, most
// significant bit first. Returns 0x80 on timeout, which no real job number is.
uint8_t IECBusHandler::spindleReceiveJobNo()
{
  uint8_t b = 0;

  for(uint8_t i=7; i!=0; i--)
    {
      writePinDATA(HIGH);

      if( !fastWaitATN(HIGH, 0) ) return 0x80;
      delayMicrosecondsISafe(2);

      b = (uint8_t)((b << 1) | (readPinCLK() ? 0 : 1));
      writePinDATA(LOW);

      if( !fastWaitATN(LOW, 1000) ) return 0x80;
    }

  return b;
}


// Pull the continuation record out of a sector: the next command, and the
// units postponed to the end of the job. Returns where the first
// non-postponed unit's length byte sits.
uint8_t IECBusHandler::spindleCopyCR(Spindle3Session &s)
{
  uint8_t pos = 0xFF-2;
  for(uint8_t i=0; i<3; i++) s.nextCmd[i] = m_buffer[pos+i];
  pos--;

  uint8_t dest = SP3_PP_LEN-1;

  while( true )
    {
      uint8_t i = m_buffer[pos];
      if( i==0 || i>4 || i>=dest ) break;   // end of the postponed units

      i++;                                  // the length byte travels too
      while( i-- ) s.ppUnits[dest--] = m_buffer[pos--];
    }

  if( dest==SP3_PP_LEN-1 )
    {
      // none postponed: a dummy unit keeps the send path uniform
      s.ppUnits[dest] = 3;
      dest -= 4;
    }

  s.ppUnits[dest] = 0;   // end marker
  return pos;
}


// Send either the regular units out of the sector buffer or the postponed ones
// out of ppUnits. Sets s.async when the computer interrupts with a request.
bool IECBusHandler::spindleSendUnits(Spindle3Session &s, uint8_t pos, bool pp)
{
  const uint8_t *data = pp ? s.ppUnits : m_buffer;
  uint8_t unitLen = (pos!=0) ? data[pos] : 0xFF;

  while( unitLen > 0 )
    {
      if( pos>0 && unitLen>=pos ) return false;   // impossible length

      pos = (uint8_t)(pos - unitLen);

      if( pp )
        while( readPinCLK() )
          if( !isResetPinIdle() ) return false;

      writePinDATA(HIGH);
      if( !fastWaitATN(HIGH, 0) ) return false;
      delayMicrosecondsISafe(2);

      if( readPinDATA() ) { s.async = true; return true; }   // async or reset

      // a "chain head" is a postponed unit of length 2; otherwise the computer
      // releasing CLK is what asks for the status
      bool chain = (pp && unitLen==2) || readPinCLK();

      noInterrupts();
      bool ok = clockedWriteByte(unitLen, 1000);

      while( ok && unitLen>0 )
        {
          unitLen--;
          uint8_t b = data[pos + unitLen];
          s.jobCrc = iecCrc16Update(s.jobCrc, b);
          ok = spindleV3WriteByte((uint8_t)(b ^ 0x7F), 2500);
        }

      if( ok ) ok = fastWaitATN(HIGH, 0);

      unitLen = (pos>1) ? data[--pos] : 0;

      writePinCLK(!chain);
      // DATA means "more follows", except on the last unit of a job
      writePinDATA(unitLen==0 && pp && (s.nextCmd[0] & SP3_CMD_NEWJOB));
      interrupts();

      if( !ok ) return false;
      if( !fastWaitATN(LOW, 1000) ) return false;

      writePinDATA(LOW);
      writePinCLK(HIGH);

      if( s.blockDelay>0 ) delay(s.blockDelay);
    }

  return true;
}


bool IECBusHandler::runSpindleV3Loader()
{
  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  Spindle3Session s;
  memset(&s, 0, sizeof(s));
  s.jobCrc = 0xFFFF;
  s.track  = SP3_INIT_TRACK;
  sp3FakeCommand(s.cmd, SP3_INIT_SECTOR);

  writePinDATA(LOW);
  if( !fastWaitATN(LOW, 1000) ) goto done;

  while( true )
    {
      uint8_t sector = 0;
      bool restart = false;

      while( true )
        {
          sector = sp3NextSector(s.cmd, sector);
          if( sector==SP3_MAX_SECTORS ) break;

          if( !m_currentDevice->epyxReadSector(s.track, sector, m_buffer) ) goto done;

          if( s.track==SP3_INIT_TRACK )
            {
              s.jobCrc = 0xFFFF;

              switch( m_buffer[0] & 0x1F )
                {
                case SP3_INIT_SECTOR:
                  if( s.initDone )
                    {
                      // A disk flip: the new side has to carry the expected
                      // id, and if it does not we wait for another swap.
                      if( memcmp(s.nextId, m_buffer+0xF9, SP3_SIDE_ID_LEN)!=0 )
                        {
                          if( !waitForDiskChange() ) goto done;
                          restart = true;
                          break;
                        }
                      m_buffer[0xFF-2] |= SP3_CMD_NEWJOB;
                    }
                  else
                    {
                      s.initDone = true;
                      // seed the next side id with this one, so an async
                      // request for the very first job resolves
                      memcpy(s.nextId, m_buffer+0xF9, SP3_SIDE_ID_LEN);
                    }

                  s.track = 1;
                  restart = true;
                  break;

                case SP3_FLIP_SECTOR:
                  // The retry unit has to go out at least once even when this
                  // is the end of the chain rather than a real flip, or the
                  // computer never returns from its loader call.
                  memcpy(s.nextId, m_buffer+1, SP3_SIDE_ID_LEN);
                  m_buffer[0x00] = m_buffer[0x0E];
                  memcpy(m_buffer+(0xFF-9), m_buffer+4, 10);
                  break;

                case SP3_ASYNC_SECTOR:
                  {
                    if( !fastWaitATN(LOW, 1000) ) goto done;

                    uint8_t job = spindleReceiveJobNo();
                    if( job & 0x80 ) goto done;

                    // the stored track is a HALF-track, hence the shift
                    s.track = (uint8_t)(m_buffer[0x80-job] >> 1);
                    sp3FakeCommand(s.cmd, m_buffer[0x40-job]);
                    s.cmd[0] |= SP3_CMD_ONDEMAND;
                    restart = true;
                    break;
                  }

                default:
                  goto done;   // a sector this loader does not use
                }

              if( restart ) break;
            }
          else if( s.cmd[0] & SP3_CMD_ONDEMAND )
            {
              // first sector of an async job that is not the first job
              m_buffer[0xFF-3]  = 0;                    // ignore its units
              m_buffer[0xFF-2] &= (uint8_t) ~SP3_CMD_NEWJOB;
              s.jobCrc = 0xFFFF;
            }

          uint8_t unitStart;
          switch( m_buffer[0] & (SP3_FLAG_FULLSECT|SP3_FLAG_CONTREC) )
            {
            case SP3_FLAG_FULLSECT: unitStart = 0;                  break;
            case SP3_FLAG_CONTREC:  unitStart = spindleCopyCR(s);   break;
            default:                unitStart = 0xFF;               break;
            }

          s.async = false;
          if( !spindleSendUnits(s, unitStart, false) ) goto done;

          if( readPinDATA() )
            {
              if( readPinCLK() ) goto done;   // reset

              // abandon this transfer and go read the async sector
              writePinDATA(LOW);
              s.track = SP3_INIT_TRACK;
              sp3FakeCommand(s.cmd, SP3_ASYNC_SECTOR);
              restart = true;
              break;
            }

          sector++;
        }

      if( restart ) continue;

      // the postponed units close the job; copyCR() guarantees at least one
      if( !spindleSendUnits(s, SP3_PP_LEN-1, true) ) goto done;

      switch( s.nextCmd[0] & (SP3_CMD_NEXTTRACK|SP3_CMD_ONDEMAND) )
        {
        case 0: break;
        case SP3_CMD_NEXTTRACK:
          while( ++s.track == SP3_INIT_TRACK ) ;
          break;
        case SP3_CMD_ONDEMAND:
          s.track = SP3_INIT_TRACK;
          break;
        default:
          goto done;   // a combination this loader does not use
        }

      if( s.nextCmd[0] & SP3_CMD_NEWJOB )
        {
          s.blockDelay = 0;
          for(const Spindle3Quirk *q = s_sp3Quirks; q->crc!=0; q++)
            if( q->crc==s.jobCrc ) { s.blockDelay = q->blockDelayMs; break; }
          s.jobCrc = 0xFFFF;
        }

      memcpy(s.cmd, s.nextCmd, sizeof(s.cmd));
    }

 done:
  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}

#endif
