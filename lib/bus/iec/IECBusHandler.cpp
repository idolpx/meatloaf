// -----------------------------------------------------------------------------
// Copyright (C) 2023 David Hansel
//
// This implementation is based on the code used in the VICE emulator.
// The corresponding code in VICE (src/serial/serial-iec-device.c) was 
// contributed to VICE by me in 2003.
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

#define IEC_BUSHANDLER_DEFINE_GLOBALS
#include "IECBusHandlerInternal.h"

#if defined(ARDUINO)
#include <Arduino.h>
#elif defined(ESP_PLATFORM)
#include "../../../include/esp-idf-arduino.h"
#endif

#ifndef ESP_IDF_VERSION_VAL
#define ESP_IDF_VERSION_VAL(x,y,z) 0
#endif

#ifndef NOT_AN_INTERRUPT
#define NOT_AN_INTERRUPT -1
#endif

#ifndef INTERRUPT_FCN_ARG
#define INTERRUPT_FCN_ARG
#endif

#if IEC_MAX_DEVICES>30
#error "Maximum allowed number of devices is 30"
#endif


// delayMicroseconds on some platforms does not work if called when interrupts are disabled
// => this version does work on all supported platforms
void RAMFUNC(delayMicrosecondsISafe)(uint16_t t)
{
#if defined(ARDUINO_ARCH_RP2040)
  // For unknown reasons, using the code in the #else branch on RP2040 can sometimes cause
  // delays to be much longer than intended. This is observed when called from parallelBusHandshakeTransmit()
  // while saving files, causing corruption of the saved data (due to missed bytes).
  // Using "busy_wait_at_least_cycles" avoids those cases.
  busy_wait_at_least_cycles((clock_get_hz(clk_sys)/1000000) * t);
#else
  timer_init();
  timer_reset();
  timer_start();
  while( t>125 ) { timer_wait_until(125); timer_reset(); t -= 125; }
  timer_wait_until(t);
  timer_stop();
#endif
}


// -----------------------------------------------------------------------------------------



IECBusHandler *IECBusHandler::s_bushandler = NULL;


void RAMFUNC(IECBusHandler::writePinCTRL)(bool v)
{
  if( m_pinCTRL!=0xFF )
    digitalWrite(m_pinCTRL, v);
}



bool IECBusHandler::waitTimeout(uint16_t timeout, uint8_t cond)
{
  // This function may be called in code where interrupts are disabled.
  // Calling micros() when interrupts are disabled does not work on all
  // platforms, some return incorrect values, others may re-enable interrupts
  // So we use our high-precision timer. However, on some platforms that timer
  // can only count up to 127 microseconds so we have to go in small increments.

  timer_init();
  timer_reset();
  timer_start();
  while( true )
    {
      switch( cond )
        {
        case TC_DATA_LOW:
          if( readPinDATA() == LOW  ) return true;
          break;

        case TC_DATA_HIGH:
          if( readPinDATA() == HIGH ) return true;
          break;

        case TC_CLK_LOW:
          if( readPinCLK()  == LOW  ) return true;
          break;

        case TC_CLK_HIGH:
          if( readPinCLK()  == HIGH ) return true;
          break;
        }

      if( ((m_flags & P_ATN)!=0) == readPinATN() )
        {
          // ATN changed state => abort with FALSE
          return false;
        }
      else if( timeout<100 )
        {
          if( !timer_less_than(timeout) )
            {
              // timeout has expired => if there was no condition to wait for
              // then return TRUE, otherwise return FALSE (because the condition was not met)
              return cond==TC_NONE;
            }
        }
      else if( !timer_less_than(100) )
        {
          // subtracting from the timout value like below is not 100% precise (we may wait
          // a few microseconds too long because the timer may already have counter further)
          // but this function is not meant for SUPER timing-critical code so that's ok.
          timer_reset();
          timeout -= 100;
        }
    }
}


void IECBusHandler::waitPinATN(bool state)
{
#ifdef ESP_PLATFORM
  // waiting indefinitely with interrupts disabled on ESP32 is bad because
  // the interrupt WDT will reboot the system if we wait too long
  // => if interrupts are disabled then briefly enable them before the timeout to "feed" the WDT
  uint64_t t = esp_timer_get_time();
  while( readPinATN()!=state )
    {
      if( !haveInterrupts && (esp_timer_get_time()-t)>IWDT_FEED_TIME )
        {
          interrupts(); noInterrupts();
          t = esp_timer_get_time();
        }
    }
#else
  while( readPinATN()!=state );
#endif
}


bool IECBusHandler::waitPinDATA(bool state, uint16_t timeout)
{
  // (if timeout is not given it defaults to 1000us)
  // if ATN changes (i.e. our internal ATN state no longer matches the ATN signal line)
  // or the timeout is met then exit with error condition
  if( timeout==0 )
    {
#ifdef ESP_PLATFORM
      // waiting indefinitely with interrupts disabled on ESP32 is bad because
      // the interrupt WDT will reboot the system if we wait too long
      // => if interrupts are disabled then briefly enable them before the timeout to "feed" the WDT
      uint64_t t = esp_timer_get_time();
      while( readPinDATA()!=state )
        {
          if( ((m_flags & P_ATN)!=0) == readPinATN() )
            return false;
          else if( !haveInterrupts && (esp_timer_get_time()-t)>IWDT_FEED_TIME )
            {
              interrupts(); noInterrupts();
              t = esp_timer_get_time();
            }
        }
#else
      // timeout is 0 (no timeout)
      while( readPinDATA()!=state )
        if( ((m_flags & P_ATN)!=0) == readPinATN() )
          return false;
#endif
    }
  else
    {
      // if waitTimeout for the given condition fails then exit
      if( !waitTimeout(timeout, state ? TC_DATA_HIGH : TC_DATA_LOW) ) return false;
    }

  // DATA LOW can only be properly detected if ATN went HIGH->LOW
  // (m_flags&ATN)==0 and readPinATN()==0)
  // since other devices may have pulled DATA LOW
  return state || (m_flags & P_ATN) || readPinATN();
}


bool IECBusHandler::waitPinCLK(bool state, uint16_t timeout)
{
  // (if timeout is not given it defaults to 1000us)
  // if ATN changes (i.e. our internal ATN state no longer matches the ATN signal line)
  // or the timeout is met then exit with error condition
  if( timeout==0 )
    {
#ifdef ESP_PLATFORM
      // waiting indefinitely with interrupts disabled on ESP32 is bad because
      // the interrupt WDT will reboot the system if we wait too long
      // => if interrupts are disabled then briefly enable them before the timeout to "feed" the WDT
      uint64_t t = esp_timer_get_time();
      while( readPinCLK()!=state )
        {
          if( ((m_flags & P_ATN)!=0) == readPinATN() )
            return false;
          else if( !haveInterrupts && (esp_timer_get_time()-t)>IWDT_FEED_TIME )
            {
              interrupts(); noInterrupts();
              t = esp_timer_get_time();
            }
        }
#else
      // timeout is 0 (no timeout)
      while( readPinCLK()!=state )
        if( ((m_flags & P_ATN)!=0) == readPinATN() )
          return false;
#endif
    }
  else
    {
      // if waitTimeout for the given condition fails then exit
      if( !waitTimeout(timeout, state ? TC_CLK_HIGH : TC_CLK_LOW) ) return false;
    }
  
  return true;
}


void IECBusHandler::sendSRQ()
{
  if( m_pinSRQ!=0xFF )
    {
#if !defined(IEC_USE_LINE_DRIVERS)
      digitalWrite(m_pinSRQ, LOW);
      pinMode(m_pinSRQ, OUTPUT);
      delayMicrosecondsISafe(1);
      pinMode(m_pinSRQ, INPUT);
#elif defined(IEC_USE_INVERTED_LINE_DRIVERS)
      digitalWrite(m_pinSRQ, HIGH);
      delayMicrosecondsISafe(1);
      digitalWrite(m_pinSRQ, LOW);
#else
      digitalWrite(m_pinSRQ, LOW);
      delayMicrosecondsISafe(1);
      digitalWrite(m_pinSRQ, HIGH);
#endif
    }
}


#ifdef IEC_USE_LINE_DRIVERS
IECBusHandler::IECBusHandler(uint8_t pinATN, uint8_t pinCLK, uint8_t pinCLKout, uint8_t pinDATA, uint8_t pinDATAout, uint8_t pinRESET, uint8_t pinCTRL, uint8_t pinSRQ)
#else
IECBusHandler::IECBusHandler(uint8_t pinATN, uint8_t pinCLK, uint8_t pinDATA, uint8_t pinRESET, uint8_t pinCTRL, uint8_t pinSRQ)
#endif
#if defined(IEC_SUPPORT_PARALLEL)
#if defined(IEC_SUPPORT_PARALLEL_XRA1405)
#if defined(ESP_PLATFORM)
  // ESP32
: m_pinParallelSCK(18),
  m_pinParallelCOPI(23),
  m_pinParallelCIPO(19),
  m_pinParallelCS(22),
  m_pinParallelHandshakeTransmit(4),
  m_pinParallelHandshakeReceive(36)
#elif defined(ARDUINO_ARCH_RP2040)
  // Raspberry Pi Pico
: m_pinParallelCS(20),
  m_pinParallelCIPO(16),
  m_pinParallelCOPI(19),
  m_pinParallelSCK(18),
  m_pinParallelHandshakeTransmit(6),
  m_pinParallelHandshakeReceive(15)
#elif defined(__AVR_ATmega328P__) || defined(ARDUINO_UNOR4)
  // Arduino UNO, Pro Mini, Micro, Nano
: m_pinParallelCS(9),
  m_pinParallelCIPO(12),
  m_pinParallelCOPI(11),
  m_pinParallelSCK(13),
  m_pinParallelHandshakeTransmit(7),
  m_pinParallelHandshakeReceive(2)
#else
#error "Parallel cable using XRA1405 not supported on this platform"
#endif
#else // !IEC_SUPPORT_PARALLEL_XRA1405
#if defined(ESP_PLATFORM)
  // ESP32
: m_pinParallelHandshakeTransmit(4),
  m_pinParallelHandshakeReceive(36),
  m_pinParallel{13,14,15,16,17,25,26,27}
#elif defined(ARDUINO_ARCH_RP2040)
  // Raspberry Pi Pico
: m_pinParallelHandshakeTransmit(6),
  m_pinParallelHandshakeReceive(15),
  m_pinParallel{7,8,9,10,11,12,13,14}
#elif defined(__SAM3X8E__)
  // Arduino Due
: m_pinParallelHandshakeTransmit(52),
  m_pinParallelHandshakeReceive(53),
  m_pinParallel{51,50,49,48,47,46,45,44}
#elif defined(__AVR_ATmega328P__) || defined(ARDUINO_UNOR4)
  // Arduino UNO, Pro Mini, Nano
: m_pinParallelHandshakeTransmit(7),
  m_pinParallelHandshakeReceive(2),
  m_pinParallel{A0,A1,A2,A3,A4,A5,8,9}
#elif defined(__AVR_ATmega2560__)
  // Arduino Mega 2560
: m_pinParallelHandshakeTransmit(30),
  m_pinParallelHandshakeReceive(2),
  m_pinParallel{22,23,24,25,26,27,28,29}
#else
#error "Parallel cable not supported on this platform"
#endif
#endif // IEC_SUPPORT_PARALLEL_XRA1405
#endif // IEC_SUPPORT_PARALLEL
{
  m_numDevices = 0;
  m_exclusiveDevice = NULL;
  m_inTask     = false;
  m_hostMode   = false;
  m_atnInterruptEnabled = false;
  m_flags      = 0xFF; // 0xFF means: begin() has not yet been called
  m_currentDevice = NULL;
  m_enabled    = false; // task() won't process ATN/transfers until begin() runs (see task())

  m_pinATN       = pinATN;
  m_pinCLK       = pinCLK;
  m_pinDATA      = pinDATA;
  m_pinRESET     = pinRESET;
  m_pinCTRL      = pinCTRL;
  m_pinSRQ       = pinSRQ;
#ifdef IEC_USE_LINE_DRIVERS
  m_pinCLKout    = pinCLKout;
  m_pinDATAout   = pinDATAout;
#endif

#if defined(IEC_SUPPORT_FASTLOAD)
#if IEC_DEFAULT_FASTLOAD_BUFFER_SIZE>254
  m_bufferSize = 254;
#elif IEC_DEFAULT_FASTLOAD_BUFFER_SIZE>0
  m_bufferSize = IEC_DEFAULT_FASTLOAD_BUFFER_SIZE;
#else
  m_buffer = NULL;
  m_bufferSize = 0;
#endif
#endif

#ifdef IOREG_TYPE
  m_bitRESET     = digitalPinToBitMask(pinRESET);
  m_regRESETread = portInputRegister(digitalPinToPort(pinRESET));
  m_bitATN       = digitalPinToBitMask(pinATN);
  m_regATNread   = portInputRegister(digitalPinToPort(pinATN));
  m_bitCLK       = digitalPinToBitMask(pinCLK);
  m_regCLKread   = portInputRegister(digitalPinToPort(pinCLK));
  m_regCLKmode   = portModeRegister(digitalPinToPort(pinCLK));
  m_bitDATA      = digitalPinToBitMask(pinDATA);
  m_regDATAread  = portInputRegister(digitalPinToPort(pinDATA));
  m_regDATAmode  = portModeRegister(digitalPinToPort(pinDATA));
#ifdef IEC_USE_LINE_DRIVERS
  m_bitCLKout    = digitalPinToBitMask(pinCLKout);
  m_regCLKwrite  = portOutputRegister(digitalPinToPort(pinCLKout));
  m_bitDATAout   = digitalPinToBitMask(pinDATAout);
  m_regDATAwrite = portOutputRegister(digitalPinToPort(pinDATAout));
#else
  m_regCLKwrite  = portOutputRegister(digitalPinToPort(pinCLK));
  m_regDATAwrite = portOutputRegister(digitalPinToPort(pinDATA));
#endif
#endif

  m_atnInterrupt = digitalPinToInterrupt(m_pinATN);
}


// Attaches the shared ATN interrupt if this handler owns the ATN pin and
// isn't already attached. Used by begin() and by task()'s RESET handling
// when a RESET-line transition re-enables a previously end()'d bus.
void IECBusHandler::attachATNInterrupt()
{
  if( m_atnInterrupt!=NOT_AN_INTERRUPT && s_bushandler==NULL )
    {
      s_bushandler = this;
#if defined(IEC_USE_LINE_DRIVERS) && defined(IEC_USE_INVERTED_INPUTS)
      attachInterrupt(m_atnInterrupt, atnInterruptFcn, RISING);
#else
      attachInterrupt(m_atnInterrupt, atnInterruptFcn, FALLING);
#endif
      m_atnInterruptEnabled = true;
    }
}


void IECBusHandler::begin()
{
  JDEBUGI();

#if defined(IEC_USE_LINE_DRIVERS)
  pinMode(m_pinCLKout,  OUTPUT);
  pinMode(m_pinDATAout, OUTPUT);
  writePinCLK(HIGH);
  writePinDATA(HIGH);
  if( m_pinSRQ<0xFF )
    {
      pinMode(m_pinSRQ, OUTPUT);
      digitalWrite(m_pinSRQ, HIGH);
    }
#else
  // set pins to output 0 (when in output mode)
  pinMode(m_pinCLK,  OUTPUT); digitalWrite(m_pinCLK, LOW); 
  pinMode(m_pinDATA, OUTPUT); digitalWrite(m_pinDATA, LOW); 
  if( m_pinSRQ<0xFF ) pinMode(m_pinSRQ,   INPUT);
#endif

  pinMode(m_pinATN,   INPUT);
  pinMode(m_pinCLK,   INPUT);
  pinMode(m_pinDATA,  INPUT);
  if( m_pinCTRL<0xFF )  pinMode(m_pinCTRL,  OUTPUT);
  if( m_pinRESET<0xFF ) pinMode(m_pinRESET, INPUT);
  m_flags = 0;
  m_currentDevice = NULL;

  // allow ATN to pull DATA low in hardware
  writePinCTRL(LOW);

  // if the ATN pin is capable of interrupts then use interrupts to detect 
  // ATN requests, otherwise we'll poll the ATN pin in function microTask().
  attachATNInterrupt();

  // call begin() function for all attached devices
  for(uint8_t i=0; i<m_numDevices; i++)
    m_devices[i]->begin();

  // let task() start processing now that begin() has fully completed
  m_enabled = true;
}


void IECBusHandler::end()
{
  // Set this FIRST, before anything else. task() checks m_enabled right
  // after its RESET-pin handling (which stays active even while disabled --
  // see task()), so any call starting after this write skips straight past
  // the ATN/transfer handling. This has to be a flag nothing but begin()/
  // end() ever writes: m_flags gets many m_flags|=/&=~ updates throughout
  // task()'s ATN/transfer handling, so reusing it as a "disabled" sentinel
  // (as a stale comment here used to claim) is not safe -- a task() call
  // already past the ATN section when end() runs could clobber a sentinel
  // value via its own unrelated bitwise ops.
  m_enabled = false;

  // Stop reacting to ATN edges: task() runs continuously on its own FreeRTOS
  // task (possibly on the other core), so if we changed pins before detaching
  // the interrupt, atnInterruptFcn() could still fire and call atnRequest().
  if( m_atnInterrupt!=NOT_AN_INTERRUPT && s_bushandler==this )
    {
      detachInterrupt(m_atnInterrupt);
      s_bushandler = NULL;
    }
  m_atnInterruptEnabled = false;

  m_currentDevice = NULL;

  // Same convention begin() uses: start with P_RESET clear rather than
  // asserting a stale value, so the next task() call resyncs it from a real
  // readPinRESET() rather than possibly reporting a phantom falling edge.
  m_flags = 0;

  // release CLK/DATA (switch to high-Z input) so we stop driving the bus,
  // as if this device were physically unplugged
  writePinCLK(HIGH);
  writePinDATA(HIGH);
  writePinCTRL(HIGH);  // disable ATN->DATA hardware coupling, if present
}


bool IECBusHandler::canServeATN()
{
  return (m_pinCTRL!=0xFF) || (m_atnInterrupt != NOT_AN_INTERRUPT);
}


bool IECBusHandler::isResetPinIdle()
{
  return readPinRESET();
}


bool IECBusHandler::inTransaction()
{
  return (m_flags & (P_LISTENING|P_TALKING))!=0;
}


void IECBusHandler::setATNInterruptEnabled(bool enable)
{
  if( m_atnInterrupt==NOT_AN_INTERRUPT || s_bushandler!=this )
    {
      m_atnInterruptEnabled = false;
      return;
    }

  if( enable==m_atnInterruptEnabled )
    return;

  if( enable )
    {
#if defined(IEC_USE_LINE_DRIVERS) && defined(IEC_USE_INVERTED_INPUTS)
      attachInterrupt(m_atnInterrupt, atnInterruptFcn, RISING);
#else
      attachInterrupt(m_atnInterrupt, atnInterruptFcn, FALLING);
#endif
      m_atnInterruptEnabled = true;
    }
  else
    {
      detachInterrupt(m_atnInterrupt);
      m_atnInterruptEnabled = false;
    }
}


bool IECBusHandler::attachDevice(IECDevice *dev)
{
  if( m_numDevices<IEC_MAX_DEVICES && findDevice(dev->m_devnr, true)==NULL )
    {
      dev->m_handler = this;
      dev->m_flFlags  = 0;
      dev->m_flProtocol = IEC_FL_PROT_NONE;
#ifdef IEC_SUPPORT_PARALLEL
      enableParallelPins();
#endif
      // if IECBusHandler::begin() has been called already then call the device's
      // begin() function now, otherwise it will be called in IECBusHandler::begin() 
      if( m_flags!=0xFF ) dev->begin();

      m_devices[m_numDevices] = dev;
      m_numDevices++;
      return true;
    }
  else
    return false;
}


bool IECBusHandler::detachDevice(IECDevice *dev)
{
  for(uint8_t i=0; i<m_numDevices; i++)
    if( dev == m_devices[i] )
      {
        dev->m_handler = NULL;
        m_devices[i] = m_devices[m_numDevices-1];
        m_numDevices--;
#ifdef IEC_SUPPORT_PARALLEL
        enableParallelPins();
#endif
        if( m_currentDevice==dev )  m_currentDevice = NULL;
        return true;
      }

  return false;
}


void IECBusHandler::setBusExclusive(IECDevice *dev)
{
  m_exclusiveDevice = dev;

  if( dev==NULL ) return;

  // Everything else goes quiet. Not detached and not reset -- just made
  // inactive, so it stops answering ATN; the next RESET puts it back.
  for(uint8_t i=0; i<m_numDevices; i++)
    if( m_devices[i]!=dev )
      m_devices[i]->setActive(false);
}


IECDevice *IECBusHandler::findDevice(uint8_t devnr, bool includeInactive)
{
  for(uint8_t i=0; i<m_numDevices; i++)
    if( devnr == m_devices[i]->m_devnr && (includeInactive || m_devices[i]->isActive()) )
      return m_devices[i];

  return NULL;
}


void RAMFUNC(IECBusHandler::atnInterruptFcn)(INTERRUPT_FCN_ARG)
{ 
  if( s_bushandler!=NULL && !s_bushandler->m_inTask && ((s_bushandler->m_flags & P_ATN)==0) )
    s_bushandler->atnRequest();
}


#if defined(IEC_SUPPORT_FASTLOAD) && !defined(IEC_DEFAULT_FASTLOAD_BUFFER_SIZE)
void IECBusHandler::setBuffer(uint8_t *buffer, uint8_t bufferSize)
{
  m_buffer     = bufferSize>0 ? buffer : NULL;
  m_bufferSize = bufferSize>254 ? 254 : bufferSize;
}
#endif



// ------------------------------------  Generic Fast-Load support routines  ------------------------------------  


uint32_t IECBusHandler::getSupportedFastLoaders()
{
  uint32_t mask = 0;
#ifdef IEC_FP_JIFFY
  mask |= bit(IEC_FP_JIFFY);
#endif
#ifdef IEC_FP_EPYX
  mask |= bit(IEC_FP_EPYX);
#endif
#ifdef IEC_FP_FC3
  mask |= bit(IEC_FP_FC3);
#endif
#ifdef IEC_FP_AR6
  mask |= bit(IEC_FP_AR6);
#endif
#ifdef IEC_FP_HYPRALOAD
  mask |= bit(IEC_FP_HYPRALOAD);
#endif
#ifdef IEC_FP_DOLPHIN
  mask |= bit(IEC_FP_DOLPHIN);
#endif
#ifdef IEC_FP_SPEEDDOS
  mask |= bit(IEC_FP_SPEEDDOS);
#endif
  // software fast loaders -- no wiring to check for, see fastload.h
#ifdef IEC_FP_TURBODISK
  mask |= (1UL<<IEC_FP_TURBODISK);
#endif
#ifdef IEC_FP_DREAMLOAD
  mask |= (1UL<<IEC_FP_DREAMLOAD);
#endif
#ifdef IEC_FP_ULOAD3
  mask |= (1UL<<IEC_FP_ULOAD3);
#endif
#ifdef IEC_FP_GIJOE
  mask |= (1UL<<IEC_FP_GIJOE);
#endif
#ifdef IEC_FP_GEOS
  mask |= (1UL<<IEC_FP_GEOS);
#endif
#ifdef IEC_FP_WHEELS
  mask |= (1UL<<IEC_FP_WHEELS);
#endif
#ifdef IEC_FP_NIPPON
  mask |= (1UL<<IEC_FP_NIPPON);
#endif
#ifdef IEC_FP_ELOAD1
  mask |= (1UL<<IEC_FP_ELOAD1);
#endif
#ifdef IEC_FP_MMZAK
  mask |= (1UL<<IEC_FP_MMZAK);
#endif
#ifdef IEC_FP_N0SDOS
  mask |= (1UL<<IEC_FP_N0SDOS);
#endif
#ifdef IEC_FP_SAMSJOURNEY
  mask |= (1UL<<IEC_FP_SAMSJOURNEY);
#endif
  return mask;
}

bool IECBusHandler::isFastLoaderSupported(uint8_t loader)
{
  return (loader<=31) && ((1UL<<loader) & getSupportedFastLoaders())!=0;
}


bool IECBusHandler::enableFastLoader(IECDevice *dev, uint8_t loader, bool enable)
{
  if( !isFastLoaderSupported(loader) ) return false;

  switch( loader )
    {
#ifdef IEC_FP_JIFFY
    case IEC_FP_JIFFY:
      dev->m_flFlags &= ~(S_JIFFY_DETECTED|S_JIFFY_BLOCK);
      break;
#endif
#ifdef IEC_FP_EPYX
    case IEC_FP_EPYX:
      break;
#endif
#ifdef IEC_FP_DOLPHIN
    case IEC_FP_DOLPHIN:
      dev->m_flFlags |= S_DOLPHIN_BURST_ENABLED;
      dev->m_flFlags &= ~S_DOLPHIN_DETECTED;
      if( !checkParallelPins() ) return false;
      enableParallelPins();
      break;
#endif
#ifdef IEC_FP_SPEEDDOS
    case IEC_FP_SPEEDDOS:
      dev->m_flFlags &= ~(S_SPEEDDOS_DETECTED);
      if( !checkParallelPins() ) return false;
      enableParallelPins();
      break;
#endif

    default:
      break;
    }

  return true;
}


void IECBusHandler::fastLoadRequest(IECDevice *dev, uint8_t protocol, uint8_t request)
{
  m_currentDevice = dev;

  switch( protocol )
    {
#ifdef IEC_FP_DOLPHIN
    case IEC_FP_DOLPHIN:
      m_timeoutStart = micros();
      m_timeoutDuration = (request==IEC_FL_PROT_SAVE ? 500 : 200);
      break;
#endif
#ifdef IEC_FP_FC3
    case IEC_FP_FC3:
      m_timeoutStart = micros();
      m_timeoutDuration = 20000;
      if( request == IEC_FL_PROT_LOAD || request == IEC_FL_PROT_LOADIMG )
        {
          m_buffer[0] = 7; // not used, appears to be always 7
          m_buffer[1] = 0; // first block number
        }
      else if( request == IEC_FL_PROT_SAVE )
        {
          // signal "not ready"
          writePinDATA(LOW);
        }
      break;
#endif
#ifdef IEC_FP_AR6
    case IEC_FP_AR6:
      // signal "not ready"
      writePinCLK(LOW);

      // for LOAD request: set block count
      m_buffer[255] = 0;

      // wait 500us to make sure sender has pulled DATA low and seen our CLK low
      m_timeoutStart = micros();
      m_timeoutDuration = 500;
      break;
#endif

#ifdef IEC_FP_HYPRALOAD
    case IEC_FP_HYPRALOAD:
      // signal "not ready"
      writePinDATA(LOW);

      // cancel any ATN request that has already occurred
      // (HypraLoad uses ATN for clocking the data)
      m_flags &= ~P_ATN;

      // signals that this is the first sector read
      m_buffer[0] = 0x00;

      // wait 15ms to make sure C64 screen is off (affects timing)
      // on a real 1541 this would not be necessary since reading
      // the first sector would take longer than 20ms.
      m_timeoutStart = micros();
      m_timeoutDuration = 15000;
      break;
#endif
      
    default:
      break;
    }
}


// ------------------------------------  IEC protocol support routines  ------------------------------------  


bool RAMFUNC(IECBusHandler::receiveIECByteATN)(uint8_t &data, uint8_t bytenum)
{
#if defined(IEC_FP_SPEEDDOS)
  // SpeedDos protocol detection
  // BEFORE receiving secondary address, wait for either:
  //  HIGH->LOW edge (1us pulse) on incoming parallel handshake signal, 
  //      if received pull outgoing parallel handshake signal LOW to confirm
  //  LOW->HIGH edge on ATN or CLK, 
  //      if so then timeout, host does not support SpeedDos
  if( bytenum==2 )
    {
      JDEBUG1();
      IECDevice *dev = findDevice(m_primary & 0x1F);
      if( dev!=NULL && dev->isFastLoaderEnabled(IEC_FP_SPEEDDOS) )
        if( waitParallelBusHandshakeReceivedISafe(true) )
          {
            dev->m_flFlags |= S_SPEEDDOS_DETECTED;
            parallelBusHandshakeTransmit();
          }

      // SpeedDos uses parallel protocol to transmit the secondary address
      if( dev!=NULL && (dev->m_flFlags & S_SPEEDDOS_DETECTED) )
        {
          // wait for CLK=1
          JDEBUG0();
          if( !waitPinCLK(HIGH, 0) ) return false;

          // release DATA ("ready-for-data")
          JDEBUG1();
          writePinDATA(HIGH);

          // wait for CLK=0
          if( !waitPinCLK(LOW, 0) ) return false;

          // wait for parallel data to be ready
          if( !waitParallelBusHandshakeReceivedISafe() ) return false;
          JDEBUG1();
      
          // get the parallel data
          data = readParallelData();

          // let the sender know we got the data
          parallelBusHandshakeTransmit();
          writePinDATA(LOW);
          JDEBUG0();

          return true;
        }
    }
#endif

  // wait for CLK=1
  if( !waitPinCLK(HIGH, 0) ) return false;

  // release DATA ("ready-for-data")
  writePinDATA(HIGH);

  // other devices on the bus may be holding DATA low, the bus master
  // starts its 200us timeout (see below) once DATA goes high.
  if( !waitPinDATA(HIGH, 0) ) return false;

  // wait for sender to set CLK=0 ("ready-to-send")
  if( !waitPinCLK(LOW, 200) )
    {
      // sender did not set CLK=0 within 200us after DATA went high, it is signaling EOI
      // => acknowledge we received it by setting DATA=0 for 80us
      // note that EOI is not really used under ATN but may still be signaled, for example
      // the EPYX FastLoad cartridge's sector read/write function may signal EOI under ATN
      // also, game "Jet (Sublogic, 1986)
      writePinDATA(LOW);
      if( !waitTimeout(80) ) return false;
      writePinDATA(HIGH);

      // keep waiting for CLK=0
      // must wait indefinitely since other devices may be holding DATA low until
      // they are ready but bus master will start sending as soon as all devices
      // have released DATA
      if( !waitPinCLK(LOW, 0) ) return false;
    }

  // receive data bits
  data = 0;
  for(uint8_t i=0; i<8; i++)
    {
      JDEBUG1();

#ifdef IEC_FP_JIFFY
      // JiffyDos protocol detection
      if( (i==7) && (bytenum==1) && !waitPinCLK(HIGH, 200) )
        {
          IECDevice *dev = findDevice((data>>1)&0x1F);
          JDEBUG0();
          if( (dev!=NULL) && (dev->isFastLoaderEnabled(IEC_FP_JIFFY)) )
            {
              JDEBUG1();
              // when sending final bit of primary address byte under ATN, host
              // delayed CLK=1 by more than 200us => JiffyDOS protocol detection
              // => if JiffyDOS support is enabled and we are being addressed then
              // respond that we support the protocol by pulling DATA low for 80us
              dev->m_flFlags |= S_JIFFY_DETECTED;
              writePinDATA(LOW);
              if( !waitTimeout(80) ) return false;
              writePinDATA(HIGH);
            }
        }
#endif

      // wait for CLK=1, signaling data is ready
      if( !waitPinCLK(HIGH) ) return false;
      JDEBUG0();

      // read DATA bit
      data >>= 1;
      if( readPinDATA() ) data |= 0x80;

      // wait for CLK=0, signaling "data not ready"
      if( !waitPinCLK(LOW) ) return false;
    }

  // Acknowledge receipt by pulling DATA low
  writePinDATA(LOW);

#if defined(IEC_FP_DOLPHIN)
  // DolphinDos parallel cable detection:
  // after receiving secondary address, wait for either:
  //  HIGH->LOW edge (1us pulse) on incoming parallel handshake signal, 
  //      if received pull outgoing parallel handshake signal LOW to confirm
  //  LOW->HIGH edge on ATN or CLK,
  //      if so then timeout, host does not support DolphinDos

  if( bytenum==2 )
    {
      IECDevice *dev = findDevice(m_primary & 0x1F);
      if( dev!=NULL && dev->isFastLoaderEnabled(IEC_FP_DOLPHIN) )
        if( waitParallelBusHandshakeReceivedISafe(true) )
          {
            dev->m_flFlags |= S_DOLPHIN_DETECTED;
            parallelBusHandshakeTransmit();
          }
    }
#endif

  return true;
}


bool RAMFUNC(IECBusHandler::receiveIECByte)(bool canWriteOk)
{
  // NOTE: we only get here if sender has already signaled ready-to-send
  // by releasing CLK
  bool eoi = false;

  noInterrupts();

  // release DATA ("ready-for-data")
  writePinDATA(HIGH);

  // wait for sender to set CLK=0 ("ready-to-send")
  if( !waitPinCLK(LOW, 200) )
    {
      // exit if waitPinCLK returned because of falling edge on ATN
      if( !readPinATN() ) { interrupts(); return false; }

      // sender did not set CLK=0 within 200us after we set DATA=1, it is signaling EOI
      // => acknowledge we received it by setting DATA=0 for 80us
      eoi = true;
      writePinDATA(LOW);
      if( !waitTimeout(80) ) { interrupts(); return false; }
      writePinDATA(HIGH);

      // keep waiting for CLK=0
      if( !waitPinCLK(LOW) ) { interrupts(); return false; }
    }

  // receive data bits
  uint8_t data = 0;
  for(uint8_t i=0; i<8; i++)
    {
      // wait for CLK=1, signaling data is ready
      if( !waitPinCLK(HIGH) ) { interrupts(); return false; }

      // read DATA bit
      data >>= 1;
      if( readPinDATA() ) data |= 0x80;

      // wait for CLK=0, signaling "data not ready"
      if( !waitPinCLK(LOW) ) { interrupts(); return false; }
    }

  // if device can receive data, acknowledge receipt by pulling DATA low
  // (do this before allowing interrupts to avoid long interrupts from
  // exceeding the 1ms timeout we have to acknowledge the receipt)
  if( canWriteOk ) writePinDATA(LOW);

  interrupts();

  if( canWriteOk )
    {
      // pass received data on to the device
      m_currentDevice->write(data, eoi);
      return true;
    }
  else
    {
      // canWrite() reported an error
      return false;
    }
}


bool RAMFUNC(IECBusHandler::transmitIECByte)(uint8_t numData)
{
  // check whether ready-to-receive was already signaled by the 
  // receiver before we signaled ready-to-send. The 1541 ROM 
  // disassembly (E919-E924) suggests that this signals a "verify error" 
  // condition and we should send EOI. Note that the C64 kernal does not
  // actually do this signaling during a "verify" operation so I don't
  // know whether my interpretation here is correct. However, some 
  // programs (e.g. "copy 190") lock up if we don't handle this case.
  bool verifyError = readPinDATA();

  noInterrupts();

  // signal "ready-to-send" (CLK=1)
  writePinCLK(HIGH);
  
  // wait (indefinitely, no timeout) for DATA HIGH ("ready-to-receive")
  // NOTE: this must be in a blocking loop since the receiver starts counting
  // up the EOI timeout immediately after setting DATA HIGH. If we had exited the 
  // "task" function then it might be more than 200us until we get back here
  // to pull CLK low and the receiver will interpret that delay as EOI.
  if( !waitPinDATA(HIGH, 0) ) { interrupts(); return false; }
  
  if( numData==1 || verifyError )
    {
      // only this byte left to send => signal EOI by keeping CLK=1
      // wait for receiver to acknowledge EOI by setting DATA=0 then DATA=1
      // if we got here by "verifyError" then wait indefinitely because we
      // didn't enter the "wait for DATA high" state above
      if( !waitPinDATA(LOW, verifyError ? 0 : 1000) ) { interrupts(); return false; }
      if( !waitPinDATA(HIGH) ) { interrupts(); return false; }
    }

  // if we have nothing to send then there was some kind of error 
  // => aborting at this stage will signal the error condition to the receiver
  //    (e.g. "File not found" for LOAD)
  if( numData==0 ) { interrupts(); return false; }

  // signal "data not valid" (CLK=0)
  writePinCLK(LOW);

  interrupts();

  bool doPeek = false;
#if defined(IEC_FP_AR6) || defined(IEC_FP_HYPRALOAD)
  // After opening a file to load, Action Replay 6 and HypraLoad read the first 2 bytes (load address)
  // but then signal "ready" (DATA high) again before pulling ATN low which makes us
  // read and discard the third byte if we don't use peek() here
#ifdef IEC_FP_AR6
  doPeek |= m_currentDevice->isFastLoaderEnabled(IEC_FP_AR6);
#endif
#ifdef IEC_FP_HYPRALOAD
  doPeek |= m_currentDevice->isFastLoaderEnabled(IEC_FP_HYPRALOAD);
#endif

  // get the data byte from the device
  uint8_t data = doPeek ? m_currentDevice->peek() : m_currentDevice->read();
#else
  uint8_t data = m_currentDevice->read();
#endif

  // transmit the byte
  for(uint8_t i=0; i<8; i++)
    {
      // signal "data not valid" (CLK=0)
      writePinCLK(LOW);

      // set bit on DATA line
      writePinDATA((data & 1)!=0);

      // hold for 80us
      if( !waitTimeout(80) ) return false;
      
      // signal "data valid" (CLK=1)
      writePinCLK(HIGH);

      // hold for 70us (60us is not enough for game "Mercenary" or "Tracer Junction")
      if( !waitTimeout(70) ) return false;

      // next bit
      data >>= 1;
    }

  // pull CLK=0 and release DATA=1 to signal "busy"
  writePinCLK(LOW);
  writePinDATA(HIGH);

  // wait for receiver to signal "busy"
  if( !waitPinDATA(LOW) ) return false;

  // discard previously read data byte
  if( doPeek ) m_currentDevice->read();
  
  return true;
}


// called when a falling edge on ATN is detected, either by the pin change
// interrupt handler or by polling within the microTask function
void RAMFUNC(IECBusHandler::atnRequest)()
{
  // check if ATN is actually LOW, if not then just return (stray interrupt request)
  if( readPinATN() ) return;

#ifdef IEC_FP_HYPRALOAD
  // Hypra-Load uses ATN to clock bytes during fastload
  if( m_currentDevice!=NULL && m_currentDevice->m_flProtocol == ((IEC_FP_HYPRALOAD<<3) | IEC_FL_PROT_LOAD) )
    return;
#endif

  JDEBUG1();

  // falling edge on ATN detected (bus master addressing all devices)
  m_flags |= P_ATN;
  m_flags &= ~P_DONE;

  // ignore anything for 100us after ATN falling edge
#ifdef ESP_PLATFORM
  // calling "micros()" (aka esp_timer_get_time()) within an interrupt handler
  // on ESP32 appears to sometimes return incorrect values. This was observed
  // when running Meatloaf on a LOLIN D32 board. So we just note that the 
  // timeout needs to be started and will actually set m_timeoutStart outside 
  // of the interrupt handler within the task() function
  m_timeoutStart = 0xFFFFFFFF;
#else
  m_timeoutStart = micros();
#endif

  // release CLK (in case we were holding it LOW before)
  writePinCLK(HIGH);
  
  // set DATA=0 ("I am here").  If nobody on the bus does this within 1ms,
  // busmaster will assume that "Device not present" 
  writePinDATA(LOW);

  // disable the hardware that allows ATN to pull DATA low
  writePinCTRL(HIGH);

  for(uint8_t i=0; i<m_numDevices; i++)
    {
#ifdef IEC_FP_JIFFY
      m_devices[i]->m_flFlags &= ~(S_JIFFY_DETECTED|S_JIFFY_BLOCK);
#endif
#ifdef IEC_FP_DOLPHIN
      m_devices[i]->m_flFlags &= ~S_DOLPHIN_DETECTED;
#endif
#ifdef IEC_FP_SPEEDDOS
      m_devices[i]->m_flFlags &= ~S_SPEEDDOS_DETECTED;
#endif

      m_devices[i]->m_flProtocol = IEC_FL_PROT_NONE;
    }

  JDEBUG0();
}


void RAMFUNC(IECBusHandler::handleATNSequence)()
{
  // no more interrupts until the ATN sequence is finished. If we allowed interrupts
  // and a long interrupt occurred close to the end of the sequence then we may miss
  // a quick ATN low->high->low sequence, i.e completely missing the start of a new
  // ATN request.
  noInterrupts();

  // P_DONE flag may have gotten set again after it was reset in atnRequest()
  m_flags &= ~P_DONE;

  // bytenum counts the byte number within this ATN sequence,
  // used in receiveIECByteATN for fastloader detection
  uint8_t bytenum = 1;

  m_primary = 0;
  m_secondary = 0;
  if( receiveIECByteATN(m_primary, bytenum) && ((m_primary == 0x3f) || (m_primary == 0x5f) || (findDevice((unsigned int) m_primary & 0x1f)!=NULL)) )
    {
      // this is either UNLISTEN or UNTALK or we were addressed
      // => receive the secondary address, assume 0 if not sent
      uint8_t data;
      while( m_primary!=0x3f && m_primary!=0x5f && !readPinATN() && receiveIECByteATN(data, ++bytenum) )
        {
          // some games send multiple primary address bytes ("gwendolyn") or
          // send UNLISTEN after LISTEN ("tracer sanction"), within the same ATN sequence
          if( (data & 0xF0)==0xE0 )
            {
              // the 1541 ROM processes CLOSE requests immdediately while still
              // handling the ATN sequence ($E8CE in 1541 ROM), all other requests are handled
              // after the ATN sequence finishes. Used in game "tracer sanction".
              IECDevice *dev = findDevice(m_primary & 0x1F);
              if( dev!=NULL )
                {
                  dev->listen(data);
                  m_flags &= ~P_TALKING;
                  m_flags |= P_LISTENING;
                  m_secondary = data;
                }
            }
          else if( (data & 0x60)==0x60 )
            {
              // 1541 ROM stops processing after receiving secondary address (except CLOSE)
              m_secondary = data;
              break;
            }
          else if( data==0x3f || data==0x5f )
            m_primary = data;
          else if( ((data & 0x20)==0x20 || (data & 0x40)==0x40) && (data & 0x0f)==(m_primary & 0x0f) )
            { m_primary = data; m_secondary = 0; }
          else
            break;
        }

      // make sure ATN has been released
      waitPinATN(HIGH);
      m_flags &= ~P_ATN;

      // allow ATN to pull DATA low in hardware
      writePinCTRL(LOW);
          
      // see Hypra-Load comment below
      bool dataIdleState = HIGH;

      if( m_primary == 0x3f )
        {
          // all devices were told to stop listening
          if( m_flags & P_LISTENING )
            {
              if( m_currentDevice!=NULL ) m_currentDevice->unlisten();
              m_currentDevice = NULL;
              m_flags &= ~P_LISTENING;
            }
        }
      else if( m_primary == 0x5f )
        {
          // all devices were told to stop talking
          if( m_flags & P_TALKING )
            {
              if( m_currentDevice!=NULL ) m_currentDevice->untalk();

              m_currentDevice = NULL;
              m_flags &= ~P_TALKING;
            }
#ifdef IEC_FP_HYPRALOAD
          else if( m_flags & P_LISTENING )
            {
              // Hypra-Load sends LISTEN->DATA->UNTALK when transmitting
              // the final M-E command after uploading data. Also, it expects DATA=LOW
              // within 3us after setting ATN low again following the M-E command.
              // While the atnRequest() function does pull DATA low quickly, especially when
              // run in an interrupt, it is not always guaranteed within 3us. To avoid problems,
              // we just keep DATA low already after this UNTALK command
              if( m_currentDevice!=NULL ) { m_currentDevice->unlisten(); dataIdleState = LOW; }

              m_currentDevice = NULL;
              m_flags &= ~P_LISTENING;
            }
#endif
        }
      else if( (m_primary & 0xE0)==0x20 )
        {
          IECDevice *dev = findDevice(m_primary & 0x1F);
          if( dev!=NULL )
            {
              // we were told to listen - note that for CLOSE
              // (secondary=0xEx) the listen() call was aready made above
              m_currentDevice = dev;
              if( (m_secondary & 0xF0)!=0xE0 ) m_currentDevice->listen(m_secondary);
              m_flags &= ~P_TALKING;
              m_flags |= P_LISTENING;
#ifdef IEC_FP_DOLPHIN
              // see comments in function receiveDolphinByte
              if( m_secondary==0x61 ) m_bufferCtr = 2*PARALLEL_PREBUFFER_BYTES;
#endif
              // set DATA=0 ("I am here")
              writePinDATA(LOW);
            }
        }
      else if( (m_primary & 0xE0)==0x40 )
        {
          IECDevice *dev = findDevice(m_primary & 0x1F);
          if( dev!=NULL  )
            {
              // we were told to talk
              m_currentDevice = dev;
#ifdef IEC_FP_JIFFY
              if( (m_currentDevice->m_flFlags & S_JIFFY_DETECTED)!=0 && m_secondary==0x61 )
                {
                  // in JiffyDOS, secondary 0x61 when talking enables "block transfer" mode
                  m_secondary = 0x60;
                  m_currentDevice->m_flFlags |= S_JIFFY_BLOCK;
                }
#endif        
              m_currentDevice->talk(m_secondary);
              m_flags &= ~P_LISTENING;
              m_flags |= P_TALKING;
#if defined(IEC_FP_DOLPHIN) || defined(IEC_FP_SPEEDDOS)
              // see comments in function transmitDolphinByte/transmitSpeedDosByte
              if( m_secondary==0x60 ) m_bufferCtr = 0;
#endif
              // wait for bus master to set CLK=1 (and DATA=0) for role reversal
              if( waitPinCLK(HIGH) )
                {
                  // now set CLK=0 and DATA=1
                  writePinCLK(LOW);
                  writePinDATA(HIGH);
                  
                  // wait 80us before transmitting first byte of data
                  delayMicrosecondsISafe(80);
                  m_timeoutDuration = 0;
                }
            }
        }
          
      if( !(m_flags & (P_LISTENING | P_TALKING)) )
        {
          // we're neither listening nor talking => release CLK/DATA
          writePinCLK(HIGH);
          writePinDATA(dataIdleState);
        }
    }
  else
    {
      // either we were not addressed or there was an error receiving the primary address
      delayMicrosecondsISafe(150);
      writePinCLK(HIGH);
      writePinDATA(HIGH);
      waitPinATN(HIGH);
      m_flags &= ~P_ATN;

      // if someone else was told to start talking then we must stop
      if( (m_primary & 0xE0)==0x40 ) m_flags &= ~P_TALKING;

      // allow ATN to pull DATA low in hardware
      writePinCTRL(LOW);
    }

  if( !(m_flags & (P_LISTENING | P_TALKING)) )
    {
      // we're neither listening nor talking
      // => allow the interrupt handler to call atnRequest() again, otherwise on
      // some platforms a long low-priority interrupt (e.g. WiFi interrupt on ESP32)
      // could cause us to fail to react in time to a subsequent ATN high->low edge
      // Note that such interrupts are likely to occur right here after we allow
      // interrupts again because they have been queued up during the time we were
      // in this function.
      m_inTask = false;
    }

  interrupts();
}


void IECBusHandler::handleFastLoadProtocols()
{
  if( m_currentDevice!=NULL )
    {
      uint8_t protocol = m_currentDevice->m_flProtocol;
      if( protocol != IEC_FL_PROT_NONE )
        {
          uint8_t loader = protocol >> 3;
          protocol &= 0x07;

#ifdef IEC_FP_DOLPHIN
          // ------------------ DolphinDos burst transmit handling -------------------
          
          if( (loader==IEC_FP_DOLPHIN) && (protocol==IEC_FL_PROT_LOAD) && (micros()-m_timeoutStart)>m_timeoutDuration && !readPinDATA() )
            {
              // if we are in burst transmit mode, give other devices 200us to release
              // the DATA line and wait for the host to pull DATA LOW

              // pull CLK line LOW (host should have released it by now)
              writePinCLK(LOW);
              
              if( m_currentDevice->m_flFlags & S_DOLPHIN_BURST_ENABLED )
                {
                  // transmit data in burst mode
                  transmitDolphinBurst();
                  
                  // close the file (usually the host sends these but not in burst mode)
                  m_currentDevice->listen(0xE0);
                  m_currentDevice->unlisten();
                  
                  // check whether ATN has been asserted and handle if necessary
                  if( !readPinATN() ) atnRequest();
                }
              else
                {
                  // switch to regular transmit mode
                  m_flags = P_TALKING;
                  m_currentDevice->m_flFlags |= S_DOLPHIN_DETECTED;
                  m_secondary = 0x60;
                }
              
              m_currentDevice->m_flProtocol = IEC_FL_PROT_NONE;
            }

          // ------------------ DolphinDos burst receive handling -------------------
          
          if( (loader==IEC_FP_DOLPHIN) && (protocol==IEC_FL_PROT_SAVE) && (micros()-m_timeoutStart)>m_timeoutDuration && !readPinCLK() )
            {
              // if we are in burst receive mode, wait 500us to make sure host has released CLK after 
              // sending "XZ" burst request (Dolphin kernal ef82), and wait for it to pull CLK low again
              // (if we don't wait at first then we may read CLK=0 already before the host has released it)

              if( m_currentDevice->m_flFlags & S_DOLPHIN_BURST_ENABLED )
                {
                  // transmit data in burst mode
                  receiveDolphinBurst();
          
                  // check whether ATN has been asserted and handle if necessary
                  if( !readPinATN() ) atnRequest();
                }
              else
                {
                  // switch to regular receive mode
                  m_flags = P_LISTENING;
                  m_currentDevice->m_flFlags |= S_DOLPHIN_DETECTED;
                  m_secondary = 0x61;

                  // see comment in function receiveDolphinByte
                  m_bufferCtr = (2*PARALLEL_PREBUFFER_BYTES)-m_bufferCtr;

                  // signal NOT ready to receive
                  writePinDATA(LOW);
                }

              m_currentDevice->m_flProtocol = IEC_FL_PROT_NONE;
            }
#endif

#ifdef IEC_FP_SPEEDDOS
          // ------------------ SpeedDos burstload transfer handling -------------------

          if( (loader==IEC_FP_SPEEDDOS) && (protocol==IEC_FL_PROT_LOAD) )
            {
              transmitSpeedDosFile();

              // either end-of-data or transmission error => we are done
              writePinCLK(HIGH);
              writePinDATA(HIGH);
        
              // close the file (usually the host sends these but not in burst mode)
              m_currentDevice->listen(0xE0);
              m_currentDevice->unlisten();
            
              // check whether ATN has been asserted and handle if necessary
              if( !readPinATN() ) atnRequest();

              m_currentDevice->m_flProtocol = IEC_FL_PROT_NONE;
            }
#endif

#ifdef IEC_FP_EPYX
          // ------------------ Epyx FastLoad transfer handling -------------------
      
          if( (loader==IEC_FP_EPYX) && (protocol==IEC_FL_PROT_HEADER) && readPinDATA() )
            {
              m_currentDevice->m_flProtocol = IEC_FL_PROT_NONE;
              if( !receiveEpyxHeader() )
                {
                  // transmission error
                  writePinCLK(HIGH);
                  writePinDATA(HIGH);
                }
            }
          else if( (loader==IEC_FP_EPYX) && (protocol==IEC_FL_PROT_LOAD) )
            {
              if( !transmitEpyxBlock() )
                {
                  // either end-of-data or transmission error => we are done
                  writePinCLK(HIGH);
                  writePinDATA(HIGH);

                  // close the file (was opened in receiveEpyxHeader)
                  m_currentDevice->listen(0xE0);
                  m_currentDevice->unlisten();

                  // no more data to send
                  m_currentDevice->m_flProtocol = IEC_FL_PROT_NONE;
                }
            }
#ifdef IEC_FP_EPYX_SECTOROPS
          else if( (loader==IEC_FP_EPYX) && (protocol==IEC_FL_PROT_SECTOR) )
            {
              if( !finishEpyxSectorCommand() )
                {
                  // either no more operations or transmission error => we are done
                  writePinCLK(HIGH);
                  writePinDATA(HIGH);

                  // no more sector operations
                  m_currentDevice->m_flProtocol = IEC_FL_PROT_NONE;
                }
            }
#endif
#endif

#ifdef IEC_FP_HYPRALOAD
          // ------------------ HypraLoad transfer handling -------------------

          if( (loader==IEC_FP_HYPRALOAD) && (protocol==IEC_FL_PROT_LOAD) && (micros()-m_timeoutStart)>m_timeoutDuration && !readPinATN() )
            {
              if( !transmitHypraLoadBlock() )
                {
                  // either end-of-data or transmission error => we are done
                  writePinCLK(HIGH);
                  writePinDATA(HIGH);

                  // close the file (HypraLoad does not send an explicit CLOSE after load finishes)
                  m_currentDevice->listen(0xE0);
                  m_currentDevice->unlisten();

                  // no more data to send
                  m_currentDevice->m_flProtocol = IEC_FL_PROT_NONE;
                }
            }
#endif

#ifdef IEC_FP_TURBODISK
          // ------------------ Turbodisk transfer handling -------------------

          if( (loader==IEC_FP_TURBODISK) && (protocol==IEC_FL_PROT_LOAD) )
            {
              if( !transmitTurbodiskBlock() )
                {
                  // either end-of-data or transmission error => we are done
                  writePinCLK(HIGH);
                  writePinDATA(HIGH);

                  // close the file (Turbodisk sends no CLOSE of its own)
                  m_currentDevice->listen(0xE0);
                  m_currentDevice->unlisten();

                  m_currentDevice->m_flProtocol = IEC_FL_PROT_NONE;
                }
            }
#endif

#ifdef IEC_FP_FC3
          // ------------------ Final Cartridge 3 transfer handling -------------------

          if( (loader==IEC_FP_FC3) && (protocol==IEC_FL_PROT_LOAD) && ((m_timeoutDuration==0) || (micros()-m_timeoutStart)>m_timeoutDuration) )
            {
              m_timeoutDuration = 0;
              if( transmitFC3Block()!=1 )
                {
                  // either end-of-data or transmission error => we are done
                  m_currentDevice->m_flProtocol = IEC_FL_PROT_NONE;
                }
            }
          else if( (loader==IEC_FP_FC3) && (protocol==IEC_FL_PROT_LOADIMG) && ((m_timeoutDuration==0) || (micros()-m_timeoutStart)>m_timeoutDuration) )
            {
              m_timeoutDuration = 0;
              if( transmitFC3ImageBlock()!=1 )
                {
                  // either end-of-data or transmission error => we are done
                  m_currentDevice->m_flProtocol = IEC_FL_PROT_NONE;
                }
            }
          else if( (loader==IEC_FP_FC3) && (protocol==IEC_FL_PROT_SAVE) && ((m_timeoutDuration==0) || (micros()-m_timeoutStart)>m_timeoutDuration) )
            {
              int8_t res = receiveFC3Block();
              if( res!=1 )
                {
                  // either no more operations or transmission error => we are done
                  writePinCLK(HIGH);
                  writePinDATA(HIGH);
                  m_currentDevice->m_flProtocol = IEC_FL_PROT_NONE;

                  // close the file (usually the host sends these but not after fast-save)
                  m_currentDevice->listen(0xE1);
                  m_currentDevice->unlisten();
                }
            }
#endif

#ifdef IEC_FP_AR6
          // ------------------ Action Replay 6 transfer handling -------------------

          if( (loader==IEC_FP_AR6) && (protocol==IEC_FL_PROT_LOAD || protocol==IEC_FL_PROT_LOADIMG) &&
              ((m_timeoutDuration==0) || (micros()-m_timeoutStart)>m_timeoutDuration) )
            {
              m_timeoutDuration = 0;

              // use AR3 protocol for LOADIMG (we support AR3 stand-alone LOADER, not AR6)
              int8_t res = transmitAR6Block(protocol==IEC_FL_PROT_LOAD);
              if( res!=1 )
                {
                  // either end-of-data or transmission error => we are done
                  writePinCLK(HIGH);
                  writePinDATA(HIGH);
                  m_currentDevice->m_flProtocol = IEC_FL_PROT_NONE;

                  if( res<0 )
                    {
                      // close the file (usually the host sends these but not if the transfer was interrupted)
                      m_currentDevice->listen(0xE0);
                      m_currentDevice->unlisten();
                    }
                }
            }
          else if( (loader==IEC_FP_AR6) && (protocol==IEC_FL_PROT_SAVE) && ((m_timeoutDuration==0) || (micros()-m_timeoutStart)>m_timeoutDuration) )
            {
              if( receiveAR6Block()!=1 )
                {
                  // either no more blocks or transmission error => we are done
                  writePinCLK(HIGH);
                  writePinDATA(HIGH);
                  m_currentDevice->m_flProtocol = IEC_FL_PROT_NONE;
                }
            }
#endif
        }
    }
}


void IECBusHandler::task()
{
  if( m_hostMode )
    return;

  // ------------------ check for activity on RESET pin -------------------
  // Checked even while disabled (after end()/sleep): a RESET-line transition
  // returns the bus to its default enabled state, the same way a real drive
  // responds to the C64's reset line regardless of its own power state.

  if( readPinRESET() )
    m_flags |= P_RESET;
  else if( (m_flags & P_RESET)!=0 )
    {
      // falling edge on RESET pin
      m_currentDevice = NULL;
      m_flags = 0;

      // release CLK and DATA, allow ATN to pull DATA low in hardware
      writePinCLK(HIGH);
      writePinDATA(HIGH);
      writePinCTRL(LOW);

      if( !m_enabled )
        {
          // bus was end()'d -- RESET brings it back to its default enabled
          // state, including re-arming the ATN interrupt end() tore down
          m_enabled = true;
          attachATNInterrupt();
        }

      // A RESET is what ends bus-exclusive mode. Clear it BEFORE the reset
      // calls below, because those are what restore each device's persisted
      // enabled flag -- clearing afterwards would leave the slept ones off.
      m_exclusiveDevice = NULL;

      // call "reset" function for attached devices
      for(uint8_t i=0; i<m_numDevices; i++)
        m_devices[i]->reset();
    }

  // ------------------ check for activity on ATN pin -------------------
  // don't do anything else if the bus hasn't been begin()'d, or is end()'d
  // and hasn't seen a RESET-line transition yet (handled above)
  if( !m_enabled ) return;

  // prevent interrupt handler from calling atnRequest()
  m_inTask = true;

  if( !(m_flags & P_ATN) && !readPinATN() )
    {
      // falling edge on ATN (bus master addressing all devices)
      atnRequest();
    } 

#ifdef ESP_PLATFORM
  // see comment in atnRequest function
  if( (m_flags & P_ATN)!=0 && !readPinATN() &&
      (m_timeoutStart==0xFFFFFFFF ? (m_timeoutStart=micros(),false) : (micros()-m_timeoutStart)>100) &&
      readPinCLK() )
#else
  if( (m_flags & P_ATN)!=0 && !readPinATN() && (micros()-m_timeoutStart)>100 && readPinCLK() )
#endif
    {
      // we are under ATN, have waited 100us and the host has released CLK
      handleATNSequence();

      if( (m_flags & P_LISTENING)!=0 )
        {
          // a device is supposed to listen, check if it can accept data
          // (meanwhile allow atnRequest to be called in interrupt)
          m_inTask = false;
          m_currentDevice->task();
          bool canWrite = (m_currentDevice->canWrite()!=0);
          m_inTask = true;

          if( (m_flags & P_ATN)==0 && !canWrite )
            {
              // device can't accept data => signal error by releasing DATA line
              writePinDATA(HIGH);
              m_flags |= P_DONE;
            }
        }
    }
  else if( (m_flags & P_ATN)!=0 && readPinATN() )
    {
      // host has released ATN
      m_flags &= ~P_ATN;

      // ATN was released before handleATNSequence() ran, i.e. no address byte
      // was received (e.g. ATN glitched low while the host powers up, or we
      // missed the sequence entirely). atnRequest() pulled DATA low ("I am
      // here") and disabled the hardware ATN->DATA path; if no transaction is
      // in progress nothing else will release DATA, leaving it latched low on
      // an idle bus and wedging hosts that poll for bus-free before starting
      // a transfer => restore the idle bus state
      if( (m_flags & (P_LISTENING|P_TALKING))==0 )
        {
          writePinCLK(HIGH);
          writePinDATA(HIGH);
        }

      // allow ATN to pull DATA low in hardware
      writePinCTRL(LOW);
    }

  // ------------------ fast-load protocol handling -------------------

  handleFastLoadProtocols();

  // ------------------ receiving data -------------------

  if( (m_flags & (P_ATN|P_LISTENING|P_DONE))==P_LISTENING && (m_currentDevice!=NULL) )
    {
     // we are not under ATN, are in "listening" mode and not done with the transaction

      // check if we can write (also gives devices a chance to
      // execute time-consuming tasks while bus master waits for ready-for-data)
      m_inTask = false;
      int8_t numData = m_currentDevice->canWrite();
      m_inTask = true;

      if( m_flags & P_ATN )
        { 
         // a falling edge on ATN happened while we were stuck in "canWrite"
        }
#ifdef IEC_FP_JIFFY
      else if( (m_currentDevice->m_flFlags & S_JIFFY_DETECTED)!=0 && numData>=0 )
        {
          // receiving under JiffyDOS protocol
          if( !receiveJiffyByte(numData>0) )
            {
              // receive failed => release DATA 
              // and stop listening.  This will signal
              // an error condition to the sender
              writePinDATA(HIGH);
              m_flags |= P_DONE;
            }
          }
#endif
#ifdef IEC_FP_DOLPHIN
      else if( (m_currentDevice->m_flFlags & S_DOLPHIN_DETECTED)!=0 && numData>=0 )
        {
          // receiving under DolphinDOS protocol
          if( !readPinCLK() )
            { /* CLK is still low => sender is not ready yet */ }
          else if( !receiveDolphinByte(numData>0) )
            {
              // receive failed => release DATA 
              // and stop listening.  This will signal
              // an error condition to the sender
              writePinDATA(HIGH);
              m_flags |= P_DONE;
            }
        }
#endif
#ifdef IEC_FP_SPEEDDOS
      else if( (m_currentDevice->m_flFlags & S_SPEEDDOS_DETECTED)!=0 && numData>=0 )
        {
          // receiving under SpeedDos protocol
          if( !readPinCLK() )
            { /* CLK is still low => sender is not ready yet */ }
          else if( !receiveSpeedDosByte(numData>0) )
            {
              // receive failed => release DATA 
              // and stop listening.  This will signal
              // an error condition to the sender
              writePinDATA(HIGH);
              m_flags |= P_DONE;
            }
        }
#endif
      else if( numData>=0 && readPinCLK() )
        {
          // canWrite() result was non-negative
          // CLK high signals sender is ready to transmit
          if( !receiveIECByte(numData>0) )
            {
              // receive failed => transaction is done
              m_flags |= P_DONE;
            }
        }
    }

  // ------------------ transmitting data -------------------

  if( (m_flags & (P_ATN|P_TALKING|P_DONE))==P_TALKING && (m_currentDevice!=NULL) )
  {
    // we are not under ATN, are in "talking" mode and not done with the transaction

#ifdef IEC_FP_JIFFY
    if( (m_currentDevice->m_flFlags & S_JIFFY_BLOCK)!=0 )
      {
        // JiffyDOS block transfer mode
        m_inTask = false;
        uint8_t numData = m_currentDevice->read(m_buffer, m_bufferSize);
        m_inTask = true;

        // delay to make sure receiver sees our CLK LOW and enters "new data block" state.
        // If a possible VIC "bad line" occurs right after reading bits 6+7 it may take
        // the receiver up to 160us after reading bits 6+7 (at FB71) to checking for CLK low (at FB54).
        // If we make it back into transmitJiffyBlock() during that time period
        // then we may already set CLK HIGH again before receiver sees the CLK LOW, 
        // preventing the receiver from going into "new data block" state
        while( (micros()-m_timeoutStart)<175 );

        if( (m_flags & P_ATN) || !readPinATN() || !transmitJiffyBlock(m_buffer, numData) )
          {
            // either a transmission error, no more data to send or falling edge on ATN
            m_flags |= P_DONE;
          }
        else
          {
            // remember time when previous transmission finished
            m_timeoutStart = micros();
          }
      }
    else
#endif
      {
        // check if we can read (also gives devices a chance to
        // execute time-consuming tasks while bus master waits for ready-to-send)
        m_inTask = false;
        int8_t numData = m_currentDevice->canRead();
        m_inTask = true;

        if( m_flags & P_ATN )
          {
            // a falling edge on ATN happened while we were stuck in "canRead"
          }
        else if( (micros()-m_timeoutStart)<m_timeoutDuration || numData<0 )
          {
            // either timeout not yet met or canRead() returned a negative value => do nothing
          }
#ifdef IEC_FP_JIFFY
        else if( (m_currentDevice->m_flFlags & S_JIFFY_DETECTED)!=0 )
          {
            // JiffyDOS byte-by-byte transfer mode
            if( !transmitJiffyByte(numData) )
              {
                // either a transmission error, no more data to send or falling edge on ATN
                m_flags |= P_DONE;
              }
          }
#endif
#ifdef IEC_FP_DOLPHIN
        else if( (m_currentDevice->m_flFlags & S_DOLPHIN_DETECTED)!=0 )
          {
            // DolphinDOS byte-by-byte transfer mode
            if( !transmitDolphinByte(numData) )
              {
                // either a transmission error, no more data to send or falling edge on ATN
                writePinCLK(HIGH);
                m_flags |= P_DONE;
              }
          }
#endif
#ifdef IEC_FP_SPEEDDOS
        else if( (m_currentDevice->m_flFlags & S_SPEEDDOS_DETECTED)!=0 )
          {
            // SpeedDOS byte-by-byte transfer mode
            if( !transmitSpeedDosByte(numData) )
              {
                // either a transmission error, no more data to send or falling edge on ATN
                writePinCLK(HIGH);
                m_flags |= P_DONE;
              }
          }
#endif
        else
          {
            // regular IEC transfer
            if( transmitIECByte(numData) )
              {
                // delay before next transmission ("between bytes time")
                m_timeoutStart = micros();
                m_timeoutDuration = 200;
              }
            else
              {
                // either a transmission error, no more data to send or falling edge on ATN
                m_flags |= P_DONE;
              }
          }
      }
  }

  // allow the interrupt handler to call atnRequest() again
  m_inTask = false;

  // if ATN is low and we don't have P_ATN then we missed the falling edge,
  // make sure to process it before we leave
  if( m_atnInterrupt!=NOT_AN_INTERRUPT && !readPinATN() && !(m_flags & P_ATN) ) { noInterrupts(); atnRequest(); interrupts(); }

  // call "task" function for attached devices
  for(uint8_t i=0; i<m_numDevices; i++)
    m_devices[i]->task(); 
}
