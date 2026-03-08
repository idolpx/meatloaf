
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
  uint8_t write(uint8_t *data, uint8_t n);

  virtual uint8_t writeBufferData() = 0;
  virtual uint8_t readBufferData()  = 0;
  virtual std::shared_ptr<MStream> getStream() { return nullptr; };

protected:
  iecDrive *m_drive;
  uint8_t  *m_data;
  size_t    m_len, m_ptr;
};


class iecChannelHandlerFile : public iecChannelHandler
{
public: 
  iecChannelHandlerFile(iecDrive *drive, std::shared_ptr<MStream> stream, int fixLoadAddress = -1);
  virtual ~iecChannelHandlerFile();

  virtual uint8_t readBufferData();
  virtual uint8_t writeBufferData();
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


class driveMemory
{
private:
  std::vector<uint8_t> ram;  // 0000-07FF  RAM (lazy-allocated on first write)
  size_t _ramSize;           // intended RAM size

  // ROM bytes are shared across all drives that load the same file.
  // Stored in PSRAM when available; freed when the last drive releases it.
  struct RomBytes {
    uint8_t* data = nullptr;
    size_t   size = 0;
    ~RomBytes() { if (data) { free(data); data = nullptr; } }
  };
  std::shared_ptr<RomBytes> rom;

  static std::unordered_map<std::string, std::weak_ptr<RomBytes>>& getRomCache() {
    static std::unordered_map<std::string, std::weak_ptr<RomBytes>> cache;
    return cache;
  }

public:
  driveMemory(size_t ramSize = 2048) : _ramSize(ramSize) {}
  ~driveMemory() = default;

  uint16_t mw_hash = 0xFFFF;

  bool setRAM(size_t ramSize) {
    _ramSize = ramSize;
    if (!ram.empty()) ram.resize(ramSize, 0x00);
    return true;
  }

  bool setROM(std::string filename) {
    // Return immediately if a live cached copy already exists
    auto& cache = getRomCache();
    auto it = cache.find(filename);
    if (it != cache.end()) {
      auto locked = it->second.lock();
      if (locked) {
        rom = locked;
        printf("Drive ROM shared: %s (%zu bytes)\r\n", filename.c_str(), rom->size);
        return true;
      }
    }

    // Load from SD/.rom/ then flash /.rom/
    std::unique_ptr<MFile> rom_file(MFSOwner::File("/sd/.rom/" + filename, true));
    if (!rom_file) rom_file.reset(MFSOwner::File("/.rom/" + filename, true));
    if (!rom_file) return false;

    auto stream = rom_file->getSourceStream();
    if (!stream) return false;

    size_t size = stream->size();

    // Prefer PSRAM to keep ROM bytes out of scarce internal RAM
    uint8_t* buf = nullptr;
#ifdef CONFIG_SPIRAM
    buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (!buf) buf = (uint8_t*)malloc(size);
    if (!buf) return false;

    stream->read(buf, size);

    auto romData = std::make_shared<RomBytes>();
    romData->data = buf;
    romData->size = size;
    cache[filename] = romData;  // weak ref so cache entry auto-clears when unused
    rom = romData;

    printf("Drive ROM Loaded file[%s] size[%zu]\r\n", rom_file->url.c_str(), rom->size);
    return true;
  }

  size_t read(uint16_t addr, uint8_t *data, size_t len)
  {
    // RAM
    if ( addr < 0x0FFF )
    {
      if ( addr >= 0x0800 )
        addr -= 0x0800; // RAM Mirror

      if (ram.empty() || addr + len > ram.size()) {
        return 0;
      }

      memcpy(data, &ram[addr], len);
      Debug_printv("RAM read %04X:%s", addr, mstr::toHex(data, len).c_str());
      printf("%s",util_hexdump((const uint8_t *)ram.data(), ram.size()).c_str());
      return len;
    }

    // ROM
    if ( addr >= 0x8000 )
    {
      if ( addr >= 0xC000 )
        addr -= 0xC000;
      else if ( addr >= 0x8000 )
        addr -= 0x8000; // ROM Mirror

      if ( rom && rom->data )
      {
        if ( addr + len > rom->size ) len = rom->size - addr;
        memcpy(data, rom->data + addr, len);
        return len;
      }
      return 0;
    }

    return 0;
  }

  void write(uint16_t addr, const uint8_t *data, size_t len)
  {
    // RAM
    if ( addr < 0x0FFF )
    {
      if ( addr >= 0x0800 )
        addr -= 0x0800; // RAM Mirror

      // Lazy-allocate RAM on first write
      if (ram.empty()) ram.resize(_ramSize, 0x00);

      if (addr + len > ram.size()) return;
      memcpy(&ram[addr], data, len);
      mw_hash = esp_rom_crc16_be(mw_hash, data, len);
      Debug_printv("RAM write %04X:%s [%d] crc[%04X]", addr, mstr::toHex(data, len).c_str(), len, mw_hash);
    }
  }

  void execute(uint16_t addr)
  {
    // RAM
    if ( addr < 0x0FFF )
    {
      if ( addr >= 0x0800 )
        addr -= 0x0800; // RAM Mirror

      if (!ram.empty()) {
        Debug_printv("RAM execute %04X", addr);
        printf("%s",util_hexdump((const uint8_t *)ram.data(), ram.size()).c_str());
        mw_hash = 0xFFFF;
      }
    }

    // ROM
    if ( rom )
    {
      if ( addr >= 0x8000 )
      {
        if ( addr >= 0xC000 )
          addr -= 0xC000;
        else if ( addr >= 0x8000 )
          addr -= 0x8000; // ROM Mirror

        // Translate ROM functions to virtual drive functions
        Debug_printv("ROM execute %04X", addr);
      }
    }
  }

  void reset() {
    if (!ram.empty()) ram.assign(ram.size(), 0x00);
    mw_hash = 0xFFFF;
  }
};

class iecDrive : public SystemFileDevice
{
public:
  iecDrive(uint8_t devnum = 0x00);
  ~iecDrive();

  mediatype_t mount(fnFile *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
  void unmount();

  int     id() { return m_devnr; };
  uint8_t getNumOpenChannels();
  std::string getCWD() { return m_cwd->url; }
  uint8_t getStatusCode() { return m_statusCode; }
  void    setStatusCode(uint8_t code, uint8_t trk = 0);
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

  std::unique_ptr<MFile> m_cwd;   // current working directory
  iecChannelHandler *m_channels[16];
  uint8_t m_statusCode, m_statusTrk, m_numOpenChannels;
//#ifdef USE_VDRIVE
  VDrive   *m_vdrive;
//#endif
  uint32_t  m_byteCount;
  uint64_t  m_timeStart;

  driveMemory m_memory;
};

#endif // DRIVE_H
