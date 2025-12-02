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

#include "IECBusHandler.h"
#include "IECDevice.h"

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

//#define JDEBUG

// ---------------- Arduino 8-bit ATMega (UNO R3/Mega/Mini/Micro/Leonardo...)

#if defined(__AVR__)

#if defined(__AVR_ATmega32U4__)
// Atmega32U4 does not have a second 8-bit timer (first one is used by Arduino millis())
// => use lower 8 bit of 16-bit timer 1
#define timer_init()         { TCCR1A=0; TCCR1B=0; }
#define timer_reset()        TCNT1L=0
#define timer_start()        TCCR1B |= bit(CS11)
#define timer_stop()         TCCR1B &= ~bit(CS11)
#define timer_less_than(us)  (TCNT1L < ((uint8_t) (2*(us))))
#define timer_not_equal(us)  (TCNT1L != uint8_t(uint32_t(2*(us))))
#define timer_wait_until(us) while( timer_not_equal(us) )
#else
// use 8-bit timer 2 with /8 prescaler
#define timer_init()         { TCCR2A=0; TCCR2B=0; }
#define timer_reset()        TCNT2=0
#define timer_start()        TCCR2B |= bit(CS21)
#define timer_stop()         TCCR2B &= ~bit(CS21)
#define timer_less_than(us)  (TCNT2 < ((uint8_t) (2*(us))))
#define timer_not_equal(us)  (TCNT2 != uint8_t(uint16_t(2*(us))))
#define timer_wait_until(us) while( timer_not_equal(us) )
#endif

//NOTE: Must disable IEC_FP_DOLPHIN/IEC_FP_SPEEDDOS, otherwise no pins left for debugging (except Mega)
#ifdef JDEBUG
#define JDEBUGI() DDRD |= 0x80; PORTD &= ~0x80 // PD7 = pin digital 7
#define JDEBUG0() PORTD&=~0x80
#define JDEBUG1() PORTD|=0x80
#endif

// ---------------- Arduino Uno R4

#elif defined(ARDUINO_UNOR4_MINIMA) || defined(ARDUINO_UNOR4_WIFI)
#ifndef ARDUINO_UNOR4
#define ARDUINO_UNOR4
#endif

// NOTE: this assumes the AGT timer is running at the (Arduino default) 3MHz rate
//       and rolling over after 3000 ticks 
static unsigned long timer_start_ticks;
static uint16_t timer_ticks_diff(uint16_t t0, uint16_t t1) { return ((t0 < t1) ? 3000 + t0 : t0) - t1; }
#define timer_init()         while(0)
#define timer_reset()        timer_start_ticks = R_AGT0->AGT
#define timer_start()        timer_start_ticks = R_AGT0->AGT
#define timer_stop()         while(0)
#define timer_less_than(us)  (timer_ticks_diff(timer_start_ticks, R_AGT0->AGT) < ((int) ((us)*3)))
#define timer_wait_until(us) while( timer_less_than(us) )

#ifdef JDEBUG
#define JDEBUGI() pinMode(1, OUTPUT)
#define JDEBUG0() R_PORT3->PODR &= ~bit(2);
#define JDEBUG1() R_PORT3->PODR |=  bit(2);
#endif

// ---------------- Arduino Due

#elif defined(__SAM3X8E__)

#define portModeRegister(port) 0

static unsigned long timer_start_ticks;
static uint32_t timer_ticks_diff(uint32_t t0, uint32_t t1) { return ((t0 < t1) ? 84000 + t0 : t0) - t1; }
#define timer_init()         while(0)
#define timer_reset()        timer_start_ticks = SysTick->VAL;
#define timer_start()        timer_start_ticks = SysTick->VAL;
#define timer_stop()         while(0)
#define timer_less_than(us)  (timer_ticks_diff(timer_start_ticks, SysTick->VAL) < ((int) ((us)*84)))
#define timer_wait_until(us) while( timer_less_than(us) )

#ifdef JDEBUG
#define JDEBUGI() pinMode(2, OUTPUT)
#define JDEBUG0() REG_PIOB_CODR = 1<<25
#define JDEBUG1() REG_PIOB_SODR = 1<<25
#endif

// ---------------- Raspberry Pi Pico

#elif defined(ARDUINO_ARCH_RP2040)

// note: micros() call on MBED core is SLOW - using time_us_32() instead
static unsigned long timer_start_us;
#define timer_init()         while(0)
#define timer_reset()        timer_start_us = time_us_32()
#define timer_start()        timer_start_us = time_us_32()
#define timer_stop()         while(0)
#define timer_less_than(us)  ((time_us_32()-timer_start_us) < ((int) ((us)+0.5)))
#define timer_wait_until(us) while( timer_less_than(us) )

#ifdef JDEBUG
#define JDEBUGI() pinMode(28, OUTPUT)
#define JDEBUG0() gpio_put(28, 0)
#define JDEBUG1() gpio_put(28, 1)
#endif

// ---------------- ESP32

#elif defined(ESP_PLATFORM)

// using esp_cpu_get_cycle_count() instead of esp_timer_get_time() works much
// better in timing-critical code since it translates into a single CPU instruction
// instead of a library call that may introduce short delays due to flash ROM access
// conflicts with the other core.
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp_clk.h"
#define esp_cpu_cycle_count_t uint32_t
#define esp_cpu_get_cycle_count esp_cpu_get_ccount
#define esp_rom_get_cpu_ticks_per_us() (esp_clk_cpu_freq()/1000000)
#endif
static DRAM_ATTR esp_cpu_cycle_count_t timer_start_cycles, timer_cycles_per_us_div2;
#define timer_init()         timer_cycles_per_us_div2 = esp_rom_get_cpu_ticks_per_us()/2;
#define timer_reset()        timer_start_cycles = esp_cpu_get_cycle_count()
#define timer_start()        timer_start_cycles = esp_cpu_get_cycle_count()
#define timer_stop()         while(0)
#define timer_less_than(us)  ((esp_cpu_get_cycle_count()-timer_start_cycles) < ((uint32_t((us)*2)*timer_cycles_per_us_div2)))
#define timer_wait_until(us) \
  { esp_cpu_cycle_count_t to = uint32_t((us)*2) * timer_cycles_per_us_div2; \
    while( (esp_cpu_get_cycle_count()-timer_start_cycles) < to ); }

// interval in which we need to feed the interrupt WDT to stop it from re-booting the system
#define IWDT_FEED_TIME ((CONFIG_ESP_INT_WDT_TIMEOUT_MS-10)*1000)

// keep track whether interrupts are enabled or not (see comments in waitPinDATA/waitPinCLK)
static bool haveInterrupts = true;
#undef noInterrupts
#undef interrupts
#define noInterrupts() { portDISABLE_INTERRUPTS(); haveInterrupts = false; }
#define interrupts()   { haveInterrupts = true; portENABLE_INTERRUPTS(); }

#if defined(JDEBUG)
#define JDEBUGI() pinMode(12, OUTPUT)
#define JDEBUG0() GPIO.out_w1tc = bit(12)
#define JDEBUG1() GPIO.out_w1ts = bit(12)
#endif

// ---------------- other (32-bit) platforms

#else

static unsigned long timer_start_us;
#define timer_init()         while(0)
#define timer_reset()        timer_start_us = micros()
#define timer_start()        timer_start_us = micros()
#define timer_stop()         while(0)
#define timer_less_than(us)  ((micros()-timer_start_us) < ((int) ((us)+0.5)))
#define timer_wait_until(us) while( timer_less_than(us) )

#if defined(JDEBUG) && defined(ESP_PLATFORM)
#define JDEBUGI() pinMode(26, OUTPUT)
#define JDEBUG0() GPIO.out_w1tc = bit(26)
#define JDEBUG1() GPIO.out_w1ts = bit(26)
#endif

#endif

#ifndef JDEBUG
#define JDEBUGI()
#define JDEBUG0()
#define JDEBUG1()
#endif

#if defined(__SAM3X8E__)
// Arduino Due
#define pinModeFastExt(pin, reg, bit, dir)    { if( (dir)==OUTPUT ) digitalPinToPort(pin)->PIO_OER |= bit; else digitalPinToPort(pin)->PIO_ODR |= bit; }
#define digitalReadFastExt(pin, reg, bit)     (*(reg) & (bit))
#define digitalWriteFastExt(pin, reg, bit, v) { if( v ) *(reg)|=(bit); else (*reg)&=~(bit); }
#elif defined(ARDUINO_ARCH_RP2040)
// Raspberry Pi Pico
#define pinModeFastExt(pin, reg, bit, dir)    gpio_set_dir(pin, (dir)==OUTPUT)
#define digitalReadFastExt(pin, reg, bit)     gpio_get(pin)
#define digitalWriteFastExt(pin, reg, bit, v) gpio_put(pin, v)
#define RAMFUNC(name) __not_in_flash_func(name)
#elif defined(__AVR__) || defined(ARDUINO_UNOR4)
// Arduino 8-bit (Uno R3/Mega/...)
#define pinModeFastExt(pin, reg, bit, dir)    { if( (dir)==OUTPUT ) *(reg)|=(bit); else *(reg)&=~(bit); }
#define digitalReadFastExt(pin, reg, bit)     (*(reg) & (bit))
#define digitalWriteFastExt(pin, reg, bit, v) { if( v ) *(reg)|=(bit); else (*reg)&=~(bit); }
#elif defined(ESP_PLATFORM)
// ESP32
#define pinModeFastExt(pin, reg, bit, dir)    { if( (dir)==OUTPUT ) *(reg)|=(bit); else *(reg)&=~(bit); }
#define digitalReadFastExt(pin, reg, bit)     (*(reg) & (bit))
#define digitalWriteFastExt(pin, reg, bit, v) { if( v ) *(reg)|=(bit); else (*reg)&=~(bit); }
#define RAMFUNC(name) IRAM_ATTR name
#else
#warning "No fast digital I/O macros defined for this platform - code will likely run too slow"
#define pinModeFastExt(pin, reg, bit, dir)    pinMode(pin, dir)
#define digitalReadFastExt(pin, reg, bit)     digitalRead(pin)
#define digitalWriteFastExt(pin, reg, bit, v) digitalWrite(pin, v)
#endif

// For some platforms (ESP32, PiPico) we need the code to reside in SRAM rather than flash
// because flash access can be slow in some cases, disrupting protocol timing. If so, the
// RAMFUNC() macro gets defined above, for other platforms we define it (empty) here
#ifndef RAMFUNC
#define RAMFUNC(name) name
#endif

// delayMicroseconds on some platforms does not work if called when interrupts are disabled
// => define a version that does work on all supported platforms
static void RAMFUNC(delayMicrosecondsISafe)(uint16_t t)
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


// define digitalReadFastExtIEC according to whether IEC lines are inverted or not
#if defined(IEC_USE_LINE_DRIVERS) && defined(IEC_USE_INVERTED_INPUTS)
#define digitalReadFastExtIEC(pin, reg, bit) (!(digitalReadFastExt(pin, reg, bit)))
#else
#define digitalReadFastExtIEC(pin, reg, bit) (digitalReadFastExt(pin, reg, bit))
#endif

// -----------------------------------------------------------------------------------------

#define P_ATN        0x80
#define P_LISTENING  0x40
#define P_TALKING    0x20
#define P_DONE       0x10
#define P_RESET      0x08

#define S_JIFFY_DETECTED         0x01  // Detected JiffyDos request from host
#define S_JIFFY_BLOCK            0x02  // Detected JiffyDos block transfer request from host
#define S_DOLPHIN_DETECTED       0x04  // Detected DolphinDos request from host
#define S_DOLPHIN_BURST_ENABLED  0x08  // DolphinDos burst mode is enabled
#define S_SPEEDDOS_DETECTED      0x10  // Detected SpeedDos request from host

#define TC_NONE      0
#define TC_DATA_LOW  1
#define TC_DATA_HIGH 2
#define TC_CLK_LOW   3
#define TC_CLK_HIGH  4


IECBusHandler *IECBusHandler::s_bushandler = NULL;

#ifdef IEC_USE_LINE_DRIVERS

void RAMFUNC(IECBusHandler::writePinCLK)(bool v)
{
#ifdef IEC_USE_INVERTED_LINE_DRIVERS
  digitalWriteFastExt(m_pinCLKout, m_regCLKwrite, m_bitCLKout, !v);
#else
  digitalWriteFastExt(m_pinCLKout, m_regCLKwrite, m_bitCLKout, v);
#endif
}

void RAMFUNC(IECBusHandler::writePinDATA)(bool v)
{
#ifdef IEC_USE_INVERTED_LINE_DRIVERS
  digitalWriteFastExt(m_pinDATAout, m_regDATAwrite, m_bitDATAout, !v);
#else
  digitalWriteFastExt(m_pinDATAout, m_regDATAwrite, m_bitDATAout, v);
#endif
}

#else

void RAMFUNC(IECBusHandler::writePinCLK)(bool v)
{
  // Emulate open collector behavior: 
  // - switch pin to INPUT  mode (high-Z output) for true
  // - switch pin to OUTPUT mode (LOW output) for false
  pinModeFastExt(m_pinCLK, m_regCLKmode, m_bitCLK, v ? INPUT : OUTPUT);
}


void RAMFUNC(IECBusHandler::writePinDATA)(bool v)
{
  // Emulate open collector behavior: 
  // - switch pin to INPUT  mode (high-Z output) for true
  // - switch pin to OUTPUT mode (LOW output) for false
  pinModeFastExt(m_pinDATA, m_regDATAmode, m_bitDATA, v ? INPUT : OUTPUT);
}
#endif

void RAMFUNC(IECBusHandler::writePinCTRL)(bool v)
{
  if( m_pinCTRL!=0xFF )
    digitalWrite(m_pinCTRL, v);
}

bool RAMFUNC(IECBusHandler::readPinATN)()
{
  return digitalReadFastExtIEC(m_pinATN, m_regATNread, m_bitATN)!=0;
}


bool RAMFUNC(IECBusHandler::readPinCLK)()
{
  return digitalReadFastExtIEC(m_pinCLK, m_regCLKread, m_bitCLK)!=0;
}


bool RAMFUNC(IECBusHandler::readPinDATA)()
{
  return digitalReadFastExtIEC(m_pinDATA, m_regDATAread, m_bitDATA)!=0;
}


bool RAMFUNC(IECBusHandler::readPinRESET)()
{
  if( m_pinRESET==0xFF ) return true;
  return digitalReadFastExtIEC(m_pinRESET, m_regRESETread, m_bitRESET)!=0;
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
: m_pinParallel{13,14,15,16,17,25,26,27},
  m_pinParallelHandshakeTransmit(4),
  m_pinParallelHandshakeReceive(36)
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
  m_inTask     = false;
  m_flags      = 0xFF; // 0xFF means: begin() has not yet been called
  m_currentDevice = NULL;

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
  if( m_atnInterrupt!=NOT_AN_INTERRUPT && s_bushandler==NULL )
    {
      s_bushandler = this;
#if defined(IEC_USE_LINE_DRIVERS) && defined(IEC_USE_INVERTED_INPUTS)
      attachInterrupt(m_atnInterrupt, atnInterruptFcn, RISING);
#else
      attachInterrupt(m_atnInterrupt, atnInterruptFcn, FALLING);
#endif
    }

  // call begin() function for all attached devices
  for(uint8_t i=0; i<m_numDevices; i++)
    m_devices[i]->begin();
}


bool IECBusHandler::canServeATN() 
{ 
  return (m_pinCTRL!=0xFF) || (m_atnInterrupt != NOT_AN_INTERRUPT); 
}


bool IECBusHandler::inTransaction()
{
  return (m_flags & (P_LISTENING|P_TALKING))!=0;
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


IECDevice *IECBusHandler::findDevice(uint8_t devnr, bool includeInactive)
{
  for(uint8_t i=0; i<m_numDevices; i++)
    if( devnr == m_devices[i]->m_devnr && (includeInactive || m_devices[i]->isActive()) )
      return m_devices[i];

  return NULL;
}


void RAMFUNC(IECBusHandler::atnInterruptFcn)(INTERRUPT_FCN_ARG)
{ 
  if( s_bushandler!=NULL && !s_bushandler->m_inTask & ((s_bushandler->m_flags & P_ATN)==0) )
    s_bushandler->atnRequest();
}


#if defined(IEC_SUPPORT_FASTLOAD) && !defined(IEC_DEFAULT_FASTLOAD_BUFFER_SIZE)
void IECBusHandler::setBuffer(uint8_t *buffer, uint8_t bufferSize)
{
  m_buffer     = bufferSize>0 ? buffer : NULL;
  m_bufferSize = bufferSize>254 ? 254 : bufferSize;
}
#endif

#ifdef IEC_SUPPORT_PARALLEL

// ------------------------------------  Parallel cable support routines  ------------------------------------  

#define PARALLEL_PREBUFFER_BYTES 2

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


// ------------------------------------  Generic Fast-Load support routines  ------------------------------------  


uint8_t IECBusHandler::getSupportedFastLoaders()
{
  uint8_t mask = 0;
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
#ifdef IEC_FP_DOLPHIN
  mask |= bit(IEC_FP_DOLPHIN);
#endif
#ifdef IEC_FP_SPEEDDOS
  mask |= bit(IEC_FP_SPEEDDOS);
#endif
  return mask;
}

bool IECBusHandler::isFastLoaderSupported(uint8_t loader)
{
  return (loader<=7) && (bit(loader) & getSupportedFastLoaders())!=0;
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
      
    default:
      break;
    }
}


#ifdef IEC_FP_JIFFY

// ------------------------------------  JiffyDos support routines  ------------------------------------  

bool RAMFUNC(IECBusHandler::receiveJiffyByte)(bool canWriteOk)
{
  uint8_t data = 0;
  JDEBUG1();
  timer_init();
  timer_reset();

  noInterrupts(); 

  // signal "ready" by releasing DATA
  writePinDATA(HIGH);

  // wait (indefinitely) for either CLK high ("ready-to-send") or ATN low
  // NOTE: this must be in a blocking loop since the sender starts transmitting
  // the byte immediately after setting CLK high. If we exit the "task" function then
  // we may not get back here in time to receive.
  while( !digitalReadFastExtIEC(m_pinCLK, m_regCLKread, m_bitCLK) && digitalReadFastExtIEC(m_pinATN, m_regATNread, m_bitATN) )
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

  // start timer (on AVR, lag from CLK high to timer start is between 700...1700ns)
  timer_start();
  JDEBUG0();

  // abort if ATN low
  if( !readPinATN() )
    { interrupts(); return false; }

  // bits 4+5 are set by sender 11 cycles after CLK HIGH (FC51)
  // wait until 14us after CLK
  timer_wait_until(14);
  
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(4);
  if( !readPinDATA() ) data |= bit(5);
  JDEBUG0();

  // bits 6+7 are set by sender 24 cycles after CLK HIGH (FC5A)
  // wait until 27us after CLK
  timer_wait_until(27);
  
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(6);
  if( !readPinDATA() ) data |= bit(7);
  JDEBUG0();

  // bits 3+1 are set by sender 35 cycles after CLK HIGH (FC62)
  // wait until 38us after CLK
  timer_wait_until(38);

  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(3);
  if( !readPinDATA() ) data |= bit(1);
  JDEBUG0();

  // bits 2+0 are set by sender 48 cycles after CLK HIGH (FC6B)
  // wait until 51us after CLK
  timer_wait_until(51);

  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(2);
  if( !readPinDATA() ) data |= bit(0);
  JDEBUG0();

  // sender sets EOI status 61 cycles after CLK HIGH (FC76)
  // wait until 64us after CLK
  timer_wait_until(64);

  // if CLK is high at this point then the sender is signaling EOI
  JDEBUG1();
  bool eoi = readPinCLK();

  // acknowledge receipt
  writePinDATA(LOW);

  // sender reads acknowledgement 80 cycles after CLK HIGH (FC82)
  // wait until 83us after CLK
  timer_wait_until(83);

  JDEBUG0();

  interrupts();

  if( canWriteOk )
    {
      // pass received data on to the device
      m_currentDevice->write(data, eoi);
    }
  else
    {
      // canWrite() reported an error
      return false;
    }
  
  return true;
}


bool RAMFUNC(IECBusHandler::transmitJiffyByte)(uint8_t numData)
{
  uint8_t data = numData>0 ? m_currentDevice->peek() : 0;

  JDEBUG1();
  timer_init();
  timer_reset();

  noInterrupts();

  // signal "READY" by releasing CLK
  writePinCLK(HIGH);
  
  // wait (indefinitely) for either DATA high ("ready-to-receive", FBCB) or ATN low
  // NOTE: this must be in a blocking loop since the receiver receives the data
  // immediately after setting DATA high. If we exit the "task" function then
  // we may not get back here in time to transmit.
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

  // start timer (on AVR, lag from DATA high to timer start is between 700...1700ns)
  timer_start();
  JDEBUG0();

  // abort if ATN low
  if( !readPinATN() )
    { interrupts(); return false; }

  writePinCLK(data & bit(0));
  writePinDATA(data & bit(1));
  JDEBUG1();
  // bits 0+1 are read by receiver 16 cycles after DATA HIGH (FBD5)

  // wait until 16.5 us after DATA
  timer_wait_until(16.5);
  
  JDEBUG0();
  writePinCLK(data & bit(2));
  writePinDATA(data & bit(3));
  JDEBUG1();
  // bits 2+3 are read by receiver 26 cycles after DATA HIGH (FBDB)

  // wait until 27.5 us after DATA
  timer_wait_until(27.5);

  JDEBUG0();
  writePinCLK(data & bit(4));
  writePinDATA(data & bit(5));
  JDEBUG1();
  // bits 4+5 are read by receiver 37 cycles after DATA HIGH (FBE2)

  // wait until 39 us after DATA
  timer_wait_until(39);

  JDEBUG0();
  writePinCLK(data & bit(6));
  writePinDATA(data & bit(7));
  JDEBUG1();
  // bits 6+7 are read by receiver 48 cycles after DATA HIGH (FBE9)

  // wait until 50 us after DATA
  timer_wait_until(50);
  JDEBUG0();
      
  // numData:
  //   0: no data was available to read (error condition, discard this byte)
  //   1: this was the last byte of data
  //  >1: more data is available after this
  if( numData>1 )
    {
      // CLK=LOW  and DATA=HIGH means "at least one more byte"
      writePinCLK(LOW);
      writePinDATA(HIGH);
    }
  else
    {
      // CLK=HIGH and DATA=LOW  means EOI (this was the last byte)
      // CLK=HIGH and DATA=HIGH means "error"
      writePinCLK(HIGH);
      writePinDATA(numData==0);
    }

  // EOI/error status is read by receiver 59 cycles after DATA HIGH (FBEF)
  // receiver sets DATA low 63 cycles after initial DATA HIGH (FBF2)
  timer_wait_until(60);

  // receiver signals "done" by pulling DATA low (FBF2)
  JDEBUG1();
  if( !waitPinDATA(LOW) ) { interrupts(); return false; }
  JDEBUG0();

  // at this point make sure CLK/DATA are in "busy" configuration
  // even if we signaled EOI or error before
  writePinCLK(LOW);
  writePinDATA(HIGH);

  interrupts();

  if( numData>0 )
    {
      // success => discard transmitted byte (was previously read via peek())
      m_currentDevice->read();
      return true;
    }
  else
    return false;
}


bool RAMFUNC(IECBusHandler::transmitJiffyBlock)(uint8_t *buffer, uint8_t numBytes)
{
  JDEBUG1();
  timer_init();

  // wait (indefinitely) until receiver is not holding DATA low anymore (FB07)
  // NOTE: this must be in a blocking loop since the receiver starts counting
  // up the EOI timeout immediately after setting DATA HIGH. If we had exited the 
  // "task" function then it might be more than 200us until we get back here
  // to pull CLK low and the receiver will interpret that delay as EOI.
  while( !readPinDATA() )
    if( !readPinATN() )
      { JDEBUG0(); return false; }

  // receiver will be in "new data block" state at this point,
  // waiting for us (FB0C) to release CLK
  if( numBytes==0 )
    {
      // nothing to send => signal EOI by keeping DATA high
      // and pulsing CLK high-low
      writePinDATA(HIGH);
      writePinCLK(HIGH);
      if( !waitTimeout(100) ) return false;
      writePinCLK(LOW);
      if( !waitTimeout(100) ) return false;
      JDEBUG0(); 
      return false;
    }

  // signal "ready to send" by pulling DATA low and releasing CLK
  writePinDATA(LOW);
  writePinCLK(HIGH);

  // delay to make sure receiver has seen DATA=LOW - even though receiver 
  // is in a tight loop (at FB0C), a VIC "bad line" may steal 40-50us.
  if( !waitTimeout(60) ) return false;

  noInterrupts();

  for(uint8_t i=0; i<numBytes; i++)
    {
      uint8_t data = buffer[i];

      // release DATA
      writePinDATA(HIGH);

      // stop and reset timer
      timer_stop();
      timer_reset();

      // signal READY by releasing CLK
      writePinCLK(HIGH);

      // make sure DATA has settled on HIGH
      // (receiver takes at least 19 cycles between seeing DATA HIGH [at FB3E] and setting DATA LOW [at FB51]
      // so waiting a couple microseconds will not hurt transfer performance)
      delayMicrosecondsISafe(2);

      // wait (indefinitely) for either DATA low (FB51) or ATN low
      // NOTE: this must be in a blocking loop since the receiver receives the data
      // immediately after setting DATA high. If we exit the "task" function then
      // we may not get back here in time to transmit.
      while( digitalReadFastExtIEC(m_pinDATA, m_regDATAread, m_bitDATA) && digitalReadFastExtIEC(m_pinATN, m_regATNread, m_bitATN) )
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

      // start timer (on AVR, lag from DATA low to timer start is between 700...1700ns)
      timer_start();
      JDEBUG0();
      
      // abort if ATN low
      if( !readPinATN() )
        { interrupts(); return false; }

      // receiver expects to see CLK high at 4 cycles after DATA LOW (FB54)
      // wait until 6 us after DATA LOW
      timer_wait_until(6);

      JDEBUG0();
      writePinCLK(data & bit(0));
      writePinDATA(data & bit(1));
      JDEBUG1();
      // bits 0+1 are read by receiver 16 cycles after DATA LOW (FB5D)

      // wait until 17 us after DATA LOW
      timer_wait_until(17);
  
      JDEBUG0();
      writePinCLK(data & bit(2));
      writePinDATA(data & bit(3));
      JDEBUG1();
      // bits 2+3 are read by receiver 26 cycles after DATA LOW (FB63)

      // wait until 27 us after DATA LOW
      timer_wait_until(27);

      JDEBUG0();
      writePinCLK(data & bit(4));
      writePinDATA(data & bit(5));
      JDEBUG1();
      // bits 4+5 are read by receiver 37 cycles after DATA LOW (FB6A)

      // wait until 39 us after DATA LOW
      timer_wait_until(39);

      JDEBUG0();
      writePinCLK(data & bit(6));
      writePinDATA(data & bit(7));
      JDEBUG1();
      // bits 6+7 are read by receiver 48 cycles after DATA LOW (FB71)

      // wait until 50 us after DATA LOW
      timer_wait_until(50);
    }

  // signal "not ready" by pulling CLK LOW
  writePinCLK(LOW);

  // release DATA
  writePinDATA(HIGH);

  interrupts();

  JDEBUG0();

  return true;
}


#endif // IEC_FP_JIFFY

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


bool IECBusHandler::transmitDolphinByte(uint8_t numData)
{
  // Note: receiver starts a 50us timeout after setting DATA high
  // (ready-to-receive) waiting for CLK low (data valid). If we take
  // longer than those 50us the receiver will interpret that as EOI
  // (last byte of data). So we need to take precautions:
  // - disable interrupts between setting CLK high and setting CLK low
  // - get the data byte to send before setting CLK high
  // - wait for DATA high in a blocking loop
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

// ------------------------------------  Final Cartridge 3 support routines  ------------------------------------


#ifdef IEC_FP_FC3

// necessary for UNO since otherwise the writePinCLK/writePinDATA 
// calls take too long and the timing will be off.
#pragma GCC push_options
#pragma GCC optimize ("O2")

void RAMFUNC(IECBusHandler::transmitFC3Bytes)(uint8_t *data)
{
  // In the following table, the "Cycle" column for PAL/NTSC gives the cycle number
  // counted from the beginning of the "CLK low" detection loop on the C64 (at $9962),
  // i.e.the fastest case where CLK is already low when it is read.
  // The loop itself takes 7 cycles for each iteration which introduces a 7-cycle
  // variation between when each bit may be read.
  //
  // The "read time" column for each bit shows the earliest and latest time after
  // "CLK low" that the C64 may read the given bits. The "write" column is the time
  // at which the NEXT bits should be written and "margin" gives the amount of time
  // after the write time before the bits are read. Note that because of the 7-cycle
  // variation and different timing between NTSC and PAL, the margins are very small.
  // Note that FC3 code has one extra NOP in the receive code on NTSC (before bits 2+3)
  // to (somewhat) balance out the faster clock speed.
  //
  //               ----- PAL -----   ---- NTSC ----  -- read time --
  // Byte | Bits | Cycle |     us | Cycle |    us       min      max | write | margin
  //    1 | 0+1  |  13   |  13.19 |  13   |  12.71 |  12.71 |  20.30 |  20.5 | 4.87
  //    1 | 2+3  |  25   |  25.37 |  27   |  26.40 |  25.37 |  33.24 |  33.5 | 4.05
  //    1 | 4+5  |  37   |  37.55 |  39   |  38.13 |  37.55 |  44.98 |  45.5 | 4.23
  //    1 | 6+7  |  49   |  49.73 |  51   |  49.87 |  49.73 |  56.84 |  57.5 | 6.06
  //    2 | 0+1  |  63   |  63.94 |  65   |  63.56 |  63.56 |  71.05 |  71.5 | 4.62
  //    2 | 2+3  |  75   |  76.12 |  79   |  77.24 |  76.12 |  84.09 |  84.5 | 3.80
  //    2 | 4+5  |  87   |  88.30 |  91   |  88.98 |  88.30 |  95.82 |  96.5 | 3.98
  //    2 | 6+7  |  99   | 100.48 | 103   | 100.71 | 100.48 | 107.59 | 108.5 | 5.90
  //    3 | 0+1  | 113   | 114.69 | 117   | 114.40 | 114.40 | 121.80 | 122.5 | 4.37
  //    3 | 0+1  | 125   | 126.87 | 131   | 128.09 | 126.87 | 134.93 | 135.5 | 3.55
  //    3 | 2+3  | 137   | 139.05 | 143   | 139.82 | 139.05 | 146.67 | 147.5 | 3.73
  //    3 | 4+5  | 149   | 151.23 | 155   | 151.56 | 151.23 | 158.40 | 159.5 | 5.74
  //    4 | 0+1  | 163   | 165.44 | 169   | 165.24 | 165.24 | 172.55 | 173   | 4.62
  //    4 | 2+3  | 175   | 177.62 | 183   | 178.93 | 177.62 | 185.78 | 186   | 3.80
  //    4 | 4+5  | 187   | 189.80 | 195   | 190.67 | 189.80 | 197.51 | 198   | 3.98
  //    4 | 6+7  | 199   | 201.98 | 207   | 202.40 | 201.98 | 209.24 | 210   |

#define FC3_TRANSMIT_BYTE(b, t) \
  JDEBUG0();                    \
  writePinCLK( b & bit(0));     \
  writePinDATA(b & bit(1));     \
  JDEBUG1();                    \
  timer_wait_until(t);          \
  JDEBUG0();                    \
  writePinCLK( b & bit(2));     \
  writePinDATA(b & bit(3));     \
  JDEBUG1();                    \
  timer_wait_until(t+13);       \
  JDEBUG0();                    \
  writePinCLK( b & bit(4));     \
  writePinDATA(b & bit(5));     \
  JDEBUG1();                    \
  timer_wait_until(t+25);       \
  JDEBUG0();                    \
  writePinCLK( b & bit(6));     \
  writePinDATA(b & bit(7));     \
  JDEBUG1();                    \
  timer_wait_until(t+37);

  timer_init();
  timer_reset();

  // signal "ready" by pulling CLK low
  // At this point the receiver is in a fairly tight loop waiting for CLK low (9962-9966)
  // but since each cycle of the loop takes 7 cycles there is up to 7.1us variance in when the
  // loop is exited and therefore when the transmitted data below is actually read.
  writePinCLK(LOW);
  timer_start();

  // make sure the C64 has seen our CLK low
  timer_wait_until(8);

#if defined(__AVR__)
  // On AVR we need to start TRANSMIT_BYTE early because of the slow processor
  // (takes time to get from timer_wait_until() to set the CLK/DATA pins)
#define FC3_OFFSET -1
#elif defined(ARDUINO_ARCH_RP2040)
  // PiPico timer resolution is only 1us, so if we try to wait 10us it may end up 
  // being just slightly more than 9us. However, it is fast so we can wait some
  // extra cycles and still have the CLK/DATA signals updated in time.
#define FC3_OFFSET 1.5
#else
#define FC3_OFFSET 0
#endif

  // transmit four bytes (for timing values see table above)
  FC3_TRANSMIT_BYTE(data[0],  20.5 + FC3_OFFSET);
  FC3_TRANSMIT_BYTE(data[1],  71.5 + FC3_OFFSET);
  FC3_TRANSMIT_BYTE(data[2], 122.5 + FC3_OFFSET);
  FC3_TRANSMIT_BYTE(data[3], 173   + FC3_OFFSET);

  // release CLK
  writePinCLK(HIGH);
  timer_stop();
}


bool RAMFUNC(IECBusHandler::receiveFC3Byte)(uint8_t *pdata)
{
  uint8_t data = 0;
  timer_init();
  timer_reset();

  // wait (indefinitely) for CLK high
  while( !digitalReadFastExtIEC(m_pinCLK, m_regCLKread, m_bitCLK) && digitalReadFastExtIEC(m_pinATN, m_regATNread, m_bitATN) )
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
  timer_start();

  // abort if ATN low
  if( !readPinATN() ) { JDEBUG0(); return false; }
  
  // sender sets bits 7(CLK) and 5(DATA) 12us after CLK high
  timer_wait_until(15);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(7);
  if( !readPinDATA() ) data |= bit(5);
  JDEBUG0();

  // sender sets bits 6(CLK) and 4(DATA) 22us after CLK high
  timer_wait_until(25);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(6);
  if( !readPinDATA() ) data |= bit(4);
  JDEBUG0();
  
  // sender sets bits 3(CLK) and 1(DATA) 38us after CLK high
  timer_wait_until(41);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(3);
  if( !readPinDATA() ) data |= bit(1);
  JDEBUG0();
  
  // sender sets bits 2(CLK) and 0(DATA) 48us after CLK high
  timer_wait_until(51);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(2);
  if( !readPinDATA() ) data |= bit(0);
  JDEBUG0();

  // sender releases DATA and pulls CLK low 58us after CLK high
  if( !waitPinCLK(LOW) ) return false;
  timer_stop();

  *pdata = data;
  return true;
}

#pragma GCC pop_options

int8_t RAMFUNC(IECBusHandler::transmitFC3Block)()
{
  m_inTask = false;
  if( m_buffer[1]==0 )
    {
      // first block => we need only 252 bytes since
      // the first two bytes (load address) were already transmitted via regular 
      // serial protocol. The receiver discards the repeated two bytes so their
      // actual value does not matter.
      // But we do read one more byte to test whether there will be a next block.
      // If there are more blocks to transmit then n must be 0, otherwise it must
      // be one more than the number of data bytes in this block 
      // (n+3 because of the repeated load address)
      uint8_t n = m_currentDevice->read(m_buffer+5, 253);
      m_buffer[2] = (n==253) ? 0 : n+3;
    }
  else
    {
      // second or later block => move the extra data byte that was read before to the beginning
      // and attempt to read 254 more bytes (one byte more than needed for this block).
      // If there is more data (i.e. blocks) to transmit then n must be 0, otherwise it must
      // be one more than the number of data bytes in this block (n+1 because of the extra data byte)
      m_buffer[3] = m_buffer[257];
      uint8_t n = m_currentDevice->read(m_buffer+4, 254);
      m_buffer[2] = (n==254) ? 0 : n+2;
    }

  m_inTask = true;

  // if ATN was asserted then done
  if( m_flags & P_ATN ) return -1;
  
  // signal "ready" by pulling CLK low
  writePinCLK(LOW);
  
  // wait for confirmation (DATA low)
  if( !waitPinDATA(LOW, 0) ) return -1;
  
  // release CLK
  writePinCLK(HIGH);

  // wait for DATA high
  if( !waitPinDATA(HIGH, 0) ) return -1;
  
  noInterrupts();

  // transmit 260 bytes of data (65 x 4 bytes)
  // byte 0: not used by receiver
  // byte 1: block number
  // byte 2: number of valid data bytes in block (0=full block of 254 bytes)
  // byte 3-256: 254 data bytes in block
  // byte 257-259: not used by receiver
  uint8_t *data = m_buffer;
  for(int i=0; i<65; i++)
    {
      // wait to give receiver time to get ready for next data segment
      delayMicrosecondsISafe(150);

      // transmit 4-byte tuple
      transmitFC3Bytes(data);

      // next 4 bytes
      data +=4;
    }

  // release CLK, signal end-of-data by pulling DATA low if this was the last block
  writePinCLK(HIGH);
  writePinDATA(m_buffer[2]==0 ? HIGH : LOW);

  // increment block number
  m_buffer[1]++;

  interrupts();

  // return true if more blocks to transmit
  return m_buffer[2]==0;
}


int8_t RAMFUNC(IECBusHandler::transmitFC3ImageBlock)()
{
  m_inTask = false;
  uint8_t n = m_currentDevice->read(m_buffer+3, 254);
  m_buffer[2] = (n==254) ? 0 : n+1;
  m_inTask = true;
  
  // if no more data available or ATN was asserted then done
  if( n==0 || (m_flags & P_ATN) ) return false;

  noInterrupts();

  // transmit 260 bytes of data (65 x 4 bytes)
  // bytes 0-2: not used by receiver
  // byte 3-256: 254 data bytes in block
  // byte 257-259: not used by receiver
  uint8_t *data = m_buffer;
  for(int i=0; i<65; i++)
    {
      // signal "ready" by pulling CLK low
      writePinCLK(LOW);
      
      // wait for confirmation (DATA low)
      if( !waitPinDATA(LOW, 0) ) { interrupts(); return -1; }
      
      // release CLK
      writePinCLK(HIGH);
      
      // wait for DATA high
      if( !waitPinDATA(HIGH, 0) ) { interrupts(); return -1; }
  
      // transmit 4-byte tuple
      transmitFC3Bytes(data);

      // release CLK and DATA
      writePinCLK(HIGH);
      writePinDATA(HIGH);

      // next 4 bytes
      data +=4;
    }

  interrupts();

  // return 1 if more blocks to transmit, otherwise 0
  return m_buffer[2]==0 ? 1 : 0;
}


int8_t RAMFUNC(IECBusHandler::receiveFC3Block)()
{
  noInterrupts();

  // signal "ready"
  writePinDATA(HIGH);
  
  // receive block data length
  uint8_t len;
  if( !receiveFC3Byte(&len) ) { interrupts(); return -1; }

  // receive block data
  uint8_t n = len==0 ? 254 : len-1;
  for(uint8_t i=0; i<n; i++)
    if( !receiveFC3Byte(m_buffer+i) ) 
      { interrupts(); return -1; }

  // signal "not ready"
  writePinDATA(LOW);
  
  interrupts();

  // len>0 signals that this was the last data block (EOI)
  bool eoi = len>0;

  // send data to device
  m_inTask = false;
  bool ok = m_currentDevice->write(m_buffer, n, eoi)==n;
  m_inTask = true;

  if( ok )
    return eoi ? 0 : 1;
  else
    return -1;
}


#endif


// ------------------------------------  Action Replay 6 support routines  ------------------------------------


#ifdef IEC_FP_AR6
// necessary for UNO since otherwise the writePinCLK/writePinDATA 
// calls take too long and the timing will be off.
#pragma GCC push_options
#pragma GCC optimize ("O2")

bool RAMFUNC(IECBusHandler::transmitAR6Byte)(uint8_t data, bool ar6Protocol)
{
  noInterrupts();
  timer_init();
  timer_reset();

  // release CLK (signal "ready")
  writePinCLK(HIGH);

  // wait (indefinitely) for DATA high
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
  timer_start();

  // abort if ATN low
  if( !readPinATN() ) { interrupts(); return false; }

  if( ar6Protocol )
    {
      // Action Replay 6 protocol

      JDEBUG0();
      writePinCLK( data & bit(0));
      writePinDATA(data & bit(1));
      JDEBUG1();
      // receiver reads bits 0(CLK) and 1(DATA) 10us after DATA high
      timer_wait_until(12);
      JDEBUG0();
      writePinCLK( data & bit(2));
      writePinDATA(data & bit(3));
      JDEBUG1();
      // receiver reads bits 2(CLK) and 3(DATA) 18us after DATA high
      timer_wait_until(20);
      JDEBUG0();
      writePinCLK( data & bit(4));
      writePinDATA(data & bit(5));
      JDEBUG1();
      // receiver reads bits 4(CLK) and 5(DATA) 26us after DATA high
      timer_wait_until(28);
      JDEBUG0();
      writePinCLK( data & bit(6));
      writePinDATA(data & bit(7));
      JDEBUG1();
      // receiver reads bits 4(CLK) and 5(DATA) 34us after DATA high
      timer_wait_until(36);
    }
  else
    {
      // Action Replay 3 protocol (for image loader)
      data = ~data;

      JDEBUG0();
      writePinCLK( data & bit(7));
      writePinDATA(data & bit(5));
      JDEBUG1();
      // receiver reads bits 7(CLK) and 5(DATA) 16us after DATA high
      timer_wait_until(18);
      JDEBUG0();
      writePinCLK( data & bit(6));
      writePinDATA(data & bit(4));
      JDEBUG1();
      // receiver reads bits 6(CLK) and 4(DATA) 26us after DATA high
      timer_wait_until(28);
      JDEBUG0();
      writePinCLK( data & bit(3));
      writePinDATA(data & bit(1));
      JDEBUG1();
      // receiver reads bits 3(CLK) and 1(DATA) 36us after DATA high
      timer_wait_until(38);
      JDEBUG0();
      writePinCLK( data & bit(2));
      writePinDATA(data & bit(0));
      JDEBUG1();
      // receiver reads bits 2(CLK) and 0(DATA) 46us after DATA high
      timer_wait_until(48);
    }

  // pull CLK low ("not ready") and release DATA
  writePinCLK(LOW);
  writePinDATA(HIGH);

  interrupts();

  // receiver pulls DATA low ("not ready") 38us after DATA high
  if( !waitPinDATA(LOW) ) return false;
  timer_stop();

  return true;
}


bool RAMFUNC(IECBusHandler::receiveAR6Byte)(uint8_t *pdata)
{
  uint8_t data = 0;

  noInterrupts();
  timer_init();
  timer_reset();

  // release CLK (signal "ready")
  writePinCLK(HIGH);

  JDEBUG1();
  // wait (indefinitely) for DATA high
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
  timer_start();
  JDEBUG0();

  // abort if ATN low
  if( !readPinATN() ) { interrupts(); return false; }
  
  // sender sets bits 7(CLK) and 5(DATA) 8us after CLK high
  timer_wait_until(11);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(7);
  if( !readPinDATA() ) data |= bit(5);
  JDEBUG0();

  // sender sets bits 6(CLK) and 4(DATA) 18us after CLK high
  timer_wait_until(21);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(6);
  if( !readPinDATA() ) data |= bit(4);
  JDEBUG0();
  
  // sender sets bits 3(CLK) and 1(DATA) 34us after CLK high
  timer_wait_until(37);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(3);
  if( !readPinDATA() ) data |= bit(1);
  JDEBUG0();
  
  // sender sets bits 2(CLK) and 0(DATA) 44us after CLK high
  timer_wait_until(47);
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(2);
  if( !readPinDATA() ) data |= bit(0);
  JDEBUG0();

  // signal "not ready"
  writePinCLK(LOW);

  interrupts();

  // sender releases CLK and pulls DATA low 57us after CLK high
  if( !waitPinDATA(LOW) ) return false;
  timer_stop();

  *pdata = data;
  return true;
}

#pragma GCC pop_options


int8_t RAMFUNC(IECBusHandler::transmitAR6Block)(bool ar6Protocol)
{
  uint8_t n;

  // the first two bytes of the file (load address) have already been sent using the
  // regular IEC protocol but the fast loader expects the file to "start over".
  // It discards the first two butes so we don't really have to send the same values.
  if( m_buffer[255]==0 )
    n = m_currentDevice->read(m_buffer+2, 252) + 2;
  else
    n = m_currentDevice->read(m_buffer, 254);

  if( !transmitAR6Byte(n, ar6Protocol) ) return -1;

  for(uint8_t i=0; i<n; i++)
    if( !transmitAR6Byte(m_buffer[i], ar6Protocol) )
      return -1;

  // next block number
  m_buffer[255]++;

  return n==0 ? 0 : 1;
}


int8_t RAMFUNC(IECBusHandler::receiveAR6Block)()
{
  for(uint16_t i=0; i<256; i++)
    if( !receiveAR6Byte(m_buffer+i) )
      return -1;
  
  // first byte of block is number of blocks to receive AFTER this one
  // it that is 0 then second byte is number of valid bytes within this block (+2)
  bool    eoi = m_buffer[0]==0;
  uint8_t n   = eoi ? m_buffer[1]-2 : 254;

  // send data to device
  m_inTask = false;
  bool ok = m_currentDevice->write(m_buffer+2, n, eoi)==n;
  m_inTask = true;

  if( ok )
    return eoi ? 0 : 1;
  else
    return -1;
}

#endif


// ------------------------------------  IEC protocol support routines  ------------------------------------  


bool RAMFUNC(IECBusHandler::receiveIECByteATN)(uint8_t &data)
{
#if defined(IEC_FP_SPEEDDOS)
  // SpeedDos protocol detection
  // BEFORE receiving secondary address, wait for either:
  //  HIGH->LOW edge (1us pulse) on incoming parallel handshake signal, 
  //      if received pull outgoing parallel handshake signal LOW to confirm
  //  LOW->HIGH edge on ATN or CLK, 
  //      if so then timeout, host does not support SpeedDos
  if( &data==&m_secondary )
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
      // wait for CLK=1, signaling data is ready
      JDEBUG1();

#ifdef IEC_FP_JIFFY
      // JiffyDos protocol detection
      if( (i==7) && (&data==&m_primary) && !waitPinCLK(HIGH, 200) )
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

  if( &data==&m_secondary )
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

  interrupts();

  if( canWriteOk )
    {
      // acknowledge receipt by pulling DATA low
      writePinDATA(LOW);

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

  // get the data byte from the device
#ifdef IEC_FP_AR6
  // After opening a file to load, Action Replay 6 reads the first 2 bytes (load address)
  // but then signals "ready" (DATA high) again before pulling ATN low which makes us
  // read and discard the third byte if we don't use peek() here
  uint8_t data = m_currentDevice->peek();
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

#ifdef IEC_FP_AR6
  // discard previously read data byte
  m_currentDevice->read();
#endif
  
  return true;
}


// called when a falling edge on ATN is detected, either by the pin change
// interrupt handler or by polling within the microTask function
void RAMFUNC(IECBusHandler::atnRequest)()
{
  // check if ATN is actually LOW, if not then just return (stray interrupt request)
  if( readPinATN() ) return;

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

  m_primary = 0;
  if( receiveIECByteATN(m_primary) && ((m_primary == 0x3f) || (m_primary == 0x5f) || (findDevice((unsigned int) m_primary & 0x1f)!=NULL)) )
    {
      // this is either UNLISTEN or UNTALK or we were addressed
      // => receive the secondary address, assume 0 if not sent
      if( (m_primary == 0x3f) || (m_primary == 0x5f) || !receiveIECByteATN(m_secondary) ) m_secondary = 0;

      // wait until either ATN or CLK is released
      // TODO: should this be more generic? Is the host allowed to
      //       address multiple devices within the same ATN sequence?
      if( waitPinCLK(HIGH, 0) )
        {
          // CLK released => host might issue an UNTALK/UNLISTEN right after TALK/LISTEN
          // (e.g. in game "tracer sanction")
          uint8_t p = 0;
          if( receiveIECByteATN(p) && (p==0x3f || p==0x5f) ) m_primary = 0;

          // wait until ATN is finally released
          waitPinATN(HIGH);
        }

      // ATN was released
      m_flags &= ~P_ATN;

      // allow ATN to pull DATA low in hardware
      writePinCTRL(LOW);
          
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
        }
      else if( (m_primary & 0xE0)==0x20 )
        {
          IECDevice *dev = findDevice(m_primary & 0x1F);
          if( dev!=NULL )
            {
              // we were told to listen
              m_currentDevice = dev;
              m_currentDevice->listen(m_secondary);
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
          writePinDATA(HIGH);
        }
    }
  else
    {
      // either we were not addressed or there was an error receiving the primary address
      delayMicrosecondsISafe(150);
      writePinCLK(HIGH);
      writePinDATA(HIGH);
      waitPinATN(HIGH);

      // if someone else was told to start talking then we must stop
      if( (m_primary & 0xE0)==0x40 ) m_flags &= ~P_TALKING;

      // allow ATN to pull DATA low in hardware
      writePinCTRL(LOW);
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
  // don't do anything if begin() hasn't been called yet
  if( m_flags==0xFF ) return;

  // prevent interrupt handler from calling atnRequest()
  m_inTask = true;

  // ------------------ check for activity on RESET pin -------------------

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

      // call "reset" function for attached devices
      for(uint8_t i=0; i<m_numDevices; i++)
        m_devices[i]->reset(); 
    }

  // ------------------ check for activity on ATN pin -------------------

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
