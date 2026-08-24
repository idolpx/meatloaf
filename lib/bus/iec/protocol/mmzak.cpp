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
// Maniac Mansion / Zak McKracken. Ported from sd2iec's fl-mmzak.c
// (Ingo Korb, GPL v2).
//
// A block server like Nippon, but the command is three bytes -- track, sector,
// then 0x30 to read, 0x40 to write, 0x20 to leave the loader. The computer
// clocks every bit, two per CLK cycle, most significant first, so there is no
// absolute timing in the transfer itself.
//
// 0x01 is the marker byte: a data byte that happens to be 0x01 is sent twice,
// 0x01 0x81 ends a good block and 0x01 0x11 reports an error.
//
// The one piece of real timing is the opening handshake -- eight 1285/1290 us
// pulses on DATA. sd2iec's comment says it is not sure the handshake is needed
// at all, since a real 1541 answers this with the ATN acknowledge hardware
// rather than in software. It is reproduced here rather than guessed at.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

#if defined(IEC_FP_MMZAK) && defined(IEC_IMPL_SOFTLOAD)

bool RAMFUNC(IECBusHandler::mmzakWaitCLK)(bool state)
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


bool RAMFUNC(IECBusHandler::transmitMMZakByte)(uint8_t value)
{
  timer_init();
  timer_reset();
  timer_start();

  for(uint8_t i=0; i<4; i++)
    {
      if( !mmzakWaitCLK(HIGH) ) return false;
      writePinDATA(value & 0x80);
      value <<= 1;

      if( !mmzakWaitCLK(LOW) ) return false;
      writePinDATA(value & 0x80);
      value <<= 1;
    }

  return true;
}


bool RAMFUNC(IECBusHandler::receiveMMZakByte)(uint8_t &data)
{
  uint8_t value = 0;

  timer_init();
  timer_reset();
  timer_start();

  for(uint8_t i=0; i<4; i++)
    {
      if( !mmzakWaitCLK(LOW) ) return false;
      value <<= 1;
      delayMicrosecondsISafe(3);
      if( !readPinDATA() ) value |= 1;

      if( !mmzakWaitCLK(HIGH) ) return false;
      value <<= 1;
      delayMicrosecondsISafe(3);
      if( !readPinDATA() ) value |= 1;
    }

  data = value;
  return true;
}


bool RAMFUNC(IECBusHandler::transmitMMZakError)()
{
  writePinCLK(HIGH);
  writePinDATA(HIGH);
  return transmitMMZakByte(0x01) && transmitMMZakByte(0x11);
}


bool IECBusHandler::mmzakReadSector(uint8_t track, uint8_t sector)
{
  if( !m_currentDevice->epyxReadSector(track, sector, m_buffer) )
    {
      noInterrupts();
      bool ok = transmitMMZakError();
      interrupts();
      return ok;
    }

  noInterrupts();

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  delayMicrosecondsISafe(3);

  bool ok = true;
  for(uint16_t i=0; i<256 && ok; i++)
    {
      // 0x01 introduces the status markers, so a data byte of 0x01 is doubled
      if( m_buffer[i]==0x01 ) ok = transmitMMZakByte(0x01);
      if( ok ) ok = transmitMMZakByte(m_buffer[i]);
    }

  if( ok ) ok = transmitMMZakByte(0x01) && transmitMMZakByte(0x81);

  writePinCLK(LOW);
  writePinDATA(HIGH);

  interrupts();
  return ok;
}


bool IECBusHandler::mmzakWriteSector(uint8_t track, uint8_t sector)
{
  noInterrupts();

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  delayMicrosecondsISafe(3);

  bool ok = true;
  for(uint16_t i=0; i<256 && ok; i++)
    ok = receiveMMZakByte(m_buffer[i]);

  writePinCLK(LOW);
  interrupts();

  if( !ok ) return false;

  if( !m_currentDevice->epyxWriteSector(track, sector, m_buffer) )
    {
      noInterrupts();
      ok = transmitMMZakError();
      interrupts();
    }

  return ok;
}


bool IECBusHandler::runMMZakLoader()
{
  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  // Opening handshake: eight long pulses on DATA. A real 1541 answers this
  // with its ATN acknowledge hardware, so sd2iec is not certain the software
  // version is needed -- it is kept because dropping it is a guess either way.
  for(uint8_t i=0; i<8; i++)
    {
      writePinCLK(HIGH);
      writePinDATA(LOW);
      delayMicrosecondsISafe(1285);
      writePinDATA(HIGH);
      delayMicrosecondsISafe(1290);
    }

  // wait for the computer to release both lines
  while( !readPinCLK() || !readPinDATA() )
    if( !isResetPinIdle() ) { setATNInterruptEnabled(atnInterruptWasEnabled); return true; }

  bool done = false;
  while( !done )
    {
      // signal readiness and wait for the computer to pull DATA low
      writePinCLK(LOW);
      writePinDATA(HIGH);
      delayMicrosecondsISafe(3);

      while( readPinDATA() )
        if( !isResetPinIdle() ) { done = true; break; }
      if( done ) break;

      writePinCLK(HIGH);

      uint8_t track, sector, command;
      noInterrupts();
      bool ok = receiveMMZakByte(track) && receiveMMZakByte(sector)
             && receiveMMZakByte(command);
      interrupts();
      if( !ok ) break;

      writePinCLK(LOW);

      switch( command )
        {
        case 0x20:   // leave the loader
          done = true;
          break;

        case 0x30:
          if( !mmzakReadSector(track, sector) ) done = true;
          break;

        case 0x40:
          if( !mmzakWriteSector(track, sector) ) done = true;
          break;

        default:
          noInterrupts();
          if( !transmitMMZakError() ) done = true;
          interrupts();
          break;
        }
    }

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  if( !readPinATN() ) atnRequest();

  return true;
}

#endif
