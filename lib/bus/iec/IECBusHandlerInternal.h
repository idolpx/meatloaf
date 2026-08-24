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
// Everything a fast loader implementation needs and nothing a user of
// IECBusHandler needs: the per-platform timer and fast-GPIO macros, the
// bus-state and per-device flag bits, and the pin accessors themselves.
//
// The pin accessors are real inline functions here rather than being defined
// once in IECBusHandler.cpp, because a loader's bit loop sets CLK and DATA at
// microsecond-resolution absolute times and cannot afford a call per pin.
//
// The variables the timer macros use are declared extern here and defined once
// in IECBusHandler.cpp. Do NOT turn them back into file-scope statics: every
// translation unit would get its own copy, so a timer_init() in one file would
// leave another file's timer_cycles_per_us_div2 at zero -- making every
// timer_wait_until() there return immediately -- and the haveInterrupts flag
// that waitPinDATA/waitPinCLK use to decide whether they may feed the watchdog
// would go out of step with the noInterrupts() that set it.
// -----------------------------------------------------------------------------

#ifndef IECBUSHANDLERINTERNAL_H
#define IECBUSHANDLERINTERNAL_H

#include "IECBusHandler.h"
#include "IECDevice.h"
#include "fastload.h"

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

// The timer macros below need a little state. IECBusHandler.cpp defines
// IEC_BUSHANDLER_DEFINE_GLOBALS before including this header and so provides
// the one definition; every other file gets a declaration.
#ifdef IEC_BUSHANDLER_DEFINE_GLOBALS
#define IEC_GLOBAL
#else
#define IEC_GLOBAL extern
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
IEC_GLOBAL unsigned long timer_start_ticks;
static inline uint16_t timer_ticks_diff(uint16_t t0, uint16_t t1) { return ((t0 < t1) ? 3000 + t0 : t0) - t1; }
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

IEC_GLOBAL unsigned long timer_start_ticks;
static inline uint32_t timer_ticks_diff(uint32_t t0, uint32_t t1) { return ((t0 < t1) ? 84000 + t0 : t0) - t1; }
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
IEC_GLOBAL unsigned long timer_start_us;
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
IEC_GLOBAL DRAM_ATTR esp_cpu_cycle_count_t timer_start_cycles, timer_cycles_per_us_div2;
#define timer_init()         timer_cycles_per_us_div2 = esp_rom_get_cpu_ticks_per_us()/2;
#define timer_reset()        timer_start_cycles = esp_cpu_get_cycle_count()
#define timer_start()        timer_start_cycles = esp_cpu_get_cycle_count()
#define timer_stop()         while(0)
#define timer_less_than(us)  ((esp_cpu_get_cycle_count()-timer_start_cycles) < ((uint32_t((us)*2)*timer_cycles_per_us_div2)))
#define timer_wait_until(us) \
  { esp_cpu_cycle_count_t to = uint32_t((us)*2) * timer_cycles_per_us_div2; \
    while( (esp_cpu_get_cycle_count()-timer_start_cycles) < to ); }

// interval in which we need to feed the interrupt WDT to stop it from re-booting the system
#define IWDT_FEED_TIME ((CONFIG_ESP_INT_WDT_TIMEOUT_MS-50)*1000)

// keep track whether interrupts are enabled or not (see comments in waitPinDATA/waitPinCLK)
#ifdef IEC_BUSHANDLER_DEFINE_GLOBALS
bool haveInterrupts = true;
#else
extern bool haveInterrupts;
#endif
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

IEC_GLOBAL unsigned long timer_start_us;
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
void RAMFUNC(delayMicrosecondsISafe)(uint16_t t);


// define digitalReadFastExtIEC according to whether IEC lines are inverted or not
#if defined(IEC_USE_LINE_DRIVERS) && defined(IEC_USE_INVERTED_INPUTS)
#define digitalReadFastExtIEC(pin, reg, bit) (!(digitalReadFastExt(pin, reg, bit)))
#else
#define digitalReadFastExtIEC(pin, reg, bit) (digitalReadFastExt(pin, reg, bit))
#endif

// Number of bytes the parallel loaders pre-buffer. Defined here rather than in
// protocol/parallel.cpp because handleFastLoadProtocols() needs it too.
#ifdef IEC_SUPPORT_PARALLEL
#define PARALLEL_PREBUFFER_BYTES 2
#endif

// -----------------------------------------------------------------------------
// bus state (m_flags) and per-device fast loader state (m_flFlags)
// -----------------------------------------------------------------------------

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
#define S_TURBODISK_FIRST        0x20  // Turbodisk transfer is still on its first block
// m_flFlags is a uint32_t and bits 0x01-0x20 are taken. A loader that needs
// per-transfer state of its own takes the next free bit and adds it here --
// there is no other record of what is in use, and reusing one silently breaks
// whichever loader owned it.

#define TC_NONE      0
#define TC_DATA_LOW  1
#define TC_DATA_HIGH 2
#define TC_CLK_LOW   3
#define TC_CLK_HIGH  4

// -----------------------------------------------------------------------------
// pin accessors -- inline so a loader's bit loop pays no call per edge
// -----------------------------------------------------------------------------

#ifdef IEC_USE_LINE_DRIVERS

inline void RAMFUNC(IECBusHandler::writePinCLK)(bool v)
{
#ifdef IEC_USE_INVERTED_LINE_DRIVERS
  digitalWriteFastExt(m_pinCLKout, m_regCLKwrite, m_bitCLKout, !v);
#else
  digitalWriteFastExt(m_pinCLKout, m_regCLKwrite, m_bitCLKout, v);
#endif
}

inline void RAMFUNC(IECBusHandler::writePinDATA)(bool v)
{
#ifdef IEC_USE_INVERTED_LINE_DRIVERS
  digitalWriteFastExt(m_pinDATAout, m_regDATAwrite, m_bitDATAout, !v);
#else
  digitalWriteFastExt(m_pinDATAout, m_regDATAwrite, m_bitDATAout, v);
#endif
}

#else

inline void RAMFUNC(IECBusHandler::writePinCLK)(bool v)
{
  // Emulate open collector behavior: 
  // - switch pin to INPUT  mode (high-Z output) for true
  // - switch pin to OUTPUT mode (LOW output) for false
  pinModeFastExt(m_pinCLK, m_regCLKmode, m_bitCLK, v ? INPUT : OUTPUT);
}


inline void RAMFUNC(IECBusHandler::writePinDATA)(bool v)
{
  // Emulate open collector behavior: 
  // - switch pin to INPUT  mode (high-Z output) for true
  // - switch pin to OUTPUT mode (LOW output) for false
  pinModeFastExt(m_pinDATA, m_regDATAmode, m_bitDATA, v ? INPUT : OUTPUT);
}
#endif
inline bool RAMFUNC(IECBusHandler::readPinATN)()
{
  return digitalReadFastExtIEC(m_pinATN, m_regATNread, m_bitATN)!=0;
}


inline bool RAMFUNC(IECBusHandler::readPinCLK)()
{
  return digitalReadFastExtIEC(m_pinCLK, m_regCLKread, m_bitCLK)!=0;
}


inline bool RAMFUNC(IECBusHandler::readPinDATA)()
{
  return digitalReadFastExtIEC(m_pinDATA, m_regDATAread, m_bitDATA)!=0;
}


inline bool RAMFUNC(IECBusHandler::readPinRESET)()
{
  if( m_pinRESET==0xFF ) return true;
  return digitalReadFastExtIEC(m_pinRESET, m_regRESETread, m_bitRESET)!=0;
}

#endif /* IECBUSHANDLERINTERNAL_H */
