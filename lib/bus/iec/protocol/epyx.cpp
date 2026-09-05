// -----------------------------------------------------------------------------
// Copyright (C) 2023 David Hansel
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
// -----------------------------------------------------------------------------
//
// Epyx FastLoad
//
// These are IECBusHandler member functions living in their own translation
// unit. Everything they need from the bus handler -- the timer macros, the
// inline pin accessors and the state flags -- comes from
// IECBusHandlerInternal.h.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

#ifdef IEC_FP_EPYX

// ------------------------------------  Epyx FastLoad support routines  ------------------------------------


bool RAMFUNC(IECBusHandler::receiveEpyxByte)(uint8_t &data)
{
  bool clk = HIGH;
  for(uint8_t i=0; i<8; i++)
    {
      // wait for next bit ready
      // can't use timeout because interrupts are disabled and (on some platforms) the
      // micros() function does not work in this case
      clk = !clk;
      if( !waitPinCLK(clk, 0) ) return false;

      // read next (inverted) bit
      JDEBUG1();
      data >>= 1;
      if( !readPinDATA() ) data |= 0x80;
      JDEBUG0();
    }

  return true;
}


bool RAMFUNC(IECBusHandler::transmitEpyxByte)(uint8_t data)
{
  // receiver expects all data bits to be inverted
  data = ~data;

  // prepare timer
  timer_init();
  timer_reset();

  // wait (indefinitely) for either DATA high ("ready-to-send") or ATN low
  // NOTE: this must be in a blocking loop since the sender starts transmitting
  // the byte immediately after setting CLK high. If we exit the "task" function then
  // we may not get back here in time to receive.
  while( !digitalReadFastExtIEC(m_pinDATA, m_regDATAread, m_bitDATA) && digitalReadFastExtIEC(m_pinATN, m_regATNread, m_bitATN) )
#ifdef ESP_PLATFORM
    if( !timer_less_than(IWDT_FEED_TIME) )
      {
        // briefly enable interrupts to "feed" the WDT, otherwise we'll get re-booted
        interrupts(); noInterrupts();
        timer_reset();
      }
#else
    {}
#endif
  
  // start timer
  timer_start();
  JDEBUG1();

  // abort if ATN low
  if( !readPinATN() ) { JDEBUG0(); return false; }

  JDEBUG0();
  writePinCLK(data & bit(7));
  writePinDATA(data & bit(5));
  JDEBUG1();
  // bits 5+7 are read by receiver 15 cycles after DATA HIGH

  // wait until 17 us after DATA
  timer_wait_until(17);

  JDEBUG0();
  writePinCLK(data & bit(6));
  writePinDATA(data & bit(4));
  JDEBUG1();
  // bits 4+6 are read by receiver 25 cycles after DATA HIGH

  // wait until 27 us after DATA
  timer_wait_until(27);

  JDEBUG0();
  writePinCLK(data & bit(3));
  writePinDATA(data & bit(1));
  JDEBUG1();
  // bits 1+3 are read by receiver 35 cycles after DATA HIGH

  // wait until 37 us after DATA
  timer_wait_until(37);

  JDEBUG0();
  writePinCLK(data & bit(2));
  writePinDATA(data & bit(0));
  JDEBUG1();
  // bits 0+2 are read by receiver 45 cycles after DATA HIGH

  // wait until 47 us after DATA
  timer_wait_until(47);

  // release DATA and give it time to stabilize, also some
  // buffer time if we got slightly delayed when waiting before
  writePinDATA(HIGH);
  timer_wait_until(52);

  // wait for DATA low, receiver signaling "not ready"
  if( !waitPinDATA(LOW, 0) ) return false;

  JDEBUG0();
  return true;
}


#ifdef IEC_FP_EPYX_SECTOROPS

// NOTE: most calls to waitPinXXX() within this code happen while
// interrupts are disabled and therefore must use the ",0" (no timeout)
// form of the call - timeouts are dealt with using the micros() function
// which does not work properly when interrupts are disabled.

bool RAMFUNC(IECBusHandler::startEpyxSectorCommand)(uint8_t command)
{
  // interrupts are assumed to be disabled when we get here
  // and will be re-enabled before we exit
  // both CLK and DATA must be released (HIGH) before entering
  uint8_t track, sector;

  if( command==0x81 )
    {
      // V1 sector write
      // wait for DATA low (no timeout), however we exit if ATN goes low,
      // interrupts are enabled while waiting (same as in 1541 code)
      interrupts();
      if( !waitPinDATA(LOW, 0) ) return false;
      noInterrupts();

      // release CLK
      writePinCLK(HIGH);
    }

  // receive track and sector
  // (command==1 means write sector, otherwise read sector)
  if( !receiveEpyxByte(track) )   { interrupts(); return false; }
  if( !receiveEpyxByte(sector) )  { interrupts(); return false; }

  // V1 of the cartridge has two different uploads for read and write
  // and therefore does not send the command separately
  if( command==0 && !receiveEpyxByte(command) ) { interrupts(); return false; }

  if( (command&0x7f)==1 )
    {
      // sector write operation => receive data
      for(int i=0; i<256; i++)
        if( !receiveEpyxByte(m_buffer[i]) )
          { interrupts(); return false; }
    }

  // pull CLK low to signal "not ready"
  writePinCLK(LOW);

  // we can allow interrupts again
  interrupts();

  // pass data on to the device
  if( (command&0x7f)==1 )
    if( !m_currentDevice->epyxWriteSector(track, sector, m_buffer) )
      { interrupts(); return false; }

  // m_buffer size is guaranteed to be >=32
  m_buffer[0] = command;
  m_buffer[1] = track;
  m_buffer[2] = sector;

  m_currentDevice->fastLoadRequest(IEC_FP_EPYX, IEC_FL_PROT_SECTOR);
  return true;
}


bool RAMFUNC(IECBusHandler::finishEpyxSectorCommand)()
{
  // this was set in receiveEpyxSectorCommand
  uint8_t command = m_buffer[0];
  uint8_t track   = m_buffer[1];
  uint8_t sector  = m_buffer[2];

  // receive data from the device
  if( (command&0x7f)!=1 )
    if( !m_currentDevice->epyxReadSector(track, sector, m_buffer) )
      return false;

  // all timing is clocked by the computer so we can't afford
  // interrupts to delay execution as long as we are signaling "ready"
  noInterrupts();

  // release CLK to signal "ready"
  writePinCLK(HIGH);

  if( command==0x81 )
    {
      // V1 sector write => receive new track/sector
      return startEpyxSectorCommand(0x81); // startEpyxSectorCommand() re-enables interrupts
    }
  else
    {
      // V1 sector read or V2/V3 read/write => release CLK to signal "ready"
      if( (command&0x7f)!=1 )
        {
          // sector read operation => send data
          for(int i=0; i<256; i++)
            if( !transmitEpyxByte(m_buffer[i]) )
              { interrupts(); return false; }
        }
      else
        {
          // release DATA and wait for computer to pull it LOW
          writePinDATA(HIGH);
          if( !waitPinDATA(LOW, 0) ) { interrupts(); return false; }
        }

      // release DATA and toggle CLK until DATA goes high or ATN goes low.
      // This provides a "heartbeat" for the computer so it knows we're still running
      // the EPYX sector command code. If the computer does not see this heartbeat
      // it will re-upload the code when it needs it.
      // The EPYX code running on a real 1541 drive does not have this timeout but
      // we need it because otherwise we're stuck in an endless loop with interrupts
      // disabled until the computer either pulls ATN low or releases DATA
      // We can not enable interrupts because the time between DATA high
      // and the start of transmission for the next track/sector/command block
      // is <400us without any chance for us to signal "not ready.
      // A (not very nice) interrupt routing may take longer than that.
      // We could just always quit and never send the heartbeat but then operations
      // like "copy disk" would have to re-upload the code for ever single sector.
      // Wait for DATA high, time out after 30000 * ~16us (~500ms)
      timer_init();
      timer_reset();
      timer_start();
      for(unsigned int i=0; i<30000; i++)
        {
          writePinCLK(LOW);
          if( !readPinATN() ) break;
          interrupts();
          timer_wait_until(8);
          noInterrupts();
          writePinCLK(HIGH);
          if( readPinDATA() ) break;
          timer_wait_until(16);
          timer_reset();
        }

      // abort if we timed out (DATA still low) or ATN is pulled
      if( !readPinDATA() || !readPinATN() ) { interrupts(); return false; }

      // wait (DATA high pulse from sender can be up to 90us)
      if( !waitTimeout(100) ) { interrupts(); return false; }

      // if DATA is still high (or ATN is low) then done, otherwise repeat for another sector
      if( readPinDATA() || !readPinATN() )
        { interrupts(); return false; }
      else
        return startEpyxSectorCommand((command&0x80) ? command : 0); // startEpyxSectorCommand() re-enables interrupts
    }
}

#endif

bool RAMFUNC(IECBusHandler::receiveEpyxHeader)()
{
  // all timing is clocked by the computer so we can't afford
  // interrupts to delay execution as long as we are signaling "ready"
  noInterrupts();

  // pull CLK low to signal "ready for header"
  writePinCLK(LOW);

  // wait for sender to set DATA low, signaling "ready"
  if( !waitPinDATA(LOW, 0) ) { interrupts(); return false; }

  // release CLK line
  writePinCLK(HIGH);

  // receive fastload routine upload (256 bytes) and compute checksum
  uint8_t data, checksum = 0;
  for(int i=0; i<256; i++)
    {
      if( !receiveEpyxByte(data) ) { interrupts(); return false; }
      checksum += data;
    }

  if( checksum==0x26 /* V1 load file */ ||
      checksum==0x86 /* V2 load file */ ||
      checksum==0xAA /* V3 load file */ )
    {
      // LOAD FILE operation
      // receive file name and open file
      uint8_t n;
      if( receiveEpyxByte(n) && n>0 && n<=32 )
        {
          // file name arrives in reverse order
          for(uint8_t i=n; i>0; i--)
            if( !receiveEpyxByte(m_buffer[i-1]) )
              { interrupts(); return false; }

          // pull CLK low to signal "not ready"
          writePinCLK(LOW);

          // can allow interrupts again
          interrupts();

          // initiate DOS OPEN command in the device (open channel #0)
          m_currentDevice->listen(0xF0);

          // send file name (in proper order) to the device
          for(uint8_t i=0; i<n; i++)
            {
              // make sure the device can accept data
              int8_t ok;
              while( (ok = m_currentDevice->canWrite())<0 )
                if( !readPinATN() )
                  return false;

              // fail if it can not
              if( ok==0 ) return false;

              // send next file name character
              m_currentDevice->write(m_buffer[i], i<n-1);
            }

          // finish DOS OPEN command in the device
          m_currentDevice->unlisten();

          m_currentDevice->fastLoadRequest(IEC_FP_EPYX, IEC_FL_PROT_LOAD);
          return true;
        }
    }
#ifdef IEC_FP_EPYX_SECTOROPS
  else if( checksum==0x0B /* V1 sector read */ )
    return startEpyxSectorCommand(0x82); // startEpyxSectorCommand re-enables interrupts
  else if( checksum==0xBA /* V1 sector write */ )
    return startEpyxSectorCommand(0x81); // startEpyxSectorCommand re-enables interrupts
  else if( checksum==0xB8 /* V2 and V3 sector read or write */ )
    return startEpyxSectorCommand(0); // startEpyxSectorCommand re-enables interrupts
#endif
#if 0
  else if( Serial )
    {
      interrupts();
      Serial.print(F("Unknown EPYX fastload routine, checksum is 0x"));
      Serial.println(checksum, HEX);
    }
#endif

  interrupts();
  return false;
}


bool RAMFUNC(IECBusHandler::transmitEpyxBlock)()
{
  // set channel number for read() call below
  m_currentDevice->talk(0);

  // get data
  m_inTask = false;
  uint8_t n = m_currentDevice->read(m_buffer, m_bufferSize);
  m_inTask = true;
  if( (m_flags & P_ATN) || !readPinATN() ) return false;

  noInterrupts();

  // release CLK to signal "ready"
  writePinCLK(HIGH);

  // transmit length of this data block
  if( !transmitEpyxByte(n) ) { interrupts(); return false; }

  // transmit the data block
  for(uint8_t i=0; i<n; i++)
    if( !transmitEpyxByte(m_buffer[i]) )
      { interrupts(); return false; }

  // pull CLK low to signal "not ready"
  writePinCLK(LOW);

  interrupts();

  // the "end transmission" condition for the receiver is receiving
  // a "0" length byte so we keep sending block until we have
  // transmitted a 0-length block (i.e. end-of-file)
  return n>0;
}


#endif
