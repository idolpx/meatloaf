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
// DolphinDOS
//
// These are IECBusHandler member functions living in their own translation
// unit. Everything they need from the bus handler -- the timer macros, the
// inline pin accessors and the state flags -- comes from
// IECBusHandlerInternal.h.
// -----------------------------------------------------------------------------

//
// https://github.com/MEGA65/open-roms/blob/master/doc/Protocol-DolphinDOS.md
// https://mega65.github.io/open-roms/doc/Protocol-DolphinDOS.html
// https://github.com/FeralChild64/open-roms/blob/master/src/kernal/iec_fast/dolphindos_detect.s
// http://sta.c64.org/cbmpar41c.html
// http://sta.c64.org/cbmpar71c.html
//

#include "../IECBusHandlerInternal.h"

#ifdef IEC_FP_DOLPHIN

// ------------------------------------  DolphinDos support routines  ------------------------------------  


void IECBusHandler::enableDolphinBurstMode(IECDevice *dev, bool enable)
{
  if( enable )
    dev->m_flFlags |= S_DOLPHIN_BURST_ENABLED;
  else
    dev->m_flFlags &= ~S_DOLPHIN_BURST_ENABLED;

  dev->m_flProtocol = IEC_FL_PROT_NONE;
}

bool IECBusHandler::receiveDolphinByte(bool canWriteOk)
{
  // NOTE: we only get here if sender has already signaled ready-to-send
  // by releasing CLK
  bool eoi = false;

  // we have buffered bytes (see comment below) that need to be
  // sent on to the higher level handler before we can receive more.
  // There are two ways to get to m_bufferCtr==PARALLEL_PREBUFFER_BYTES:
  // 1) the host never sends a XZ burst request and just keeps sending data
  // 2) the host sends a burst request but we reject it
  // note that we must wait for the host to be ready to send the next data 
  // byte before we can empty our buffer, otherwise we will already empty
  // it before the host sends the burst (XZ) request
  if( m_secondary==0x61 && m_bufferCtr > 0 && m_bufferCtr <= PARALLEL_PREBUFFER_BYTES )
    {
      // send next buffered byte on to higher level
      m_currentDevice->write(m_buffer[m_bufferCtr-1], false);
      m_bufferCtr--;
      return true;
    }

  noInterrupts();

  // signal "ready"
  writePinDATA(HIGH);

  // wait for CLK low
  if( !waitPinCLK(LOW, 100) )
    {
      // exit if waitPinCLK returned because of falling edge on ATN
      if( !readPinATN() ) { interrupts(); return false; }

      // sender did not set CLK low within 100us after we set DATA high
      // => it is signaling EOI
      // acknowledge we received it by setting DATA low for 60us
      eoi = true;
      writePinDATA(LOW);
      if( !waitTimeout(60) ) { interrupts(); return false; }
      writePinDATA(HIGH);

      // keep waiting for CLK low
      if( !waitPinCLK(LOW) ) { interrupts(); return false; }
    }

  // get data
  if( canWriteOk )
    {
      // read data from parallel bus
      uint8_t data = readParallelData();

      // confirm receipt
      writePinDATA(LOW);

      interrupts();

      // when executing a SAVE command, DolphinDos first sends two bytes of data,
      // and then the "XZ" burst request. If the transmission happens in burst mode then
      // that data is going to be sent again and the initial data is discarded.
      // (MultiDubTwo actually sends garbage bytes for the initial two bytes)
      // so we can't pass the first two bytes on yet because we don't yet know if this is
      // going to be a burst transmission. If it is NOT a burst then we need to send them
      // later (see beginning of this function). If it is a burst then we discard them.
      // Note that the SAVE command always operates on channel 1 (secondary address 0x61)
      // so we only do the buffering in that case. 
      if( m_secondary==0x61 && m_bufferCtr > PARALLEL_PREBUFFER_BYTES )
        {
          m_buffer[m_bufferCtr-PARALLEL_PREBUFFER_BYTES-1] = data;
          m_bufferCtr--;
        }
      else
        {
          // pass received data on to the device
          m_currentDevice->write(data, eoi);
        }

      return true;
    }
  else
    {
      // canWrite reported an error
      interrupts();
      return false;
    }
}


bool RAMFUNC(IECBusHandler::transmitDolphinByte)(uint8_t numData)
{
  // Note: receiver starts a 50us timeout after setting DATA high
  // (ready-to-receive) waiting for CLK low (data valid). If we take
  // longer than those 50us the receiver will interpret that as EOI
  // (last byte of data). So we need to take precautions:
  // - disable interrupts between setting CLK high and setting CLK low
  // - get the data byte to send before setting CLK high
  // - wait for DATA high in a blocking loop
  // - place this function in RAM for platforms that need/support it
  uint8_t data = numData>0 ? m_currentDevice->peek() : 0xFF;

  startParallelTransaction();

  // prepare data (bus is still in INPUT mode so the data will not be visible yet)
  // (doing it now saves time to meet the 50us timeout after DATA high)
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
  else if( numData==1 )
    {
      // last data byte => keep CLK high (signals EOI) and wait for receiver to 
      // confirm EOI by HIGH->LOW->HIGH pulse on DATA
      bool ok = (waitPinDATA(LOW) && waitPinDATA(HIGH));
      if( !ok ) { atnRequest(); interrupts(); endParallelTransaction(); return false; }
    }

  // output data on parallel bus
  JDEBUG1();
  setParallelBusModeOutput();
  JDEBUG0();

  // set CLK low (signal "data ready")
  writePinCLK(LOW);

  interrupts();
  endParallelTransaction();

  // discard data byte in device (read by peek() before)
  m_currentDevice->read();

  // remember initial bytes of data sent (see comment in transmitDolphinBurst)
  if( m_secondary==0x60 && m_bufferCtr<PARALLEL_PREBUFFER_BYTES ) 
    m_buffer[m_bufferCtr++] = data;

  // wait for receiver to confirm receipt (must confirm within 1ms)
  bool res = waitPinDATA(LOW, 1000);
  
  // release parallel bus
  setParallelBusModeInput();
  
  return res;
}


bool IECBusHandler::receiveDolphinBurst()
{
  // NOTE: we only get here if sender has already signaled ready-to-send
  // by pulling CLK low
  uint8_t n = 0;

  // clear any previous handshakes
  parallelBusHandshakeReceived();

  // pull DATA low
  writePinDATA(LOW);

  // confirm burst mode transmission
  parallelBusHandshakeTransmit();

  // keep going while CLK is low
  bool eoi = false;
  while( !eoi )
    {
      // wait for "data ready" handshake, return if ATN is asserted (high)
      if( !waitParallelBusHandshakeReceived() ) return false;

      // CLK=high means EOI ("final byte of data coming")
      eoi = readPinCLK();

      // get received data byte
      m_buffer[n++] = readParallelData();

      if( n<m_bufferSize && !eoi )
        {
          // data received and buffered  => send handshake
          parallelBusHandshakeTransmit();
        }
      else if( m_currentDevice->write(m_buffer, n, eoi)==n )
        {
          // data written successfully => send handshake
          parallelBusHandshakeTransmit();
          n = 0;
        }
      else
        {
          // error while writing data => release DATA to signal error condition and exit
          writePinDATA(HIGH);
          return false;
        }
    }

  return true;
}


bool IECBusHandler::transmitDolphinBurst()
{
  // NOTE: we only get here if sender has already signaled ready-to-receive
  // by pulling DATA low

  // send handshake to confirm burst transmission (Dolphin kernal EEDA)
  parallelBusHandshakeTransmit();

  // give the host some time to see our confirmation
  // if we send the next handshake too quickly then the host will see only one,
  // the host will be busy printing the load address after seeing the confirmation
  // so nothing is lost by waiting a good long time before the next handshake
  delayMicroseconds(1000);

  // switch parallel bus to output
  setParallelBusModeOutput();

  // when loading a file, DolphinDos switches to burst mode by sending "XQ" after
  // the transmission has started. The kernal does so after the first two bytes
  // were sent, MultiDubTwo after one byte. After swtiching to burst mode, the 1541
  // then re-transmits the bytes that were already sent.
  for(uint8_t i=0; i<m_bufferCtr; i++)
    {
      // put data on bus
      writeParallelData(m_buffer[i]);

      // send handshake (see "send handshake" comment below)
      noInterrupts();
      parallelBusHandshakeTransmit();
      parallelBusHandshakeReceived();
      interrupts();

      // wait for received handshake
      if( !waitParallelBusHandshakeReceived() ) { setParallelBusModeInput(); return false; }
    }

  // get data from the device and transmit it
  uint8_t n;
  while( (n=m_currentDevice->read(m_buffer, m_bufferSize))>0 )
    {
      startParallelTransaction();
      for(uint8_t i=0; i<n; i++)
        {
          // put data on bus
          writeParallelData(m_buffer[i]);

          // send handshake
          // sending the handshake can induce a pulse on the receive handhake
          // line so we clear the receive handshake after sending, note that we
          // can't have an interrupt take up time between sending the handshake
          // and clearing the receive handshake
          noInterrupts();
          parallelBusHandshakeTransmit();
          parallelBusHandshakeReceived();
          interrupts();
          // wait for receiver handshake
          while( !parallelBusHandshakeReceived() )
            if( !readPinATN() || readPinDATA() )
              {
                // if receiver released DATA or pulled ATN low then there
                // was an error => release bus and CLK line and return
                setParallelBusModeInput();
                writePinCLK(HIGH);
                endParallelTransaction();
                return false;
              }
        }
      endParallelTransaction();
    }

  // switch parallel bus back to input
  setParallelBusModeInput();

  // after seeing our end-of-data and confirmit it, receiver waits for 2ms
  // for us to send the handshake (below) => no interrupts, otherwise we may
  // exceed the timeout
  noInterrupts();

  // signal end-of-data
  writePinCLK(HIGH);

  // wait for receiver to confirm
  if( !waitPinDATA(HIGH) ) { interrupts(); return false; }

  // send handshake
  parallelBusHandshakeTransmit();

  interrupts();

  return true;
}

#endif //IEC_FP_DOLPHIN

