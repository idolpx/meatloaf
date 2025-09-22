// -----------------------------------------------------------------------------
// Copyright (C) 2024 David Hansel
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

#ifndef GPIBDEVICE_H
#define GPIBDEVICE_H

#include "GPIBConfig.h"
#include <stdint.h>

class GPIBusHandler;

class GPIBDevice
{
 friend class GPIBusHandler;

 public:
  // pinATN should preferrably be a pin that can handle external interrupts
  // (e.g. 2 or 3 on the Arduino UNO), if not then make sure the task() function
  // gets called at least once evey millisecond, otherwise "device not present" 
  // errors may result
  GPIBDevice(uint8_t devnr = 0xFF);

  // call this to change the device number
  void setDeviceNumber(uint8_t devnr);
  
  // this can be overloaded by derived classes
  virtual bool isActive() { return m_isActive; }

  // if isActive() is not overloaded then use this to activate/deactivate a device
  void setActive(bool b) { m_isActive = b; }

 protected:
  // called when GPIBBusHandler::begin() is called
  virtual void begin() {}

  // called GPIBBusHandler::task() is called
  virtual void task()  {}

  // called on falling edge of RESET line
  virtual void reset() {}

  // called when bus master sends TALK command
  // talk() must return within 1 millisecond
  virtual void talk(uint8_t secondary)   {}

  // called when bus master sends LISTEN command
  // listen() must return within 1 millisecond
  virtual void listen(uint8_t secondary) {}

  // called when bus master sends UNTALK command
  // untalk() must return within 1 millisecond
  virtual void untalk() {}

  // called when bus master sends UNLISTEN command
  // unlisten() must return within 1 millisecond
  virtual void unlisten() {}

  // called before a write() call to determine whether the device
  // is ready to receive data.
  // canWrite() is allowed to take an indefinite amount of time
  // canWrite() should return:
  //  <0 if more time is needed before data can be accepted (call again later), blocks GPIB bus
  //   0 if no data can be accepted (error)
  //  >0 if at least one byte of data can be accepted
  virtual int8_t canWrite() { return 0; }

  // called before a read() call to see how many bytes are available to read
  // canRead() is allowed to take an indefinite amount of time
  // canRead() should return:
  //  <0 if more time is needed before we can read (call again later), blocks GPIB bus
  //   0 if no data is available to read (error)
  //   1 if one byte of data is available
  //  >1 if more than one byte of data is available
  virtual int8_t canRead() { return 0; }

  // called when the device received data
  // write() will only be called if the last call to canWrite() returned >0
  // write() must return within 1 millisecond
  // the "eoi" parameter will be "true" if sender signaled that this is the last 
  // data byte of a transmission
  virtual void write(uint8_t data, bool eoi) {}

  // called when the device is sending data
  // read() will only be called if the last call to canRead() returned >0
  // read() is allowed to take an indefinite amount of time
  virtual uint8_t read() { return 0; }

  // send pulse on SRQ line (if SRQ pin was set in GPIBBusHandler constructor)
  void sendSRQ();

 protected:
  bool       m_isActive;
  uint8_t    m_devnr;
  uint8_t    m_flEnabled;  // bit-mask for which fast-loaders are enabled (GPIB_FP_* in GPIBConfig.h)
  uint32_t   m_flFlags;    // internal fast-loader flags
  uint8_t    m_flProtocol; // currently active fast-load protocol
  GPIBusHandler *m_handler;
};

#endif

