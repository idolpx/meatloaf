// -----------------------------------------------------------------------------
// Copyright (C) 2024 David Hansel
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

#ifndef IECFILEDEVICE_H
#define IECFILEDEVICE_H

#include "IECDevice.h"


class IECFileDevice : public IECDevice
{
 public:
  IECFileDevice(uint8_t devnr = 0xFF);

 protected:
  // --- override the following functions in your device class:

  // called during IECBusHandler::begin()
  virtual void begin();

  // called during IECBusHandler::task()
  virtual void task();

  // open file "name" on channel, the file name will be zero-terminated but
  // nameLen can also be used, especially if the file name contains NUL characters
  virtual bool open(uint8_t channel, const char *name, uint8_t nameLen) = 0;

  // close file on channel
  virtual void close(uint8_t channel) = 0;

  // write bufferSize bytes to file on channel, returning the number of bytes written
  // Returning less than bufferSize signals "cannot receive more data" for this file.
  // If eoi is true then the sender has signaled that this is the final data for this transmission.
  virtual uint8_t write(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool eoi) = 0;

  // read up to bufferSize bytes from file in channel, returning the number of bytes read
  // returning 0 will signal end-of-file to the receiver. Returning 0
  // for the FIRST call after open() signals an error condition
  // (e.g. C64 load command will show "file not found")
  // If returning a data length >0 then the device may signal end-of-data AFTER transmitting
  // the data by setting *eoi to true.
  virtual uint8_t read(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool *eoi) = 0;

  // called when the bus master reads from channel 15, the status
  // buffer is currently empty and getStatusData() is not overloaded. 
  // This should populate buffer with an appropriate status message,
  // bufferSize is the maximum allowed length of the message
  // the data in the buffer should be a null-terminated string
  // "bufferSize" is defined by IECFILEDEVICE_STATUS_BUFFER_SIZE
  virtual void getStatus(char *buffer, uint8_t bufferSize) { *buffer=0; }

  // called when the bus master reads from channel 15 and the status
  // buffer is currently empty, this should 
  // - fill buffer with up to bufferSize bytes of data
  // - return the number of data bytes stored in the buffer
  // - set "eoi" to false if more data is available to read, true otherwise
  // The default implementation of getStatusData just calls getStatus().
  virtual uint8_t getStatusData(char *buffer, uint8_t bufferSize, bool *eoi);

  // called when the bus master sends data (e.g. a command) to channel 15
  // data is a pointer to the buffer containing the received data,
  // len contains the length of the received data.
  // If this function is NOT overloaded in a derived class then the
  // text-based "execute()" function (see below) will be called.
  // Overload this funcion if your device executes commands that may contain
  // binary data.
  virtual void executeData(const uint8_t *data, uint8_t len);

  // called when the bus master sends data (i.e. a command) to channel 15
  // and the aboce "execute(command, cmdLen)" is NOT overloaded.
  // command is a 0-terminated string representing the command to execute,
  // trailing CRs ($13) are stripped off.
  // Overload this function if all commands sent to your device are text-based
  // and do not contain biary data such as NUL or CR characters.
  virtual void execute(const char *command) {}

  // called on falling edge of RESET line
  virtual void reset();

  // can be called by derived class to set the status buffer
  void setStatus(const char *data, uint8_t dataLen);

  // can be called by derived class to clear the status buffer, causing readStatus()
  // to be called again the next time the status channel is queried
  void clearStatus();

  // clear the internal read buffer of the given channel, calling this will ensure
  // that the next TALK command will immediately call "read" to get new data instead 
  // of first sending the contents of the buffer
  void clearReadBuffer(uint8_t channel);

#if defined(IEC_FP_EPYX) && defined(IEC_FP_EPYX_SECTOROPS)
  virtual bool epyxReadSector(uint8_t track, uint8_t sector, uint8_t *buffer);
  virtual bool epyxWriteSector(uint8_t track, uint8_t sector, uint8_t *buffer);
#endif

 private:

  virtual void talk(uint8_t secondary);
  virtual void listen(uint8_t secondary);
  virtual void untalk();
  virtual void unlisten();
  virtual int8_t canWrite();
  virtual int8_t canRead();
  virtual void write(uint8_t data, bool eoi);
  virtual uint8_t write(uint8_t *buffer, uint8_t bufferSize, bool eoi);
  virtual uint8_t read();
  virtual uint8_t read(uint8_t *buffer, uint8_t bufferSize);
  virtual uint8_t peek();

  void fillReadBuffer();
  void emptyWriteBuffer();
  void fileTask();
  bool isFastLoaderRequest(const char *cmd);
  bool checkMWcmd(uint16_t addr, uint8_t len, uint8_t checksum) const;
  bool checkMWcmds(const struct MWSignature *sig, uint8_t sigLen, uint8_t offset);

  bool    m_opening, m_eoi, m_statusEoi, m_canServeATN;
  uint8_t m_channel, m_cmd, m_uploadCtr;
#if defined(IEC_FP_AR6)
  uint8_t m_ar6detect;
#endif
  uint8_t m_writeBuffer[IECFILEDEVICE_WRITE_BUFFER_SIZE];

  uint8_t m_readBuffer[15][2];
  uint8_t m_statusBufferLen, m_statusBufferPtr, m_writeBufferLen;
  int8_t  m_readBufferLen[15];
  char    m_statusBuffer[IECFILEDEVICE_STATUS_BUFFER_SIZE];
};


#endif
