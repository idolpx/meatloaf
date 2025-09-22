// -----------------------------------------------------------------------------
// Copyright (C) 2023 David Hansel
// GPIB Support added by James Johnston
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

#ifndef GPIBBUSHANDLER_H
#define GPIBBUSHANDLER_H

#include "GPIBConfig.h"
#include <stdint.h>

#if defined(__AVR__)
#define IOREG_TYPE uint8_t
#elif defined(ARDUINO_UNOR4_MINIMA) || defined(ARDUINO_UNOR4_WIFI)
#define IOREG_TYPE uint16_t
#elif defined(__SAM3X8E__) || defined(ESP_PLATFORM)
#define IOREG_TYPE uint32_t
#endif

#if defined(ESP_PLATFORM) && !defined(ARDUINO)
#define INTERRUPT_FCN_ARG void *
#else
#define INTERRUPT_FCN_ARG
#endif

#define GPIB_FL_PROT_NONE    0

class GPIBDevice;

class GPIBusHandler
{
 public:
  // pinATN should preferrably be a pin that can handle external interrupts
  // (e.g. 2 or 3 on the Arduino UNO), if not then make sure the task() function
  // gets called at least once evey millisecond, otherwise "device not present" 
  // errors may result
  GPIBusHandler(uint8_t pinATN, uint8_t pinDAV, uint8_t pinNRFD, uint8_t pinNDAC, uint8_t pinEOI, uint8_t pinRESET = 0xFF, uint8_t pinCTRL = 0xFF, uint8_t pinSRQ = 0xFF);
  //GPIBusHandler(uint8_t pinATN, uint8_t pinCLK, uint8_t pinCLKout, uint8_t pinDATAin, uint8_t pinDATAout, uint8_t pinRESET = 0xFF, uint8_t pinCTRL = 0xFF, uint8_t pinSRQ = 0xFF);

  // must be called once at startup before the first call to "task", devnr
  // is the GPIB bus device number that this device should react to
  void begin();

  bool attachDevice(GPIBDevice *dev);
  bool detachDevice(GPIBDevice *dev);

  // task must be called periodically to handle GPIB bus communication
  // if the ATN signal is NOT on an interrupt-capable pin then task() must be
  // called at least once every millisecond, otherwise less frequent calls are
  // ok but bus communication will be slower if called less frequently.
  void task();

#if !defined(GPIB_DEFAULT_FASTLOAD_BUFFER_SIZE)
  // if GPIB_DEFAULT_FASTLOAD_BUFFER_SIZE is set to 0 then the buffer space used
  // by fastload protocols can be set dynamically using the setBuffer function.
  void setBuffer(uint8_t *buffer, uint8_t bufferSize);
#endif

  // call this BEFORE begin() if you do not want to use the default pins for the parallel cable
#ifdef GPIB_SUPPORT_PARALLEL_XRA1405
  void setParallelPins(uint8_t pinSCK, uint8_t pinCOPI, uint8_t pinCIPO, uint8_t pinCS);
#else
  void setParallelPins(uint8_t pinD0, uint8_t pinD1, uint8_t pinD2, uint8_t pinD3, 
                         uint8_t pinD4, uint8_t pinD5, uint8_t pinD6, uint8_t pinD7);
#endif

  GPIBDevice *findDevice(uint8_t devnr, bool includeInactive = false);
  bool canServeATN();
  bool inTransaction();
  void sendSRQ();

  GPIBDevice *m_currentDevice;
  GPIBDevice *m_devices[GPIB_MAX_DEVICES];

  uint8_t m_numDevices;
  int  m_atnInterrupt;
  uint8_t m_pinATN, m_pinDAV, m_pinNRFD, m_pinNDAC, m_pinEOI, m_pinRESET, m_pinSRQ, m_pinCTRL;

 private:
  inline bool readPinATN();
  inline bool readPinDAV();
  inline bool readPinNRFD();
  inline bool readPinNDAC();
  inline bool readPinEOI();
  inline bool readPinRESET();
  inline void writePinDAV(bool v);
  inline void writePinNRFD(bool v);
  inline void writePinNDAC(bool v);
  inline void writePinEOI(bool v);
  void writePinCTRL(bool v);
  bool waitTimeout(uint16_t timeout, uint8_t cond = 0);
  bool waitPinEOI(bool state, uint16_t timeout = 1000);
  bool waitPinNDAC(bool state, uint16_t timeout = 1000);
  bool waitPinNRFD(bool state, uint16_t timeout = 1000);
  bool waitPinDAV(bool state, uint16_t timeout = 1000);
  void waitPinATN(bool state);
  void atnRequest();
  bool receiveGPIBByteATN(uint8_t &data);
  bool receiveGPIBByte(bool canWriteOk);
  bool transmitGPIBByte(uint8_t numData);
  void handleATNSequence();

  volatile uint16_t m_timeoutDuration; 
  volatile uint32_t m_timeoutStart;
  volatile bool m_inTask;
  volatile uint8_t m_flags;
  uint8_t m_primary, m_secondary;

#ifdef IOREG_TYPE
  volatile IOREG_TYPE *m_regDAVwrite, *m_regDAVmode, *m_regNRFDwrite, *m_regNRFDmode;
  volatile IOREG_TYPE *m_regNDACwrite, *m_regNDACmode, *m_regEOIwrite, *m_regEOImode;
  volatile const IOREG_TYPE *m_regATNread, *m_regDAVread, *m_regNRFDread, *m_regNDACread, *m_regEOIread, *m_regRESETread;
  IOREG_TYPE m_bitATN, m_bitDAV, m_bitNRFD, m_bitNDAC, m_bitEOI, m_bitRESET;
#endif


  void startParallelTransaction();
  void endParallelTransaction();
  void setParallelBusModeInput();
  void setParallelBusModeOutput();
  uint8_t readParallelData();
  void writeParallelData(uint8_t data);
  bool checkParallelPins();
  void enableParallelPins();
  bool isParallelPin(uint8_t pin);

#ifdef GPIB_SUPPORT_PARALLEL_XRA1405
  uint8_t m_pinParallelSCK, m_pinParallelCOPI, m_pinParallelCIPO, m_pinParallelCS, m_inTransaction;
  uint8_t XRA1405_ReadReg(uint8_t reg);
  void    XRA1405_WriteReg(uint8_t reg, uint8_t data);

#ifdef IOREG_TYPE
  volatile IOREG_TYPE *m_regParallelCS;
  IOREG_TYPE m_bitParallelCS;
#endif

#else // !GPIB_SUPPORT_PARALLEL_XRA1405

  uint8_t m_pinParallel[8];
#ifdef IOREG_TYPE
  volatile IOREG_TYPE *m_regParallelMode[8], *m_regParallelWrite[8];
  volatile const IOREG_TYPE *m_regParallelRead[8];
  IOREG_TYPE m_bitParallel[8];
#endif

#endif // GPIB_SUPPORT_PARALLEL_XRA1405

  uint8_t m_bufferCtr;
  uint8_t m_bufferSize;
  uint8_t *m_buffer;

  static GPIBusHandler *s_bushandler;
  static void atnInterruptFcn(INTERRUPT_FCN_ARG);
};

#endif
