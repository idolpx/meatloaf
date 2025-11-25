// -----------------------------------------------------------------------------
// Copyright (C) 2023 David Hansel
// GPIB Support added by James Johnston
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

#include "GPIBusHandler.h"
#include "GPIBDevice.h"

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

#if GPIB_MAX_DEVICES>30
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

//NOTE: Must disable GPIB_FP_DOLPHIN/GPIB_FP_SPEEDDOS, otherwise no pins left for debugging (except Mega)
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

// keep track whether interrupts are enabled or not (see comments in waitPinNRFD/waitPinDAV)
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


// -----------------------------------------------------------------------------------------

#define P_ATN        0x80
#define P_LISTENING  0x40
#define P_TALKING    0x20
#define P_DONE       0x10
#define P_RESET      0x08

#define TC_NONE      0
#define TC_DAV_LOW   1
#define TC_DAV_HIGH  2
#define TC_NRFD_LOW  3
#define TC_NRFD_HIGH 4
#define TC_NDAC_LOW  5
#define TC_NDAC_HIGH 6


GPIBusHandler *GPIBusHandler::s_bushandler = NULL;

void RAMFUNC(GPIBusHandler::writePinDAV)(bool v)
{
#ifdef GPIB_USE_INVERTED_LINE_DRIVERS
  digitalWriteFastExt(m_pinDAV, m_regDAVwrite, m_bitDAV, !v);
#else
  digitalWriteFastExt(m_pinDAV, m_regDAVwrite, m_bitDAV, v);
#endif
}

void RAMFUNC(GPIBusHandler::writePinNRFD)(bool v)
{
#ifdef GPIB_USE_INVERTED_LINE_DRIVERS
  digitalWriteFastExt(m_pinNRFD, m_regNRFDwrite, m_bitNRFD, !v);
#else
  digitalWriteFastExt(m_pinNRFD, m_regNRFDwrite, m_bitNRFD, v);
#endif
}

void RAMFUNC(GPIBusHandler::writePinNDAC)(bool v)
{
#ifdef GPIB_USE_INVERTED_LINE_DRIVERS
  digitalWriteFastExt(m_pinNDAC, m_regNDACwrite, m_bitNDAC, !v);
#else
  digitalWriteFastExt(m_pinNDAC, m_regNDACwrite, m_bitNDAC, v);
#endif
}

void RAMFUNC(GPIBusHandler::writePinEOI)(bool v)
{
#ifdef GPIB_USE_INVERTED_LINE_DRIVERS
  digitalWriteFastExt(m_pinEOI, m_regEOIwrite, m_bitEOI, !v);
#else
  digitalWriteFastExt(m_pinEOI, m_regEOIwrite, m_bitEOI, v);
#endif
}

void RAMFUNC(GPIBusHandler::writePinSRQ)(bool v)
{
#ifdef GPIB_USE_INVERTED_LINE_DRIVERS
  digitalWriteFastExt(m_pinSRQ, m_regSRQwrite, m_bitSRQ, !v);
#else
  digitalWriteFastExt(m_pinSRQ, m_regSRQwrite, m_bitSRQ, v);
#endif
}

void RAMFUNC(GPIBusHandler::writePinCTRL)(bool v)
{
  if( m_pinCTRL!=0xFF )
    digitalWrite(m_pinCTRL, v);
}

bool RAMFUNC(GPIBusHandler::readPinATN)()
{
  return digitalReadFastExt(m_pinATN, m_regATNread, m_bitATN)!=0;
}


bool RAMFUNC(GPIBusHandler::readPinDAV)()
{
  return digitalReadFastExt(m_pinDAV, m_regDAVread, m_bitDAV)!=0;
}


bool RAMFUNC(GPIBusHandler::readPinNRFD)()
{
  return digitalReadFastExt(m_pinNRFD, m_regNRFDread, m_bitNRFD)!=0;
}

bool RAMFUNC(GPIBusHandler::readPinNDAC)()
{
  return digitalReadFastExt(m_pinNDAC, m_regNDACread, m_bitNDAC)!=0;
}

bool RAMFUNC(GPIBusHandler::readPinEOI)()
{
  return digitalReadFastExt(m_pinEOI, m_regEOIread, m_bitEOI)!=0;
}

bool RAMFUNC(GPIBusHandler::readPinSRQ)()
{
  return digitalReadFastExt(m_pinSRQ, m_regSRQread, m_bitSRQ)!=0;
}

bool RAMFUNC(GPIBusHandler::readPinRESET)()
{
  if( m_pinRESET==0xFF ) return true;
  return digitalReadFastExt(m_pinRESET, m_regRESETread, m_bitRESET)!=0;
}


bool GPIBusHandler::waitTimeout(uint16_t timeout, uint8_t cond)
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
        case TC_DAV_LOW:
          if( readPinDAV()  == LOW  ) return true;
          break;
        case TC_DAV_HIGH:
          if( readPinDAV()  == HIGH ) return true;
          break;

        case TC_NRFD_LOW:
          if( readPinNRFD() == LOW  ) return true;
          break;
        case TC_NRFD_HIGH:
          if( readPinNRFD() == HIGH ) return true;
          break;

        case TC_NDAC_LOW:
          if( readPinNDAC() == LOW  ) return true;
          break;
        case TC_NDAC_HIGH:
          if( readPinNDAC() == HIGH ) return true;
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


void GPIBusHandler::waitPinATN(bool state)
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

bool GPIBusHandler::waitPinNDAC(bool state, uint16_t timeout)
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
      while( readPinNDAC()!=state )
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
      while( readPinNDAC()!=state )
        if( ((m_flags & P_ATN)!=0) == readPinATN() )
          return false;
#endif
    }
  else
    {
      // if waitTimeout for the given condition fails then exit
      if( !waitTimeout(timeout, state ? TC_NDAC_HIGH : TC_NDAC_LOW) ) return false;
    }

  // NDAC LOW can only be properly detected if ATN went HIGH->LOW
  // (m_flags&ATN)==0 and readPinATN()==0)
  // since other devices may have pulled NRFD LOW
  return state || (m_flags & P_ATN) || readPinATN();
}

bool GPIBusHandler::waitPinNRFD(bool state, uint16_t timeout)
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
      while( readPinNRFD()!=state )
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
      while( readPinNRFD()!=state )
        if( ((m_flags & P_ATN)!=0) == readPinATN() )
          return false;
#endif
    }
  else
    {
      // if waitTimeout for the given condition fails then exit
      if( !waitTimeout(timeout, state ? TC_NRFD_HIGH : TC_NRFD_LOW) ) return false;
    }

  // NRFD LOW can only be properly detected if ATN went HIGH->LOW
  // (m_flags&ATN)==0 and readPinATN()==0)
  // since other devices may have pulled NRFD LOW
  return state || (m_flags & P_ATN) || readPinATN();
}


bool GPIBusHandler::waitPinDAV(bool state, uint16_t timeout)
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
      while( readPinDAV()!=state )
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
      while( readPinDAV()!=state )
        if( ((m_flags & P_ATN)!=0) == readPinATN() )
          return false;
#endif
    }
  else
    {
      // if waitTimeout for the given condition fails then exit
      if( !waitTimeout(timeout, state ? TC_DAV_HIGH : TC_DAV_LOW) ) return false;
    }
  
  return true;
}


void GPIBusHandler::sendSRQ()
{
  if( m_pinSRQ!=0xFF )
    {
#if !defined(GPIB_USE_LINE_DRIVERS)
      digitalWrite(m_pinSRQ, LOW);
      pinMode(m_pinSRQ, OUTPUT);
      delayMicrosecondsISafe(1);
      pinMode(m_pinSRQ, INPUT);
#elif defined(GPIB_USE_INVERTED_LINE_DRIVERS)
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


GPIBusHandler::GPIBusHandler(uint8_t pinATN, uint8_t pinDAV, uint8_t pinNRFD, uint8_t pinNDAC, uint8_t pinEOI, uint8_t pinRESET, uint8_t pinCTRL, uint8_t pinSRQ, uint8_t pinDDR)
#if defined(GPIB_SUPPORT_PARALLEL_XRA1405)
#if defined(ESP_PLATFORM)
  // ESP32
: m_pinParallelSCK(18),
  m_pinParallelCOPI(23),
  m_pinParallelCIPO(19),
  m_pinParallelCS(22)
#elif defined(ARDUINO_ARCH_RP2040)
  // Raspberry Pi Pico
: m_pinParallelCS(20),
  m_pinParallelCIPO(16),
  m_pinParallelCOPI(19),
  m_pinParallelSCK(18)
#elif defined(__AVR_ATmega328P__) || defined(ARDUINO_UNOR4)
  // Arduino UNO, Pro Mini, Micro, Nano
: m_pinParallelCS(9),
  m_pinParallelCIPO(12),
  m_pinParallelCOPI(11),
  m_pinParallelSCK(13)
#else
#error "Parallel cable using XRA1405 not supported on this platform"
#endif
#else // !GPIB_SUPPORT_PARALLEL_XRA1405
#if defined(ESP_PLATFORM)
  // ESP32
: m_pinParallel{13,14,15,16,17,25,26,27}
#elif defined(ARDUINO_ARCH_RP2040)
  // Raspberry Pi Pico
: m_pinParallel{7,8,9,10,11,12,13,14}
#elif defined(__SAM3X8E__)
  // Arduino Due
: m_pinParallel{51,50,49,48,47,46,45,44}
#elif defined(__AVR_ATmega328P__) || defined(ARDUINO_UNOR4)
  // Arduino UNO, Pro Mini, Nano
: m_pinParallel{A0,A1,A2,A3,A4,A5,8,9}
#elif defined(__AVR_ATmega2560__)
  // Arduino Mega 2560
: m_pinParallel{22,23,24,25,26,27,28,29}
#else
#error "Parallel cable not supported on this platform"
#endif
#endif // GPIB_SUPPORT_PARALLEL_XRA1405
{
  m_numDevices = 0;
  m_inTask     = false;
  m_flags      = 0xFF; // 0xFF means: begin() has not yet been called

  m_pinATN       = pinATN;
  m_pinDAV       = pinDAV;    // pinCLK
  m_pinNRFD      = pinNRFD;   // pinCLKout
  m_pinNDAC      = pinNDAC;   // pinDATA
  m_pinEOI       = pinEOI;    // pinDATAout
  m_pinRESET     = pinRESET;
  m_pinCTRL      = pinCTRL;
  m_pinSRQ       = pinSRQ;
  m_pinDDR       = pinDDR;    // Data Direction Register

  m_buffer = NULL;
  m_bufferSize = 128;

#ifdef IOREG_TYPE
  m_bitATN       = digitalPinToBitMask(pinATN);
  m_regATNread   = portInputRegister(digitalPinToPort(pinATN));

  m_bitDAV       = digitalPinToBitMask(pinDAV);
  m_regDAVread   = portInputRegister(digitalPinToPort(pinDAV));
  m_regDAVwrite  = portOutputRegister(digitalPinToPort(pinDAV));
  m_regDAVmode   = portModeRegister(digitalPinToPort(pinDAV));

  m_bitNRFD      = digitalPinToBitMask(pinNRFD);
  m_regNRFDread  = portInputRegister(digitalPinToPort(pinNRFD));
  m_regNRFDwrite = portOutputRegister(digitalPinToPort(pinNRFD));
  m_regNRFDmode  = portModeRegister(digitalPinToPort(pinNRFD));

  m_bitNDAC      = digitalPinToBitMask(pinNDAC);
  m_regNDACread  = portInputRegister(digitalPinToPort(pinNDAC));
  m_regNDACwrite = portOutputRegister(digitalPinToPort(pinNDAC));
  m_regNDACmode  = portModeRegister(digitalPinToPort(pinNDAC));

  m_bitEOI       = digitalPinToBitMask(pinEOI);
  m_regEOIread   = portInputRegister(digitalPinToPort(pinEOI));
  m_regEOIwrite  = portOutputRegister(digitalPinToPort(pinEOI));
  m_regEOImode   = portModeRegister(digitalPinToPort(pinEOI));

  m_bitSRQ       = digitalPinToBitMask(pinSRQ);
  m_regSRQread   = portInputRegister(digitalPinToPort(pinSRQ));
  m_regSRQwrite  = portOutputRegister(digitalPinToPort(pinSRQ));
  m_regSRQmode   = portModeRegister(digitalPinToPort(pinSRQ));

  m_bitRESET     = digitalPinToBitMask(pinRESET);
  m_regRESETread = portInputRegister(digitalPinToPort(pinRESET));
#endif

  m_atnInterrupt = digitalPinToInterrupt(m_pinATN);
}


void GPIBusHandler::begin()
{
  JDEBUGI();

#if defined(GPIB_USE_LINE_DRIVERS)
  pinMode(m_pinNRFD,  OUTPUT);
  pinMode(m_pinNDAC,  OUTPUT);
  pinMode(m_pinEOI, OUTPUT);
  writePinDAV(HIGH);
  writePinNRFD(HIGH);
  writePinNDAC(HIGH);
  if( m_pinSRQ<0xFF )
    {
      pinMode(m_pinSRQ, OUTPUT);
      digitalWrite(m_pinSRQ, HIGH);
    }
#else
  // set pins to output 0 (when in output mode)
  pinMode(m_pinNRFD, OUTPUT); digitalWrite(m_pinNRFD, LOW);
  pinMode(m_pinNDAC, OUTPUT); digitalWrite(m_pinNDAC, LOW);
  if( m_pinSRQ<0xFF )
    {
      pinMode(m_pinSRQ, OUTPUT);
      digitalWrite(m_pinSRQ, HIGH);
    }
  if( m_pinDDR<0xFF )
    {
      pinMode(m_pinDDR, OUTPUT);
      digitalWrite(m_pinDDR, LOW);
    }
#endif

  pinMode(m_pinATN,   INPUT);
  pinMode(m_pinDAV,   INPUT);
  pinMode(m_pinEOI,  INPUT);
  if( m_pinCTRL<0xFF )  pinMode(m_pinCTRL,  OUTPUT);
  if( m_pinRESET<0xFF ) pinMode(m_pinRESET, INPUT);
  m_flags = 0;

  // allow ATN to pull NRFD low in hardware
  writePinCTRL(LOW);

  // if the ATN pin is capable of interrupts then use interrupts to detect 
  // ATN requests, otherwise we'll poll the ATN pin in function microTask().
  if( m_atnInterrupt!=NOT_AN_INTERRUPT && s_bushandler==NULL )
    {
      s_bushandler = this;
      attachInterrupt(m_atnInterrupt, atnInterruptFcn, FALLING);
    }

  // call begin() function for all attached devices
  for(uint8_t i=0; i<m_numDevices; i++)
    m_devices[i]->begin();
}


bool GPIBusHandler::canServeATN() 
{ 
  return (m_pinCTRL!=0xFF) || (m_atnInterrupt != NOT_AN_INTERRUPT); 
}


bool GPIBusHandler::inTransaction()
{
  return (m_flags & (P_LISTENING|P_TALKING))!=0;
}


bool GPIBusHandler::attachDevice(GPIBDevice *dev)
{
  if( m_numDevices<GPIB_MAX_DEVICES && findDevice(dev->m_devnr, true)==NULL )
    {
      dev->m_handler = this;
      dev->m_flFlags  = 0;
      dev->m_flProtocol = GPIB_FL_PROT_NONE;
#ifdef GPIB_SUPPORT_PARALLEL
      enableParallelPins();
#endif
      // if GPIBusHandler::begin() has been called already then call the device's
      // begin() function now, otherwise it will be called in GPIBusHandler::begin() 
      if( m_flags!=0xFF ) dev->begin();

      m_devices[m_numDevices] = dev;
      m_numDevices++;
      return true;
    }
  else
    return false;
}


bool GPIBusHandler::detachDevice(GPIBDevice *dev)
{
  for(uint8_t i=0; i<m_numDevices; i++)
    if( dev == m_devices[i] )
      {
        dev->m_handler = NULL;
        m_devices[i] = m_devices[m_numDevices-1];
        m_numDevices--;
#ifdef GPIB_SUPPORT_PARALLEL
        enableParallelPins();
#endif
        return true;
      }

  return false;
}


GPIBDevice *GPIBusHandler::findDevice(uint8_t devnr, bool includeInactive)
{
  for(uint8_t i=0; i<m_numDevices; i++)
    if( devnr == m_devices[i]->m_devnr && (includeInactive || m_devices[i]->isActive()) )
      return m_devices[i];

  return NULL;
}


void RAMFUNC(GPIBusHandler::atnInterruptFcn)(INTERRUPT_FCN_ARG)
{ 
  if( s_bushandler!=NULL && !s_bushandler->m_inTask & ((s_bushandler->m_flags & P_ATN)==0) )
    s_bushandler->atnRequest();
}


// ------------------------------------  Parallel cable support routines  ------------------------------------  

#define PARALLEL_PREBUFFER_BYTES 2

#ifdef GPIB_SUPPORT_PARALLEL_XRA1405

#if defined(ESP_PLATFORM) && !defined(ARDUINO)
#include "../../../include/esp-idf-spi.h"
#else
#include "SPI.h"
#endif

#pragma GCC push_options
#pragma GCC optimize ("O2")

uint8_t RAMFUNC(GPIBusHandler::XRA1405_ReadReg)(uint8_t reg)
{
  startParallelTransaction();
  digitalWriteFastExt(m_pinParallelCS, m_regParallelCS, m_bitParallelCS, LOW);
  uint8_t res = SPI.transfer16((0x40|reg) << 9) & 0xFF;
  digitalWriteFastExt(m_pinParallelCS, m_regParallelCS, m_bitParallelCS, HIGH);
  endParallelTransaction();
  return res;
}

void RAMFUNC(GPIBusHandler::XRA1405_WriteReg)(uint8_t reg, uint8_t data)
{
  startParallelTransaction();
  digitalWriteFastExt(m_pinParallelCS, m_regParallelCS, m_bitParallelCS, LOW);
  SPI.transfer16((reg << 9) | data);
  digitalWriteFastExt(m_pinParallelCS, m_regParallelCS, m_bitParallelCS, HIGH);
  endParallelTransaction();
}

#pragma GCC pop_options

#endif


#ifdef GPIB_SUPPORT_PARALLEL_XRA1405

void GPIBusHandler::setParallelPins(uint8_t pinSCK, uint8_t pinCOPI, uint8_t pinCIPO, uint8_t pinCS)
{
  m_pinParallelCOPI = pinCOPI;
  m_pinParallelCIPO = pinCIPO;
  m_pinParallelSCK  = pinSCK;
  m_pinParallelCS   = pinCS;
}

#else

void GPIBusHandler::setParallelPins(uint8_t pinD0, uint8_t pinD1, uint8_t pinD2, uint8_t pinD3, uint8_t pinD4, uint8_t pinD5, uint8_t pinD6, uint8_t pinD7)
{
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

bool GPIBusHandler::checkParallelPins()
{
  return (m_bufferSize>=PARALLEL_PREBUFFER_BYTES && 
          !isParallelPin(m_pinATN)   && !isParallelPin(m_pinDAV) && 
          !isParallelPin(m_pinNRFD) && !isParallelPin(m_pinNDAC) && 
          !isParallelPin(m_pinRESET) && !isParallelPin(m_pinCTRL) && 
          !isParallelPin(m_pinEOI) &&

#ifdef GPIB_SUPPORT_PARALLEL_XRA1405
          m_pinParallelCS!=0xFF && m_pinParallelSCK!=0xFF && m_pinParallelCOPI!=0xFF && m_pinParallelCIPO!=0xFF
#else
          m_pinParallel[0]!=0xFF && m_pinParallel[1]!=0xFF &&
          m_pinParallel[2]!=0xFF && m_pinParallel[3]!=0xFF &&
          m_pinParallel[4]!=0xFF && m_pinParallel[5]!=0xFF &&
          m_pinParallel[6]!=0xFF && m_pinParallel[6]!=0xFF
#endif
  );
}

bool GPIBusHandler::isParallelPin(uint8_t pin)
{
#ifdef GPIB_SUPPORT_PARALLEL_XRA1405
  if( pin==m_pinParallelCS || pin==m_pinParallelCOPI || pin==m_pinParallelCIPO || pin==m_pinParallelSCK )
    return true;
#else
  for(int i=0; i<8; i++) 
    if( pin==m_pinParallel[i] )
      return true;
#endif

  return false;
}


void GPIBusHandler::enableParallelPins()
{
    // at least one device uses the parallel cable
#if defined(IOREG_TYPE)
#if defined(GPIB_SUPPORT_PARALLEL_XRA1405)
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

    // initialize parallel bus pins
#ifdef GPIB_SUPPORT_PARALLEL_XRA1405
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


void RAMFUNC(GPIBusHandler::startParallelTransaction)()
{
#ifdef GPIB_SUPPORT_PARALLEL_XRA1405
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


void RAMFUNC(GPIBusHandler::endParallelTransaction)()
{
#ifdef GPIB_SUPPORT_PARALLEL_XRA1405
  if( m_inTransaction==1 ) SPI.endTransaction();
  if( m_inTransaction>0  ) m_inTransaction--;
#endif
}


#pragma GCC push_options
#pragma GCC optimize ("O2")
uint8_t RAMFUNC(GPIBusHandler::readParallelData)()
{
  uint8_t res = 0;
#ifdef GPIB_SUPPORT_PARALLEL_XRA1405
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


void RAMFUNC(GPIBusHandler::writeParallelData)(uint8_t data)
{
#ifdef GPIB_SUPPORT_PARALLEL_XRA1405
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


void RAMFUNC(GPIBusHandler::setParallelBusModeInput)()
{
  if ( m_dataDirection == PARALLEL_DATA_INPUT )
    return; // already in input mode

  digitalWrite(m_pinDDR, LOW);
  m_dataDirection = PARALLEL_DATA_INPUT;
#ifdef GPIB_SUPPORT_PARALLEL_XRA1405
  XRA1405_WriteReg(0x06, 0xFF); // GCR1, GPIO Configuration Register for P0-P7
#else
  // set parallel bus data pins to input mode
  for(int i=0; i<8; i++) 
    pinModeFastExt(m_pinParallel[i], m_regParallelMode[i], m_bitParallel[i], INPUT);
#endif
}


void RAMFUNC(GPIBusHandler::setParallelBusModeOutput)()
{
  if ( m_dataDirection == PARALLEL_DATA_OUTPUT )
    return; // already in output mode

  digitalWrite(m_pinDDR, HIGH);
  m_dataDirection = PARALLEL_DATA_OUTPUT;
#ifdef GPIB_SUPPORT_PARALLEL_XRA1405
  XRA1405_WriteReg(0x06, 0x00); // GCR1, GPIO Configuration Register for P0-P7
#else
  // set parallel bus data pins to output mode
  for(int i=0; i<8; i++) 
    pinModeFastExt(m_pinParallel[i], m_regParallelMode[i], m_bitParallel[i], OUTPUT);
#endif
}
#pragma GCC pop_options


// ------------------------------------  IEEE-488 protocol support routines  ------------------------------------  


bool RAMFUNC(GPIBusHandler::receiveGPIBByteATN)(uint8_t &data)
{
  // wait for DAV=1
  if( !waitPinDAV(HIGH, 0) ) return false;

  setParallelBusModeInput();

  // release NRFD ("not ready for data")
  writePinNRFD(HIGH);
  // release NDAC ("no data accepted")
  writePinNDAC(HIGH);

  // wait for DAV=0
  // must wait indefinitely since other devices may be holding NRFD low until
  // they are ready, bus master will start sending as soon as all devices have
  // released NRFD
  if( !waitPinDAV(LOW, 0) ) return false;

  // receive data bits
  data = readParallelData();

  // Acknowledge receipt by pulling NDAC low
  writePinNDAC(LOW);
  // Release NRFD
  writePinNRFD(HIGH);

  // Wait for DAV to be released?

  return true;
}


bool RAMFUNC(GPIBusHandler::receiveGPIBByte)(bool canWriteOk)
{
  // NOTE: we only get here if sender has already signaled ready-to-send
  // by releasing DAV
  bool eoi = false;

  noInterrupts();

  setParallelBusModeInput();

  // release NRFD ("not ready for data")
  writePinNRFD(HIGH);

  // check for EOI
  eoi = readPinEOI();

  // receive data bits
  uint8_t data = readParallelData();

  interrupts();

  if( canWriteOk )
    {
      // acknowledge receipt by pulling NDAC low
      writePinNDAC(LOW);
      // release NRFD
      writePinNRFD(HIGH);

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


bool RAMFUNC(GPIBusHandler::transmitGPIBByte)(uint8_t numData)
{
  noInterrupts();

  setParallelBusModeOutput();

  // signal "ready-to-send" (DAV=1)
  writePinDAV(HIGH);
  
  // wait (indefinitely, no timeout) for NRFD HIGH ("ready-to-receive")
  if( !waitPinNRFD(HIGH, 0) ) { interrupts(); return false; }
  
  if( numData==1 )
    {
      // only this byte left to send => signal EOI
      writePinEOI(LOW);
    }

  // if we have nothing to send then there was some kind of error 
  // => aborting at this stage will signal the error condition to the receiver
  //    (e.g. "File not found" for LOAD)
  if( numData==0 ) { interrupts(); return false; }

  interrupts();

  // get the data byte from the device
  uint8_t data = m_currentDevice->read();

  // transmit the byte
  // set data on DATA lines
  writeParallelData(data);

  // signal "data valid" (DAV=0)
  writePinDAV(LOW);


  // wait for receiver to signal "data received"
  if( !waitPinNDAC(HIGH) ) return false;

  // pull DAV=1 to signal "data invalid"
  writePinDAV(HIGH);

  // wait for receiver to signal "end data received"
  if( !waitPinNDAC(LOW) ) return false;

  return true;
}


// called when a falling edge on ATN is detected, either by the pin change
// interrupt handler or by polling within the microTask function
void RAMFUNC(GPIBusHandler::atnRequest)()
{
  // check if ATN is actually LOW, if not then just return (stray interrupt request)
  if( readPinATN() ) return;

  // falling edge on ATN detected (bus master addressing all devices)
  m_flags |= P_ATN;
  m_flags &= ~P_DONE;
  m_currentDevice = NULL;

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

  // release DAV (in case we were holding it LOW before)
  writePinDAV(HIGH);
  
  // set NRFD=0 ("I am here").  If nobody on the bus does this within 1ms,
  // busmaster will assume that "Device not present" 
  writePinNRFD(LOW);

  // disable the hardware that allows ATN to pull NRFD low
  writePinCTRL(HIGH);

  for(uint8_t i=0; i<m_numDevices; i++)
    {
      m_devices[i]->m_flProtocol = GPIB_FL_PROT_NONE;
    }
}


void RAMFUNC(GPIBusHandler::handleATNSequence)()
{
  // no more interrupts until the ATN sequence is finished. If we allowed interrupts
  // and a long interrupt occurred close to the end of the sequence then we may miss
  // a quick ATN low->high->low sequence, i.e completely missing the start of a new
  // ATN request.
  noInterrupts();

  // P_DONE flag may have gotten set again after it was reset in atnRequest()
  m_flags &= ~P_DONE;

  m_primary = 0;
  if( receiveGPIBByteATN(m_primary) && ((m_primary == 0x3f) || (m_primary == 0x5f) || (findDevice((unsigned int) m_primary & 0x1f)!=NULL)) )
    {
      // this is either UNLISTEN or UNTALK or we were addressed
      // => receive the secondary address, assume 0 if not sent
      if( (m_primary == 0x3f) || (m_primary == 0x5f) || !receiveGPIBByteATN(m_secondary) ) m_secondary = 0;

      // wait until ATN is released
      waitPinATN(HIGH);
      m_flags &= ~P_ATN;

      // allow ATN to pull NRFD low in hardware
      writePinCTRL(LOW);
          
      if( (m_primary & 0xE0)==0x20 && (m_currentDevice = findDevice(m_primary & 0x1F))!=NULL )
        {
          // we were told to listen
          m_currentDevice->listen(m_secondary);
          m_flags &= ~P_TALKING;
          m_flags |= P_LISTENING;

          // set NRFD=0 ("I am here")
          writePinNRFD(LOW);
        }
      else if( (m_primary & 0xE0)==0x40 && (m_currentDevice = findDevice(m_primary & 0x1F))!=NULL )
        {
          // we were told to talk
          m_currentDevice->talk(m_secondary);
          m_flags &= ~P_LISTENING;
          m_flags |= P_TALKING;

          // wait for bus master to set DAV=1 (and NRFD=0) for role reversal
          if( waitPinDAV(HIGH) )
            {
              // now set DAV=0 and NRFD=1
              writePinDAV(LOW);
              writePinNRFD(HIGH);
                  
              // wait 80us before transmitting first byte of data
              delayMicrosecondsISafe(80);
              m_timeoutDuration = 0;
            }
        }
      else if( (m_primary == 0x3f) && (m_flags & P_LISTENING) )
        {
          // all devices were told to stop listening
          m_flags &= ~P_LISTENING;
          for(uint8_t i=0; i<m_numDevices; i++)
            m_devices[i]->unlisten();
        }
      else if( m_primary == 0x5f && (m_flags & P_TALKING) )
        {
          // all devices were told to stop talking
          m_flags &= ~P_TALKING;
          for(uint8_t i=0; i<m_numDevices; i++)
            m_devices[i]->untalk();
        }
          
      if( !(m_flags & (P_LISTENING | P_TALKING)) )
        {
          // we're neither listening nor talking => release DAV/NRFD
          writePinDAV(HIGH);
          writePinNRFD(HIGH);
        }
    }
  else
    {
      // either we were not addressed or there was an error receiving the primary address
      delayMicrosecondsISafe(150);
      writePinDAV(HIGH);
      writePinNRFD(HIGH);
      waitPinATN(HIGH);

      // if someone else was told to start talking then we must stop
      if( (m_primary & 0xE0)==0x40 ) m_flags &= ~P_TALKING;

      // allow ATN to pull NRFD low in hardware
      writePinCTRL(LOW);
    }

  interrupts();
}



void GPIBusHandler::task()
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
      m_flags = 0;
      
      // release DAV and NRFD, allow ATN to pull NRFD low in hardware
      writePinDAV(HIGH);
      writePinNRFD(HIGH);
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
      readPinDAV() )
#else
  if( (m_flags & P_ATN)!=0 && !readPinATN() && (micros()-m_timeoutStart)>100 && readPinDAV() )
#endif
    {
      // we are under ATN, have waited 100us and the host has released DAV
      handleATNSequence();

      if( (m_flags & P_LISTENING)!=0 )
        {
          // a device is supposed to listen, check if it can accept data
          // (meanwhile allow atnRequest to be called in interrupt)
          GPIBDevice *dev = m_currentDevice;
          m_inTask = false;
          dev->task();
          bool canWrite = (dev->canWrite()>0);
          m_inTask = true;

          // m_currentDevice could have been reset to NULL while m_inTask was 'false'
          if( m_currentDevice!=NULL && !canWrite )
            {
              // device can't accept data => signal error by releasing NRFD line
              writePinNRFD(HIGH);
              m_flags |= P_DONE;
            }
        }
    }
  else if( (m_flags & P_ATN)!=0 && readPinATN() )
    {
      // host has released ATN
      m_flags &= ~P_ATN;
    }

  // ------------------ receiving data -------------------

  if( (m_flags & (P_ATN|P_LISTENING|P_DONE))==P_LISTENING && (m_currentDevice!=NULL) )
    {
     // we are not under ATN, are in "listening" mode and not done with the transaction

      // check if we can write (also gives devices a chance to
      // execute time-consuming tasks while bus master waits for ready-for-data)
      GPIBDevice *dev = m_currentDevice;
      m_inTask = false;
      int8_t numData = dev->canWrite();
      m_inTask = true;

      if( m_currentDevice==NULL )
        { /* m_currentDevice was reset while we were stuck in "canRead" */ }
      else if( !readPinATN() )
        { 
         // a falling edge on ATN happened while we were stuck in "canWrite"
          atnRequest();
        }
      else if( numData>=0 && readPinDAV() )
        {
          // either under ATN (in which case we always accept data)
          // or canWrite() result was non-negative
          // DAV high signals sender is ready to transmit
          if( !receiveGPIBByte(numData>0) )
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

       {
        // check if we can read (also gives devices a chance to
        // execute time-consuming tasks while bus master waits for ready-to-send)
        GPIBDevice *dev = m_currentDevice;
        m_inTask = false;
        int8_t numData = dev->canRead();
        m_inTask = true;

        if( m_currentDevice==NULL )
          { /* m_currentDevice was reset while we were stuck in "canRead" */ }
        else if( !readPinATN() )
          {
            // a falling edge on ATN happened while we were stuck in "canRead"
            atnRequest();
          }
        else if( (micros()-m_timeoutStart)<m_timeoutDuration || numData<0 )
          {
            // either timeout not yet met or canRead() returned a negative value => do nothing
          }
        else
          {
            // regular GPIB transfer
            if( transmitGPIBByte(numData) )
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
