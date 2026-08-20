
// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

#ifndef DRIVE_H
#define DRIVE_H

#include "../fuji/fujiHost.h"

#include <string>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <esp_rom_crc.h>
#include <esp_heap_caps.h>

#ifdef BUILD_IEC
#include "../../bus/iec/IECFileDevice.h"
#define SystemFileDevice IECFileDevice
#endif  // BUILD_IEC
#ifdef BUILD_GPIB
#include "../../bus/gpib/GPIBFileDevice.h"
#define SystemFileDevice GPIBFileDevice
#endif  // BUILD_GPIB


#include "drive/ram.h"
#include "../../media/media.h"
#include "../meatloaf/meatloaf.h"
#include "../meatloaf/meat_buffer.h"
#include "../meatloaf/wrapper/iec_buffer.h"
#include "../meatloaf/wrapper/directory_stream.h"
#include "utils.h"

//#ifdef USE_VDRIVE
#include "../vdrive/VDriveClass.h"
//#endif

//#include "dos/_dos.h"
//#include "dos/cbmdos.2.5.h"

#define PRODUCT_ID "MEATLOAF CBM"

class iecDrive;

class iecChannelHandler
{
public:
  iecChannelHandler(iecDrive *drive);
  virtual ~iecChannelHandler();

  uint8_t read(uint8_t *data, uint8_t n);
  virtual uint8_t write(uint8_t *data, uint8_t n);

  virtual uint8_t writeBufferData() = 0;
  virtual uint8_t readBufferData()  = 0;
  virtual std::shared_ptr<MStream> getStream() { return nullptr; };

  // What this channel has open, for the console "channels" listing.  Set by
  // iecDrive::open() from the MFile's fullUrl(), because the stream cannot
  // answer it: MStream carries only `url`, which for anything inside a
  // container is the CONTAINER's path, not the file's.
  void setName(const std::string &name) { m_name = name; }
  const std::string &name() const { return m_name; }

  // Bytes transferred on this channel -- where the READER is, which is not
  // where the stream is. The two never agree while a transfer is running, and
  // they disagree in OPPOSITE directions: readBufferData() fills a whole
  // BUFFER_SIZE ahead, so the stream leads (256 bytes read off a fresh channel
  // leaves the stream at 512), while write() accumulates until the buffer is
  // full, so the stream lags by whatever is still pending.
  //
  // Counted here rather than derived from m_stream->position() and the buffer
  // occupancy, because that correction needs the direction and the direction
  // is not knowable: m_stream->mode is not initialized for a stream inside a
  // disk image -- which is why writeBufferData()'s own mode check is
  // commented out.
  size_t position() const { return m_position; }

  bool m_eos = false;

protected:
  iecDrive *m_drive;
  uint8_t  *m_data;
  size_t    m_len, m_ptr;
  size_t    m_position = 0;
  std::string m_name;
};


class iecChannelHandlerFile : public iecChannelHandler
{
public: 
  iecChannelHandlerFile(iecDrive *drive, std::shared_ptr<MStream> stream, int fixLoadAddress = -1);
  virtual ~iecChannelHandlerFile();

  virtual uint8_t readBufferData();
  virtual uint8_t writeBufferData();
  virtual uint8_t write(uint8_t *data, uint8_t n) override;
  virtual std::shared_ptr<MStream> getStream() override { return m_stream; };

private:
  std::shared_ptr<MStream> m_stream;
  int       m_fixLoadAddress;
  uint32_t  m_byteCount;
  uint64_t  m_timeStart, m_transportTimeUS;
};


class iecChannelHandlerDir : public iecChannelHandler
{
public: 
  iecChannelHandlerDir(iecDrive *drive, MFile *dir);
  virtual ~iecChannelHandlerDir();

  virtual uint8_t readBufferData();
  virtual uint8_t writeBufferData();

private:
  void addExtraInfo(std::string title, std::string text);
  
  MFile   *m_dir;
  uint8_t  m_headerLine;
  std::vector<std::string> m_headers;
};


class iecDrive : public SystemFileDevice
{
public:
  iecDrive(uint8_t devnum = 0x00);
  ~iecDrive();

  mediatype_t mount(fnFile *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
  void unmount();

  // Persist/reload this drive's settings in mlConfig (devices.iec.<id>)
  void persistConfig();
  // Returns true if a network-scheme URL restore was deferred because WiFi
  // isn't connected yet (caller should retry later), false otherwise.
  bool reloadConfig();
  // Cheap, RESET-safe restore of just the persisted enabled flag (no URL/
  // mount work, unlike reloadConfig()) — safe to call from the real-time
  // IEC bus task's RESET handling. Undoes a runtime-only `iec sleep <id>`
  // by restoring the device to its persisted enabled state; a device
  // persistently disabled via config (enabled=0) correctly stays disabled.
  void restoreActiveFromConfig();

  // Source identifier used for WS activity notifications, e.g. "drive8".
  std::string activitySource() { return "drive" + std::to_string(m_devnr); }

  int     id() { return m_devnr; };
  uint8_t getNumOpenChannels();
  std::string getCWD() { return m_cwd->url; }

  // Console "use" support: point this drive at a directory the console has
  // already resolved and validated, so set_cwd() can skip its own (network-
  // expensive) verification pass.
  void consoleSetCwd(const std::string &url) { set_cwd(url, true); }

  // Console "exec" support: run a DOS command channel string and return the
  // resulting status line, consuming it exactly as a C64 read of channel 15
  // would.  *isError (when given) reports the status before it is consumed.
  std::string consoleExecDos(const std::string &command, bool *isError = nullptr);

  // Console "open"/"read"/"write"/"close" support: drive the FILE channels the
  // way a C64 does, so a read or a write inside any mounted media can be
  // exercised without a Commodore attached.  `channel` is the secondary
  // address, exactly as in OPEN <lfn>,<dev>,<sa>,"<name>".
  //
  // These call the protected virtuals, so device 30 still reaches
  // iecMeatloaf's overrides.  They are the only console-facing surface --
  // open()/read()/write()/close() stay protected.
  // One row of the console "channels" listing.  A directory channel has no
  // stream (the listing is generated, not read), so size and position are not
  // knowable for it -- has_stream says which.
  struct ChannelInfo
  {
    uint8_t     channel;
    std::string name;
    bool        has_stream;
    uint32_t    size;
    uint32_t    position;
  };
  std::vector<ChannelInfo> consoleChannels();

  bool    consoleOpen(uint8_t channel, const std::string &name);
  uint8_t consoleRead(uint8_t channel, uint8_t *buffer, uint8_t bufferSize);
  uint8_t consoleWrite(uint8_t channel, const uint8_t *buffer, uint8_t bufferSize, bool eoi);
  void    consoleClose(uint8_t channel);

  // Read the status the way the bus master would, consuming it: a reply
  // already pushed into the status buffer wins, and only an empty buffer falls
  // through to getStatusData().  *isError (when given) reports the state
  // BEFORE it is consumed -- getStatusData() resets the code to OK, just as a
  // channel-15 read does, so sampling afterwards always reads false.
  std::string consoleStatus(bool *isError = nullptr);

  uint8_t getStatusCode() { return m_statusCode; }
  void    setStatusCode(uint8_t code, uint8_t trk = 0, uint8_t sec = 0);
  // Overrides the canned getStatus() text.  For failures whose reason the
  // numeric CBM code cannot carry -- a network drive is "NOT READY" whether
  // the host is down or its certificate was rejected, and only the message
  // field can tell the two apart on the command channel.
  void    setStatusCode(uint8_t code, const std::string &msg);
  bool    hasError();
  bool    hasMemExeError();

  fujiHost *m_host;

  // overriding the IECDevice isActive() function because device_active
  // must be a global variable
  //bool device_active = true;
  //virtual bool isActive() { return device_active; }


protected:
  // initialize device
  virtual void begin();

  // open file "name" on channel
  virtual bool open(uint8_t channel, const char *name, uint8_t nameLen);

  // close file on channel
  virtual void close(uint8_t channel);

  // write bufferSize bytes to file on channel, returning the number of bytes written
  // Returning less than bufferSize signals "cannot receive more data" for this file
  virtual uint8_t write(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool eoi);

  // read up to bufferSize bytes from file in channel, returning the number of bytes read
  // returning 0 will signal end-of-file to the receiver. Returning 0
  // for the FIRST call after open() signals an error condition
  // (e.g. C64 load command will show "file not found")
  virtual uint8_t read(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool *eoi);

  // called when the bus master reads from channel 15 and the status
  // buffer is currently empty. this should populate buffer with an appropriate 
  // status message bufferSize is the maximum allowed length of the message
  virtual uint8_t getStatusData(char *buffer, uint8_t bufferSize, bool *eoi);
  virtual void getStatus(char *buffer, uint8_t bufferSize);

  // called when the bus master sends data (i.e. a command) to channel 15
  virtual void executeData(const uint8_t *data, uint8_t dataLen);

  // called on falling edge of RESET line
  virtual void reset();

#if defined(IEC_FP_EPYX) && defined(IEC_FP_EPYX_SECTOROPS)
  virtual bool epyxReadSector(uint8_t track, uint8_t sector, uint8_t *buffer);
  virtual bool epyxWriteSector(uint8_t track, uint8_t sector, uint8_t *buffer);
#endif

  void set_cwd(std::string path, bool verified = false);
  void changePartition(int pnum);   // CMD "CP<n>" on a mounted DHD/D1M/D2M/D4M image
  void tapeCommand(std::string command);  // "T-C"/"T-I" on a mounted tape image

  std::unique_ptr<MFile> m_cwd;   // current working directory
  iecChannelHandler *m_channels[16];
  uint8_t m_statusCode, m_statusTrk, m_statusSec, m_numOpenChannels;
  std::string m_statusMessage;   // when non-empty, replaces getStatus()'s canned text
//#ifdef USE_VDRIVE
  VDrive   *m_vdrive;
//#endif
  uint32_t  m_byteCount;
  uint64_t  m_timeStart;

  driveMemory m_memory;
};

#endif // DRIVE_H
