#ifndef DRIVE_H
#define DRIVE_H

#include "../fuji/fujiHost.h"

#include <string>
#include <unordered_map>

#include "../../bus/iec/IECFileDevice.h"
#include "../../media/media.h"
#include "../meatloaf/meatloaf.h"
#include "../meatloaf/meat_buffer.h"
#include "../meatloaf/wrappers/iec_buffer.h"
#include "../meatloaf/wrappers/directory_stream.h"

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

 protected:
  iecDrive *m_drive;
  uint8_t  *m_data;
  size_t    m_len, m_ptr;
};


class iecChannelHandlerFile : public iecChannelHandler
{
 public: 
  iecChannelHandlerFile(iecDrive *drive, MStream *stream, int fixLoadAddress = -1);
  virtual ~iecChannelHandlerFile();

  virtual uint8_t readBufferData();
  virtual uint8_t writeBufferData();

 private:
  MStream *m_stream;
  int      m_fixLoadAddress;
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



class iecDrive : public IECFileDevice
{
 public:
  iecDrive(uint8_t devnum = 0xFF);
  ~iecDrive();

  mediatype_t mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
  void unmount();

  int     id() { return m_devnr; };
  uint8_t getNumOpenChannels() { return m_numOpenChannels; }
  uint8_t getStatusCode() { return m_statusCode; }
  void    setStatusCode(uint8_t code, uint8_t trk = 0);
  bool    hasError();

  fujiHost *m_host;

  // overriding the IECDevice isActive() function because device_active
  // must be a global variable
  bool device_active = true;
  virtual bool isActive() { return device_active; }

 private:
  // open file "name" on channel
  virtual bool open(uint8_t channel, const char *name);

  // close file on channel
  virtual void close(uint8_t channel);

  // write bufferSize bytes to file on channel, returning the number of bytes written
  // Returning less than bufferSize signals "cannot receive more data" for this file
  virtual uint8_t write(uint8_t channel, uint8_t *buffer, uint8_t bufferSize);

  // read up to bufferSize bytes from file in channel, returning the number of bytes read
  // returning 0 will signal end-of-file to the receiver. Returning 0
  // for the FIRST call after open() signals an error condition
  // (e.g. C64 load command will show "file not found")
  virtual uint8_t read(uint8_t channel, uint8_t *buffer, uint8_t bufferSize);

  // called when the bus master reads from channel 15 and the status
  // buffer is currently empty. this should populate buffer with an appropriate 
  // status message bufferSize is the maximum allowed length of the message
  virtual void getStatus(char *buffer, uint8_t bufferSize);

  // called when the bus master sends data (i.e. a command) to channel 15
  // command is a 0-terminated string representing the command to execute
  // commandLen contains the full length of the received command (useful if
  // the command itself may contain zeros)
  virtual void execute(const char *command, uint8_t cmdLen);

  // called on falling edge of RESET line
  virtual void reset();

  void set_cwd(std::string path);

  std::unique_ptr<MFile> m_cwd;   // current working directory
  iecChannelHandler *m_channels[16];
  uint8_t m_statusCode, m_statusTrk, m_numOpenChannels;
};

#endif // DRIVE_H
