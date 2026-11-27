#ifndef IEC_DOS_HOST_H
#define IEC_DOS_HOST_H

// What a DOS may ask of the drive it runs on. iecDrive implements every member
// as a one-line forward; the DOS never reaches into iecDrive directly. Anything
// new the DOS needs is added here -- never as a friend.

#include <cstdint>
#include <string>

class MFile;
class iecChannelHandler;
class driveMemory;

class DosHost {
public:
  virtual ~DosHost() = default;
  virtual void     dosSetStatus(uint8_t code, uint8_t trk = 0, uint8_t sec = 0) = 0;
  virtual void     dosSetStatusMessage(uint8_t code, const std::string& msg) = 0;
  virtual void     dosSetStatusData(const char* data, uint8_t len) = 0;   // SystemFileDevice::setStatus
  virtual uint8_t  dosStatusCode() = 0;
  virtual MFile*   dosCwd() = 0;
  virtual void     dosSetCwd(const std::string& path) = 0;
  virtual iecChannelHandler* dosChannel(uint32_t n) = 0;                 // nullptr if n>15 or closed
  virtual void     dosClearReadBuffer(uint8_t channel) = 0;
  virtual bool     dosPositionChannel(uint32_t ch, uint32_t pos, bool within_block) = 0;
  // U1/U2 (cbm=false) and B-R/B-W (cbm=true) on a direct-access channel:
  // between the disk block and the channel's 256-byte buffer.
  virtual bool     dosBlockTransfer(uint8_t ch, uint8_t t, uint8_t s, bool for_write, bool cbm) = 0;
  virtual void     dosChangePartition(int pnum) = 0;
  virtual void     dosTapeCommand(const std::string& command) = 0;
  virtual driveMemory& dosMemory() = 0;
  virtual void     dosEnableFastLoader(uint8_t family, bool enable) = 0;
  virtual void     dosReset() = 0;
  virtual uint8_t  dosDeviceNumber() = 0;

  // VDrive mode (Meatloaf.use_vdrive) and VDrive's image creation, which the
  // N: command uses when that mode is on.
  virtual bool     dosUseVDrive() = 0;
  virtual bool     dosCreateDiskImage(const std::string& url, const std::string& diskname) = 0;
};

#endif
