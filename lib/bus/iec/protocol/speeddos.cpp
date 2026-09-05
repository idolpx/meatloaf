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
// SpeedDOS
//
// These are IECBusHandler member functions living in their own translation
// unit. Everything they need from the bus handler -- the timer macros, the
// inline pin accessors and the state flags -- comes from
// IECBusHandlerInternal.h.
// -----------------------------------------------------------------------------

//
// https://ist.uwaterloo.ca/~schepers/MJK/parallel_cable.html
// http://sta.c64.org/cbmpar41c.html
// http://sta.c64.org/cbmpar71c.html
//

#include "../IECBusHandlerInternal.h"

#ifdef IEC_FP_SPEEDDOS

// ------------------------------------  SpeedDos support routines  ------------------------------------  


bool IECBusHandler::receiveSpeedDosByte(bool canWriteOk)
{
  // Note: SpeedDos starts a 350us timeout after setting CLK high
  // (ready-to-send) waiting for the parallel handshake signal. If we take
  // longer than those 350us the receiver will abort.
  // To be safe we need interrupts between setting DATA high 
  // and sending the handshake.

  // wait for CLK high
  JDEBUG0();
  if( !waitPinCLK(HIGH, 0) ) return false;
  
  noInterrupts();

  // release DATA ("ready-for-data")
  JDEBUG1();
  writePinDATA(HIGH);
  
  // wait until data is ready
  if( !waitParallelBusHandshakeReceivedISafe() ) { JDEBUG0(); interrupts(); return false; }
  JDEBUG0();
  
  if( canWriteOk )
    {
      // get the parallel data
      uint8_t data = readParallelData();
      
      // if CLK=1 at this point then sender is signaling EOI
      bool eoi = readPinCLK();
      
      // confirm receipt
      parallelBusHandshakeTransmit();
      writePinDATA(LOW);

      interrupts();

      // pass received data on to the device
      m_currentDevice->write(data, eoi);

      // must return false if this was the last byte so we don't attempt to receive another byte 
      // after this, otherwise we will misinterpret a PC2 pulse as another transmitted byte
      return !eoi;
    }
  else
    {
      // canWrite reported an error
      interrupts();
      return false;
    }
}


bool IECBusHandler::transmitSpeedDosByte(uint8_t numData)
{
  // Note: SpeedDos starts a 350us timeout after setting DATA high
  // (ready-to-receive) waiting for the parallel handshake signal. If we take
  // longer than those 350us the receiver will abort.
  // To be safe we disable interrupts between setting DATA high 
  // and sending the handshake.
  uint8_t data = numData>0 ? m_currentDevice->peek() : 0xFF;

  startParallelTransaction();

  // prepare data (bus is still in INPUT mode so the data will not be visible yet)
  // (doing it now saves time to meet the 350us timeout after DATA high)
  writeParallelData(data);

  noInterrupts();

  // signal "ready-to-send" (CLK=1)
  writePinCLK(HIGH);

  // wait for "ready-for-data" (DATA=1)
  JDEBUG1();
  if( !waitPinDATA(HIGH, 0) ) { atnRequest(); interrupts(); endParallelTransaction(); return false; }
  JDEBUG0();

  if( numData==0 ) 
    {
      // if we have nothing to send then there was some kind of error 
      // aborting here will signal the error condition to the receiver
      interrupts();
      endParallelTransaction();
      return false;
    }

  // set CLK state to signal EOI
  writePinCLK(numData==1);

  // put data on parallel bus and send handshake ("data ready")
  setParallelBusModeOutput();
  parallelBusHandshakeTransmit();

  // wait until receiver has read the parallel data 
  // (receiver also sets DATA=0 before reading the parallel data)
  JDEBUG1();
#ifdef ESP_PLATFORM
  // waitParallelBusHandshakeReceivedISafe() does not work reliably on ESP32
  // since ESP32 seems to randomly pause for ~5us (even with interrupts disabled),
  // missing the handshake pulse
  // => wait for DATA=0 plus 65us (15us between DATA=0 and reading the parallel data 
  // plus 50us for a possible VIC-II "badline" delay).
  if( !waitPinDATA(LOW) ) { atnRequest(); interrupts(); endParallelTransaction(); return false; }
  delayMicrosecondsISafe(65);
#else
  // wait for handshake pulse signaling the data was read
  // (receiver sets DATA=0 before reading the parallel data)
  waitParallelBusHandshakeReceivedISafe();
#endif
  JDEBUG0();

  // signal "NOT ready-to-send"
  writePinCLK(LOW);

  // release parallel bus
  setParallelBusModeInput();

  interrupts();
  
  // discard data byte in device (read by peek() before)
  m_currentDevice->read();

  // remember initial bytes of data sent (see comment in transmitSpeedDosFile)
  if( m_secondary==0x60 && m_bufferCtr<PARALLEL_PREBUFFER_BYTES )
    m_buffer[m_bufferCtr++] = data;

  endParallelTransaction();

  return true;
}


bool IECBusHandler::transmitSpeedDosParallelByte(uint8_t data)
{
  // NOTE: this function should NOT be called when interrupts are disabled!

  // put data on bus
  JDEBUG1(); 
  writeParallelData(data);
  
  // send handshake
  noInterrupts();
  parallelBusHandshakeTransmit();
  parallelBusHandshakeReceived();
  interrupts();
  
  // wait for received handshake
  bool res = waitParallelBusHandshakeReceived();
  JDEBUG0();
  return res;
}


bool IECBusHandler::transmitSpeedDosFile()
{
  // switch parallel bus to output
  setParallelBusModeOutput();

  // when loading a file, SpeedDos uploads fast-load code to the drive after
  // the transmission has already started (load address has been transmitted). 
  // The fast-load then re-transmits the bytes that were already sent.
  uint8_t offset = m_bufferCtr;

  // get remaining data from the device and transmit it
  uint8_t n;
  while( (n=m_currentDevice->read(m_buffer+offset, m_bufferSize-offset)+offset)>0 )
    {
      startParallelTransaction();
      if( !transmitSpeedDosParallelByte(n+1) )
        { setParallelBusModeInput(); return false; }

      for(uint8_t i=0; i<n; i++) 
        if( !transmitSpeedDosParallelByte(m_buffer[i]) )
          { setParallelBusModeInput(); return false; }

      endParallelTransaction();
      offset = 0;
    }

  // block length of 0 signifies end-of-data
  transmitSpeedDosParallelByte(0);

  // confirm successful transmission (0=LOAD ERROR)
  transmitSpeedDosParallelByte(1);

  // switch parallel bus back to input
  setParallelBusModeInput();

  return true;
}

#endif // IEC_FP_SPEEDDOS

