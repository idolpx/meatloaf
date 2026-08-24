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
// GI Joe. Ported from sd2iec's fl-gijoe.c (Ingo Korb, GPL v2).
//
// This one is a session owner: after the M-E it keeps the bus and serves file
// after file until the computer stops asking. It is also the cheapest loader
// in the set to port, because the computer clocks every bit -- there is no
// absolute timing anywhere in it, only handshakes, so nothing here depends on
// getting a microsecond count right.
//
// A file is named by two characters, which become the pattern "xx*". Data goes
// out as 254-byte blocks with 0xAC escaped by doubling it; 0xAC 0xC3 says
// another block follows, 0xAC 0xFF ends the file, and 0xFE 0xFE 0xAC 0xF7 is
// the error reply.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

#if defined(IEC_FP_GIJOE) && defined(IEC_IMPL_SOFTLOAD)

// sd2iec leaves this loop when a button on the device is pressed. There is no
// equivalent here, so the escape is the computer asserting ATN (it wants the
// bus back) or the RESET line dropping. Both also cover the case of the
// computer simply being switched off part way through a transfer, which would
// otherwise leave the IEC task spinning in a wait that never ends.
bool RAMFUNC(IECBusHandler::gijoeAbort)()
{
  return !readPinATN() || !isResetPinIdle();
}


// Wait for CLK to reach "state", returning false if we should give up. The
// watchdog has to be fed here: with interrupts disabled this loop is the one
// place a stopped computer can hold us indefinitely.
bool RAMFUNC(IECBusHandler::gijoeWaitCLK)(bool state)
{
  while( readPinCLK()!=state )
    {
      if( gijoeAbort() ) return false;
#ifdef ESP_PLATFORM
      if( !timer_less_than(IWDT_FEED_TIME) )
        {
          interrupts(); noInterrupts();
          timer_reset();
        }
#endif
    }

  return true;
}


// Four CLK cycles carry eight bits, two per cycle, least significant first.
bool RAMFUNC(IECBusHandler::receiveGIJoeByte)(uint8_t &data)
{
  uint8_t value = 0;

  timer_init();
  timer_reset();
  timer_start();

  for(uint8_t i=0; i<4; i++)
    {
      if( !gijoeWaitCLK(LOW) ) return false;
      value >>= 1;
      delayMicrosecondsISafe(3);
      if( !readPinDATA() ) value |= 0x80;

      if( !gijoeWaitCLK(HIGH) ) return false;
      value >>= 1;
      delayMicrosecondsISafe(3);
      if( !readPinDATA() ) value |= 0x80;
    }

  data = value;
  return true;
}


bool RAMFUNC(IECBusHandler::transmitGIJoeByte)(uint8_t value)
{
  timer_init();
  timer_reset();
  timer_start();

  for(uint8_t i=0; i<4; i++)
    {
      if( !gijoeWaitCLK(HIGH) ) return false;
      writePinDATA(value & 1);
      value >>= 1;

      if( !gijoeWaitCLK(LOW) ) return false;
      writePinDATA(value & 1);
      value >>= 1;
    }

  return true;
}


// 0xAC introduces the end-of-block and error markers, so a data byte that
// happens to be 0xAC is sent twice.
bool RAMFUNC(IECBusHandler::transmitGIJoeData)(uint8_t value)
{
  if( value==0xAC && !transmitGIJoeByte(0xAC) ) return false;
  return transmitGIJoeByte(value);
}


bool RAMFUNC(IECBusHandler::transmitGIJoeError)()
{
  return transmitGIJoeByte(0xFE) && transmitGIJoeByte(0xFE)
      && transmitGIJoeByte(0xAC) && transmitGIJoeByte(0xF7);
}


bool IECBusHandler::runGIJoeLoader()
{
  // Let the bus settle BEFORE taking it over. delay() yields to the scheduler,
  // and ten milliseconds of not servicing the bus is exactly the window in
  // which the computer starts its first handshake -- so it has to happen while
  // the ATN interrupt is still live, not after.
  delay(10);

  // The loader owns the bus from here, so the ATN interrupt has to stand down
  // -- an atnRequest() in the middle of a bit would corrupt the transfer.
  bool atnInterruptWasEnabled = isATNInterruptEnabled();
  setATNInterruptEnabled(false);

  writePinDATA(HIGH);
  writePinCLK(HIGH);

  while( !readPinDATA() || !readPinCLK() )
    if( gijoeAbort() ) { setATNInterruptEnabled(atnInterruptWasEnabled); return true; }

  bool running = true;
  while( running )
    {
      // handshake: pull CLK low, wait for the computer to pull DATA low
      writePinCLK(LOW);
      while( readPinDATA() )
        if( gijoeAbort() ) { running = false; break; }
      if( !running ) break;

      writePinCLK(HIGH);

      noInterrupts();

      // the first byte carries no information, then two file name characters
      uint8_t ignored, c0, c1;
      if( !receiveGIJoeByte(ignored) || !receiveGIJoeByte(c0) || !receiveGIJoeByte(c1) )
        { interrupts(); break; }

      writePinCLK(LOW);
      interrupts();

      // open "<c0><c1>*" on channel 0
      const char name[3] = { (char) c0, (char) c1, '*' };
      m_currentDevice->listen(0xF0);
      bool ok = true;
      for(uint8_t i=0; i<3 && ok; i++)
        {
          int8_t w;
          while( (w = m_currentDevice->canWrite())<0 )
            if( gijoeAbort() ) { ok = false; break; }

          if( ok && w>0 )
            m_currentDevice->write(name[i], i==2);
          else
            ok = false;
        }
      m_currentDevice->unlisten();

      if( !ok )
        {
          writePinCLK(HIGH);
          noInterrupts();
          transmitGIJoeError();
          interrupts();
          continue;
        }

      // serve the file one block at a time
      while( true )
        {
          m_currentDevice->talk(0);
          m_inTask = false;
          uint8_t n = m_currentDevice->read(m_buffer, 254);
          m_inTask = true;

          bool last = (n < 254);

          writePinCLK(HIGH);
          delayMicrosecondsISafe(2);

          noInterrupts();

          bool sent = true;
          for(uint8_t i=0; i<n && sent; i++)
            sent = transmitGIJoeData(m_buffer[i]);

          if( !sent )
            { interrupts(); running = false; break; }

          if( last )
            {
              sent = transmitGIJoeByte(0xAC) && transmitGIJoeByte(0xFF);
              interrupts();

              // close the file (the computer sends no CLOSE of its own)
              m_currentDevice->listen(0xE0);
              m_currentDevice->unlisten();

              if( !sent ) running = false;
              break;
            }

          sent = transmitGIJoeByte(0xAC) && transmitGIJoeByte(0xC3);
          interrupts();
          if( !sent ) { running = false; break; }

          delayMicrosecondsISafe(50);
          writePinCLK(LOW);
        }
    }

  writePinCLK(HIGH);
  writePinDATA(HIGH);
  setATNInterruptEnabled(atnInterruptWasEnabled);

  // check whether ATN was asserted while we held the bus
  if( !readPinATN() ) atnRequest();

  return true;
}

#endif
