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
// Parallel cable, shared by DolphinDOS and SpeedDOS
//
// These are IECBusHandler member functions living in their own translation
// unit. Everything they need from the bus handler -- the timer macros, the
// inline pin accessors and the state flags -- comes from
// IECBusHandlerInternal.h.
// -----------------------------------------------------------------------------

#include "../IECBusHandlerInternal.h"

#ifdef IEC_SUPPORT_PARALLEL

// ------------------------------------  Parallel cable support routines  ------------------------------------  


#ifdef IEC_SUPPORT_PARALLEL_XRA1405

#if defined(ESP_PLATFORM) && !defined(ARDUINO)
#include "../../../include/esp-idf-spi.h"
#else
#include "SPI.h"
#endif

#pragma GCC push_options
#pragma GCC optimize ("O2")

uint8_t RAMFUNC(IECBusHandler::XRA1405_ReadReg)(uint8_t reg)
{
  startParallelTransaction();
  digitalWriteFastExt(m_pinParallelCS, m_regParallelCS, m_bitParallelCS, LOW);
  uint8_t res = SPI.transfer16((0x40|reg) << 9) & 0xFF;
  digitalWriteFastExt(m_pinParallelCS, m_regParallelCS, m_bitParallelCS, HIGH);
  endParallelTransaction();
  return res;
}

void RAMFUNC(IECBusHandler::XRA1405_WriteReg)(uint8_t reg, uint8_t data)
{
  startParallelTransaction();
  digitalWriteFastExt(m_pinParallelCS, m_regParallelCS, m_bitParallelCS, LOW);
  SPI.transfer16((reg << 9) | data);
  digitalWriteFastExt(m_pinParallelCS, m_regParallelCS, m_bitParallelCS, HIGH);
  endParallelTransaction();
}

#pragma GCC pop_options

#endif

#if defined(ESP_PLATFORM) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))

#include <driver/pulse_cnt.h>
pcnt_unit_handle_t esp32_pulse_count_unit = NULL;
pcnt_channel_handle_t esp32_pulse_count_channel = NULL;
volatile static bool _handshakeReceived = false;
static bool handshakeIRQ(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx)  { _handshakeReceived = true; return false; }
#define PARALLEL_HANDSHAKE_USES_INTERRUPT

#elif !defined(__AVR_ATmega328P__) && !defined(__AVR_ATmega2560__)

volatile static bool _handshakeReceived = false;
static void RAMFUNC(handshakeIRQ)(INTERRUPT_FCN_ARG) { _handshakeReceived = true; }
#define PARALLEL_HANDSHAKE_USES_INTERRUPT

#endif


#ifdef IEC_SUPPORT_PARALLEL_XRA1405

void IECBusHandler::setParallelPins(uint8_t pinHT, uint8_t pinHR, uint8_t pinSCK, uint8_t pinCOPI, uint8_t pinCIPO, uint8_t pinCS)
{
  m_pinParallelHandshakeTransmit = pinHT;
  m_pinParallelHandshakeReceive  = pinHR;
  m_pinParallelCOPI = pinCOPI;
  m_pinParallelCIPO = pinCIPO;
  m_pinParallelSCK  = pinSCK;
  m_pinParallelCS   = pinCS;
}

#else

void IECBusHandler::setParallelPins(uint8_t pinHT, uint8_t pinHR,uint8_t pinD0, uint8_t pinD1, uint8_t pinD2, uint8_t pinD3, uint8_t pinD4, uint8_t pinD5, uint8_t pinD6, uint8_t pinD7)
{
  m_pinParallelHandshakeTransmit = pinHT;
  m_pinParallelHandshakeReceive  = pinHR;
  m_pinParallel[0] = pinD0;
  m_pinParallel[1] = pinD1;
  m_pinParallel[2] = pinD2;
  m_pinParallel[3] = pinD3;
  m_pinParallel[4] = pinD4;
  m_pinParallel[5] = pinD5;
  m_pinParallel[6] = pinD6;
  m_pinParallel[7] = pinD7;
}

#endif

bool IECBusHandler::checkParallelPins()
{
  return (m_bufferSize>=PARALLEL_PREBUFFER_BYTES && 
          !isParallelPin(m_pinATN)   && !isParallelPin(m_pinCLK) && !isParallelPin(m_pinDATA) && 
          !isParallelPin(m_pinRESET) && !isParallelPin(m_pinCTRL) && 
#ifdef IEC_USE_LINE_DRIVERS
          !isParallelPin(m_pinCLKout) && !isParallelPin(m_pinDATAout) &&
#endif
#ifdef IEC_SUPPORT_PARALLEL_XRA1405
          m_pinParallelCS!=0xFF && m_pinParallelSCK!=0xFF && m_pinParallelCOPI!=0xFF && m_pinParallelCIPO!=0xFF &&
#else
          m_pinParallel[0]!=0xFF && m_pinParallel[1]!=0xFF &&
          m_pinParallel[2]!=0xFF && m_pinParallel[3]!=0xFF &&
          m_pinParallel[4]!=0xFF && m_pinParallel[5]!=0xFF &&
          m_pinParallel[6]!=0xFF && m_pinParallel[6]!=0xFF &&
#endif
          m_pinParallelHandshakeTransmit!=0xFF && m_pinParallelHandshakeReceive!=0xFF && 
          digitalPinToInterrupt(m_pinParallelHandshakeReceive)!=NOT_AN_INTERRUPT);
}

bool IECBusHandler::isParallelPin(uint8_t pin)
{
  if( pin==m_pinParallelHandshakeTransmit || pin==m_pinParallelHandshakeReceive )
    return true;

#ifdef IEC_SUPPORT_PARALLEL_XRA1405
  if( pin==m_pinParallelCS || pin==m_pinParallelCOPI || pin==m_pinParallelCIPO || pin==m_pinParallelSCK )
    return true;
#else
  for(int i=0; i<8; i++) 
    if( pin==m_pinParallel[i] )
      return true;
#endif

  return false;
}


void IECBusHandler::enableParallelPins()
{
  uint8_t i = 0;
  for(i=0; i<m_numDevices; i++)
    {
#ifdef IEC_FP_DOLPHIN
      if( m_devices[i]->isFastLoaderEnabled(IEC_FP_DOLPHIN) ) break;
#endif
#ifdef IEC_FP_SPEEDDOS
      if( m_devices[i]->isFastLoaderEnabled(IEC_FP_SPEEDDOS) ) break;
#endif
    }

  if( i<m_numDevices )
    {
      // at least one device uses the parallel cable
#if defined(IOREG_TYPE)
      m_regParallelHandshakeTransmitMode = portModeRegister(digitalPinToPort(m_pinParallelHandshakeTransmit));
      m_bitParallelHandshakeTransmit     = digitalPinToBitMask(m_pinParallelHandshakeTransmit);
#if defined(IEC_SUPPORT_PARALLEL_XRA1405)
      m_regParallelCS = portOutputRegister(digitalPinToPort(m_pinParallelCS));
      m_bitParallelCS = digitalPinToBitMask(m_pinParallelCS);
#else
      for(uint8_t i=0; i<8; i++)
        {
          m_regParallelWrite[i] = portOutputRegister(digitalPinToPort(m_pinParallel[i]));
          m_regParallelRead[i]  = portInputRegister(digitalPinToPort(m_pinParallel[i]));
          m_regParallelMode[i]  = portModeRegister(digitalPinToPort(m_pinParallel[i]));
          m_bitParallel[i]      = digitalPinToBitMask(m_pinParallel[i]);
        }
#endif
#endif
      // initialize handshake transmit (high-Z)
      pinMode(m_pinParallelHandshakeTransmit, OUTPUT);
      digitalWrite(m_pinParallelHandshakeTransmit, LOW);
      pinModeFastExt(m_pinParallelHandshakeTransmit, m_regParallelHandshakeTransmitMode, m_bitParallelHandshakeTransmit, INPUT);
      
      // initialize handshake receive (using INPUT_PULLUP to avoid undefined behavior
      // when parallel cable is not connected)
      pinMode(m_pinParallelHandshakeReceive, INPUT_PULLUP);

      // For 8-bit AVR platforms (Arduino Uno R3, Arduino Mega) the interrupt latency combined
      // with the comparatively slow clock speed leads to reduced performance during load/save
      // For those platforms we do not use the generic interrupt mechanism but instead directly 
      // access the registers dealing with external interrupts.
      // All other platforms are fast enough so we can use the interrupt mechanism without
      // performance issues.
#if defined(__AVR_ATmega328P__)
      // 
      if( m_pinParallelHandshakeReceive==2 )
        {
          EIMSK &= ~bit(INT0);  // disable pin change interrupt
          EICRA &= ~bit(ISC00); EICRA |=  bit(ISC01); // enable falling edge detection
          m_bitParallelhandshakeReceived = bit(INTF0);
        }
      else if( m_pinParallelHandshakeReceive==3 )
        {
          EIMSK &= ~bit(INT1);  // disable pin change interrupt
          EICRA &= ~bit(ISC10); EICRA |=  bit(ISC11); // enable falling edge detection
          m_bitParallelhandshakeReceived = bit(INTF1);
        }
#elif defined(__AVR_ATmega2560__)
      if( m_pinParallelHandshakeReceive==2 )
        {
          EIMSK &= ~bit(INT4); // disable interrupt
          EICRB &= ~bit(ISC40); EICRB |=  bit(ISC41);  // enable falling edge detection
          m_bitParallelhandshakeReceived = bit(INTF4);
        }
      else if( m_pinParallelHandshakeReceive==3 )
        {
          EIMSK &= ~bit(INT5); // disable interrupt
          EICRB &= ~bit(ISC50); EICRB |=  bit(ISC51);  // enable falling edge detection
          m_bitParallelhandshakeReceived = bit(INTF5);
        }
      else if( m_pinParallelHandshakeReceive==18 )
        {
          EIMSK &= ~bit(INT3); // disable interrupt
          EICRA &= ~bit(ISC30); EICRA |=  bit(ISC31);  // enable falling edge detection
          m_bitParallelhandshakeReceived = bit(INTF3);
        }
      else if( m_pinParallelHandshakeReceive==19 )
        {
          EIMSK &= ~bit(INT2); // disable interrupt
          EICRA &= ~bit(ISC20); EICRA |=  bit(ISC21);  // enable falling edge detection
          m_bitParallelhandshakeReceived = bit(INTF2);
        }
      else if( m_pinParallelHandshakeReceive==20 )
        {
          EIMSK &= ~bit(INT1); // disable interrupt
          EICRA &= ~bit(ISC10); EICRA |=  bit(ISC11);  // enable falling edge detection
          m_bitParallelhandshakeReceived = bit(INTF1);
        }
      else if( m_pinParallelHandshakeReceive==21 )
        {
          EIMSK &= ~bit(INT0); // disable interrupt
          EICRA &= ~bit(ISC00); EICRA |=  bit(ISC01);  // enable falling edge detection
          m_bitParallelhandshakeReceived = bit(INTF0);
        }
#elif defined(ESP_PLATFORM) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
      // use pulse counter on handshake receive line (utilizing its glitch filter)
      if( esp32_pulse_count_unit==NULL )
        {
          pcnt_unit_config_t unit_config = {.low_limit = -1, .high_limit = 1, .flags = 0};
          ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &esp32_pulse_count_unit));
          pcnt_glitch_filter_config_t filter_config = { .max_glitch_ns = 250 };
          ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(esp32_pulse_count_unit, &filter_config));
          pcnt_chan_config_t chan_config;
          memset(&chan_config, 0, sizeof(pcnt_chan_config_t));
          chan_config.edge_gpio_num = m_pinParallelHandshakeReceive;
          chan_config.level_gpio_num = -1;
          ESP_ERROR_CHECK(pcnt_new_channel(esp32_pulse_count_unit, &chan_config, &esp32_pulse_count_channel));
          ESP_ERROR_CHECK(pcnt_channel_set_edge_action(esp32_pulse_count_channel, PCNT_CHANNEL_EDGE_ACTION_HOLD, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
          pcnt_event_callbacks_t cbs = { .on_reach = handshakeIRQ };
          ESP_ERROR_CHECK(pcnt_unit_add_watch_point(esp32_pulse_count_unit, 1));
          ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(esp32_pulse_count_unit, &cbs, NULL));
          ESP_ERROR_CHECK(pcnt_unit_enable(esp32_pulse_count_unit));
          ESP_ERROR_CHECK(pcnt_unit_clear_count(esp32_pulse_count_unit));
          ESP_ERROR_CHECK(pcnt_unit_start(esp32_pulse_count_unit));
        }
#elif defined(PARALLEL_HANDSHAKE_USES_INTERRUPT)
      attachInterrupt(digitalPinToInterrupt(m_pinParallelHandshakeReceive), handshakeIRQ, FALLING);
#endif

      // initialize parallel bus pins
#ifdef IEC_SUPPORT_PARALLEL_XRA1405
      digitalWrite(m_pinParallelCS, HIGH);
      pinMode(m_pinParallelCS, OUTPUT);
      digitalWrite(m_pinParallelCS, HIGH);
#if defined(ESP_PLATFORM) && !defined(ARDUINO)
      // for ESP32 ESPIDF, SPI settings are specified in "begin()" instead of "beginTransaction()"
      // (we use 16MHz since at 26MHz we run into timing issues during receive, the frequency
      // does not matter too much since we only send 16 bits of data at a time)
      SPI.begin(m_pinParallelSCK, m_pinParallelCIPO, m_pinParallelCOPI, SPISettings(16000000, MSBFIRST, SPI_MODE0));
#elif defined(ESP_PLATFORM) && defined(ARDUINO)
      // SPI for ESP32 under Arduino requires pin assignments in "begin" call
      SPI.begin(m_pinParallelSCK, m_pinParallelCIPO, m_pinParallelCOPI);
#else
      SPI.begin();
#endif
      setParallelBusModeInput();
      m_inTransaction = 0;
#else
      for(int i=0; i<8; i++) pinMode(m_pinParallel[i], INPUT);
#endif
    }
  else
    {
#if defined(ESP_PLATFORM) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
      // disable pulse counter on handshake receive line
      if( esp32_pulse_count_unit!=NULL )
        {
          pcnt_unit_stop(esp32_pulse_count_unit);
          pcnt_unit_disable(esp32_pulse_count_unit);
          pcnt_del_channel(esp32_pulse_count_channel);
          pcnt_del_unit(esp32_pulse_count_unit);
          esp32_pulse_count_unit = NULL;
          esp32_pulse_count_channel = NULL;
        }
#elif defined(PARALLEL_HANDSHAKE_USES_INTERRUPT)
      detachInterrupt(digitalPinToInterrupt(m_pinParallelHandshakeReceive));
#endif
    }
}


bool RAMFUNC(IECBusHandler::parallelBusHandshakeReceived)()
{
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega2560__)
  // see comment in function enableParallelPins
  if( EIFR & m_bitParallelhandshakeReceived )
    {
      EIFR |= m_bitParallelhandshakeReceived;
      return true;
    }
  else
    return false;
#else
  if( _handshakeReceived )
    {
      _handshakeReceived = false;
      return true;
    }
  else
    return false;
#endif
}


void RAMFUNC(IECBusHandler::parallelBusHandshakeTransmit)()
{
  // Emulate open collector behavior: 
  // - switch pin to INPUT  mode (high-Z output) for true
  // - switch pun to OUTPUT mode (LOW output) for false
  pinModeFastExt(m_pinParallelHandshakeTransmit, m_regParallelHandshakeTransmitMode, m_bitParallelHandshakeTransmit, OUTPUT);
  delayMicrosecondsISafe(1);
  pinModeFastExt(m_pinParallelHandshakeTransmit, m_regParallelHandshakeTransmitMode, m_bitParallelHandshakeTransmit, INPUT);
}


void RAMFUNC(IECBusHandler::startParallelTransaction)()
{
#ifdef IEC_SUPPORT_PARALLEL_XRA1405
  if( m_inTransaction==0 )
    {
#if defined(ESP_PLATFORM) && !defined(ARDUINO)
      // for ESPIDF, SPI settings are specified in "begin()" instead of "beginTransaction()"
      SPI.beginTransaction();
#else
      SPI.beginTransaction(SPISettings(16000000, MSBFIRST, SPI_MODE0));
#endif
    }

  m_inTransaction++;
#endif
}


void RAMFUNC(IECBusHandler::endParallelTransaction)()
{
#ifdef IEC_SUPPORT_PARALLEL_XRA1405
  if( m_inTransaction==1 ) SPI.endTransaction();
  if( m_inTransaction>0  ) m_inTransaction--;
#endif
}


#pragma GCC push_options
#pragma GCC optimize ("O2")
uint8_t RAMFUNC(IECBusHandler::readParallelData)()
{
  uint8_t res = 0;
#ifdef IEC_SUPPORT_PARALLEL_XRA1405
  res = XRA1405_ReadReg(0x00); // GSR1, GPIO State Register for P0-P7
#else
  // loop unrolled for performance
  if( digitalReadFastExt(m_pinParallel[0], m_regParallelRead[0], m_bitParallel[0]) ) res |= 0x01;
  if( digitalReadFastExt(m_pinParallel[1], m_regParallelRead[1], m_bitParallel[1]) ) res |= 0x02;
  if( digitalReadFastExt(m_pinParallel[2], m_regParallelRead[2], m_bitParallel[2]) ) res |= 0x04;
  if( digitalReadFastExt(m_pinParallel[3], m_regParallelRead[3], m_bitParallel[3]) ) res |= 0x08;
  if( digitalReadFastExt(m_pinParallel[4], m_regParallelRead[4], m_bitParallel[4]) ) res |= 0x10;
  if( digitalReadFastExt(m_pinParallel[5], m_regParallelRead[5], m_bitParallel[5]) ) res |= 0x20;
  if( digitalReadFastExt(m_pinParallel[6], m_regParallelRead[6], m_bitParallel[6]) ) res |= 0x40;
  if( digitalReadFastExt(m_pinParallel[7], m_regParallelRead[7], m_bitParallel[7]) ) res |= 0x80;
#endif
  return res;
}


void RAMFUNC(IECBusHandler::writeParallelData)(uint8_t data)
{
#ifdef IEC_SUPPORT_PARALLEL_XRA1405
  XRA1405_WriteReg(0x02, data); // OCR1, GPIO Output Control Register for P0-P7
#else
  // loop unrolled for performance
  digitalWriteFastExt(m_pinParallel[0], m_regParallelWrite[0], m_bitParallel[0], data & 0x01);
  digitalWriteFastExt(m_pinParallel[1], m_regParallelWrite[1], m_bitParallel[1], data & 0x02);
  digitalWriteFastExt(m_pinParallel[2], m_regParallelWrite[2], m_bitParallel[2], data & 0x04);
  digitalWriteFastExt(m_pinParallel[3], m_regParallelWrite[3], m_bitParallel[3], data & 0x08);
  digitalWriteFastExt(m_pinParallel[4], m_regParallelWrite[4], m_bitParallel[4], data & 0x10);
  digitalWriteFastExt(m_pinParallel[5], m_regParallelWrite[5], m_bitParallel[5], data & 0x20);
  digitalWriteFastExt(m_pinParallel[6], m_regParallelWrite[6], m_bitParallel[6], data & 0x40);
  digitalWriteFastExt(m_pinParallel[7], m_regParallelWrite[7], m_bitParallel[7], data & 0x80);
#endif
}


void RAMFUNC(IECBusHandler::setParallelBusModeInput)()
{
#ifdef IEC_SUPPORT_PARALLEL_XRA1405
  XRA1405_WriteReg(0x06, 0xFF); // GCR1, GPIO Configuration Register for P0-P7
#else
  // set parallel bus data pins to input mode
  for(int i=0; i<8; i++) 
    pinModeFastExt(m_pinParallel[i], m_regParallelMode[i], m_bitParallel[i], INPUT);
#endif
}


void RAMFUNC(IECBusHandler::setParallelBusModeOutput)()
{
#ifdef IEC_SUPPORT_PARALLEL_XRA1405
  XRA1405_WriteReg(0x06, 0x00); // GCR1, GPIO Configuration Register for P0-P7
#else
  // set parallel bus data pins to output mode
  for(int i=0; i<8; i++) 
    pinModeFastExt(m_pinParallel[i], m_regParallelMode[i], m_bitParallel[i], OUTPUT);
#endif
}
#pragma GCC pop_options


bool RAMFUNC(IECBusHandler::waitParallelBusHandshakeReceived)()
{
  uint32_t timeout = micros()+5000;
  while( !parallelBusHandshakeReceived() )
    if( !readPinATN() || micros()>timeout )
      return false;

  return true;
}


#ifdef PARALLEL_HANDSHAKE_USES_INTERRUPT

#pragma GCC push_options
#pragma GCC optimize ("O2")

bool RAMFUNC(IECBusHandler::waitParallelBusHandshakeReceivedISafe)(bool exitOnCLKchange)
{
  // Version of waitParallelBusHandshakeReceived() that can be called with interrupts disabled.
  // This is for architectures where we usually would use a pin-change interrupt to detect
  // the pulse on the incoming handshake signal (but can't if interrupts are disabled)

#if defined(IOREG_TYPE)
  volatile const IOREG_TYPE *regHandshakeReceive = portInputRegister(digitalPinToPort(m_pinParallelHandshakeReceive));
  volatile IOREG_TYPE bitHandshakeReceive = digitalPinToBitMask(m_pinParallelHandshakeReceive);
#endif

  bool atnVal = digitalReadFastExtIEC(m_pinATN, m_regATNread, m_bitATN);
  bool clkVal = digitalReadFastExtIEC(m_pinCLK, m_regCLKread, m_bitCLK);

  // wait for handshake signal going LOW (until either ATN or CLK change)
  while( true ) 
    {
      if( !digitalReadFastExt(m_pinParallelHandshakeReceive, regHandshakeReceive, bitHandshakeReceive) ) return true;
      if( atnVal!=digitalReadFastExtIEC(m_pinATN, m_regATNread, m_bitATN) ) return false;
      if( !digitalReadFastExt(m_pinParallelHandshakeReceive, regHandshakeReceive, bitHandshakeReceive) ) return true;
      if( exitOnCLKchange && clkVal!=digitalReadFastExtIEC(m_pinCLK, m_regCLKread, m_bitCLK) ) return false;
      if( !digitalReadFastExt(m_pinParallelHandshakeReceive, regHandshakeReceive, bitHandshakeReceive) ) return true;
    }
}

#pragma GCC pop_options

#else

bool RAMFUNC(IECBusHandler::waitParallelBusHandshakeReceivedISafe)(bool exitOnCLKchange)
{
  // clear any previous handshakes
  parallelBusHandshakeReceived();

  bool atnVal = readPinATN();
  bool clkVal = readPinCLK();

  // wait for handshake
  while( atnVal==readPinATN() && (!exitOnCLKchange || clkVal==readPinCLK()) )
    if( parallelBusHandshakeReceived() )
      return true;
  
  return false;
}


#endif

#endif // IEC_SUPPORT_PARALLEL
