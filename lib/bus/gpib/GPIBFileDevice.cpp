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

#include "GPIBFileDevice.h"
#include "GPIBusHandler.h"

#if defined(ARDUINO)
#include <Arduino.h>
#elif defined(ESP_PLATFORM)
#include "../../../include/esp-idf-arduino.h"
#endif

#define DEBUG 0

#if DEBUG>0

void print_hex(uint8_t data)
{
  static const PROGMEM char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  Serial.write(pgm_read_byte_near(hex+(data/16)));
  Serial.write(pgm_read_byte_near(hex+(data&15)));
}


static uint8_t dbgbuf[16], dbgnum = 0;

void dbg_print_data()
{
  if( dbgnum>0 )
    {
      for(uint8_t i=0; i<dbgnum; i++)
        {
          if( i==8 ) Serial.write(' ');
          print_hex(dbgbuf[i]);
          Serial.write(' ');
        }

      for(int i=0; i<(16-dbgnum)*3; i++) Serial.write(' ');
      if( dbgnum<8 ) Serial.write(' ');
      for(int i=0; i<dbgnum; i++)
        {
          if( (i&7)==0 ) Serial.write(' ');
          Serial.write(isprint(dbgbuf[i]) ? dbgbuf[i] : '.');
        }
      Serial.write('\r'); Serial.write('\n');
      dbgnum = 0;
    }
}

void dbg_data(uint8_t data)
{
  dbgbuf[dbgnum++] = data;
  if( dbgnum==16 ) dbg_print_data();
}

#endif


#define IFD_NONE  0
#define IFD_OPEN  1
#define IFD_CLOSE 2
#define IFD_EXEC  3
#define IFD_WRITE 4


struct MWSignature { uint16_t address; uint8_t len; uint8_t checksum; };

GPIBFileDevice::GPIBFileDevice(uint8_t devnr) : 
  GPIBDevice(devnr)
{
  m_cmd = IFD_NONE;
  m_opening = false;
}


void GPIBFileDevice::begin()
{
#if DEBUG>0
  /*if( !Serial )*/ Serial.begin(115200);
  for(int i=0; !Serial && i<5; i++) delay(1000);
  Serial.print(F("START:GPIBFileDevice, devnr=")); Serial.println(m_devnr); Serial.flush();
#endif
  
  bool ok;
#ifdef GPIB_FP_JIFFY
  ok = GPIBDevice::enableFastLoader(GPIB_FP_JIFFY, true);
#if DEBUG>0
  Serial.print(F("JiffyDos support ")); Serial.println(ok ? F("enabled") : F("disabled"));
#endif
#endif
#ifdef GPIB_FP_DOLPHIN
  ok = GPIBDevice::enableFastLoader(GPIB_FP_DOLPHIN, true);
#if DEBUG>0
  Serial.print(F("DolphinDos support ")); Serial.println(ok ? F("enabled") : F("disabled"));
#endif
#endif
#ifdef GPIB_FP_SPEEDDOS
  ok = GPIBDevice::enableFastLoader(GPIB_FP_SPEEDDOS, true);
#if DEBUG>0
  Serial.print(F("SpeedDos support ")); Serial.println(ok ? F("enabled") : F("disabled"));
#endif
#endif
#ifdef GPIB_FP_EPYX
  ok = GPIBDevice::enableFastLoader(GPIB_FP_EPYX, true);
#if DEBUG>0
  Serial.print(F("Epyx FastLoad support ")); Serial.println(ok ? F("enabled") : F("disabled"));
#endif
#endif
#ifdef GPIB_FP_FC3
  ok = GPIBDevice::enableFastLoader(GPIB_FP_FC3, true);
#if DEBUG>0
  Serial.print(F("Final Cartridge 3 support ")); Serial.println(ok ? F("enabled") : F("disabled"));
#endif
#endif
#ifdef GPIB_FP_AR6
  ok = GPIBDevice::enableFastLoader(GPIB_FP_AR6, true);
#if DEBUG>0
  Serial.print(F("Action Replay 6 support ")); Serial.println(ok ? F("enabled") : F("disabled"));
  m_ar6detect = 0;
#endif
#endif

  m_statusBufferPtr = 0;
  m_statusBufferLen = 0;
  m_writeBufferLen = 0;
  memset(m_readBufferLen, 0, 15);
  m_cmd = IFD_NONE;
  m_channel = 0xFF;
  m_opening = false;
  m_uploadCtr = 0;

  // calling fileTask() may result in significant time spent accessing the
  // disk during which we can not respond to ATN requests within the required
  // 1000us (interrupts are disabled during disk access). We have two options:
  // 1) call fileTask() from within the canWrite() and canRead() functions
  //    which are allowed to block indefinitely. Doing so has two downsides:
  //    - receiving a disk command via OPEN 1,x,15,"CMD" will NOT execute
  //      it right away because there is no call to canRead/canWrite after
  //      the "unlisten" call that finishes the transmission. The command will
  //      execute once the next operation (even just a status query) starts.
  //    - if the bus master pulls ATN low in the middle of a transmission
  //      (does not usually happen) we may not respond fast enough which may
  //      result in a "Device not present" error.
  // 2) add some minimal hardware that immediately pulls DATA low when ATN
  //    goes low (this is what the C1541 disk drive does). This will make
  //    the bus master wait until we release DATA when we are actually ready
  //    to communicate. In that case we can process fileTask() here which
  //    mitigates both issues with the first approach. The hardware needs
  //    one additional output pin (pinCTRL) used to enable/disable the
  //    override of the DATA line.
  //
  // if we have the extra hardware then m_pinCTRL!=0xFF 
  m_canServeATN = m_handler->canServeATN();

  GPIBDevice::begin();
}


uint8_t GPIBFileDevice::getStatusData(char *buffer, uint8_t bufferSize) 
{ 
  // call the getStatus() function that returns a null-terminated string
  m_statusBuffer[0] = 0;
  getStatus(m_statusBuffer, bufferSize);
  m_statusBuffer[bufferSize-1] = 0;
  return strlen(m_statusBuffer);
}


int8_t GPIBFileDevice::canRead() 
{ 
#if DEBUG>2
  Serial.write('c');Serial.write('R');
#endif

  // see comment in GPIBFileDevice constructor
  if( !m_canServeATN )
    {
      // for IFD_OPEN, fileTask() resets the channel to 0xFF which is a problem when we call it from
      // here because we have already received the LISTEN after the UNLISTEN that
      // initiated the OPEN and so m_channel will not be set again => remember and restore it here
      if( m_cmd==IFD_OPEN )
        { uint8_t c = m_channel; fileTask(); m_channel = c; }
      else
        fileTask();
    }

  if( m_channel==15 )
    {
      if( m_statusBufferPtr==m_statusBufferLen )
        {
          m_statusBufferPtr = 0;
          m_statusBufferLen = getStatusData(m_statusBuffer, GPIBFILEDEVICE_STATUS_BUFFER_SIZE);
#if DEBUG>0
          Serial.print(F("STATUS")); 
#if GPIB_MAX_DEVICES>1
          Serial.write('#'); Serial.print(m_devnr);
#endif
          Serial.write(':'); Serial.write(' ');
          Serial.println(m_statusBuffer);
          for(uint8_t i=0; i<m_statusBufferLen; i++) dbg_data(m_statusBuffer[i]);
          dbg_print_data();
#endif
        }
      
      return m_statusBufferLen-m_statusBufferPtr;
    }
  else if( m_channel > 15 || m_readBufferLen[m_channel]==-128 )
    {
      return 0; // invalid channel or OPEN failed for channel
    }
  else
    {
      fillReadBuffer();
#if DEBUG>2
      print_hex(m_readBufferLen[m_channel]);
#endif
      return m_readBufferLen[m_channel];
    }
}


uint8_t GPIBFileDevice::peek() 
{
  uint8_t data = 0;

  if( m_channel==15 )
    data = m_statusBuffer[m_statusBufferPtr];
  else if( m_channel < 15 )
    data = m_readBuffer[m_channel][0];

#if DEBUG>1
  Serial.write('P'); print_hex(data);
#endif

  return data;
}


uint8_t GPIBFileDevice::read() 
{ 
  uint8_t data = 0;

  if( m_channel==15 )
    data = m_statusBuffer[m_statusBufferPtr++];
  else if( m_channel<15 )
    {
      data = m_readBuffer[m_channel][0];
      if( m_readBufferLen[m_channel]==2 )
        {
          m_readBuffer[m_channel][0] = m_readBuffer[m_channel][1];
          m_readBufferLen[m_channel] = 1;
        }
      else
        m_readBufferLen[m_channel] = 0;
    }

#if DEBUG>1
  Serial.write('R'); print_hex(data);
#endif

  return data;
}


uint8_t GPIBFileDevice::read(uint8_t *buffer, uint8_t bufferSize)
{
  uint8_t res = 0;

  // get data from our own 2-byte buffer (if any)
  // properly deal with the case where bufferSize==1
  while( m_readBufferLen[m_channel]>0 && res<bufferSize )
    {
      buffer[res++] = m_readBuffer[m_channel][0];
      m_readBuffer[m_channel][0] = m_readBuffer[m_channel][1];
      m_readBufferLen[m_channel]--;
    }

  // get data from higher class
  while( res<bufferSize && !m_eoi )
    {
      uint8_t n = read(m_channel, buffer+res, bufferSize-res, &m_eoi);
      if( n==0 ) m_eoi = true;
#if DEBUG>0
      for(uint8_t i=0; i<n; i++) dbg_data(buffer[res+i]);
#endif
      res += n;
    }

  return res;
}


int8_t GPIBFileDevice::canWrite() 
{
#if DEBUG>2
  Serial.write('c');Serial.write('W');
#endif

  // see comment in GPIBFileDevice constructor
  if( !m_canServeATN )
    {
      // for IFD_OPEN, fileTask() resets the channel to 0xFF which is a problem when we call it from
      // here because we have already received the TALK after the UNLISTEN that
      // initiated the OPEN and so m_channel will not be set again => remember and restore it here
      if( m_cmd==IFD_OPEN )
        { uint8_t c = m_channel; fileTask(); m_channel = c; }
      else
        fileTask();
    }

  if( m_channel == 15 || m_opening )
    {
      return 1; // command channel or opening file
    }
  else if( m_channel > 15 || m_readBufferLen[m_channel]==-128 )
    {
      return 0; // invalid channel or OPEN failed
    }
  else
    {
      // if write buffer is full then send it on now
      if( m_writeBufferLen==GPIBFILEDEVICE_WRITE_BUFFER_SIZE-1 )
        emptyWriteBuffer();
      
      return (m_writeBufferLen<GPIBFILEDEVICE_WRITE_BUFFER_SIZE-1) ? 1 : 0;
    }
}


void GPIBFileDevice::write(uint8_t data, bool eoi) 
{
  // this function must return withitn 1 millisecond
  // => do not add Serial.print or function call that may take longer!
  // (at 115200 baud we can send 10 characters in less than 1 ms)

  m_eoi |= eoi;
  if( m_writeBufferLen<GPIBFILEDEVICE_WRITE_BUFFER_SIZE-1 )
    m_writeBuffer[m_writeBufferLen++] = data;
 
#if DEBUG>1
  Serial.write('W'); print_hex(data);
#endif
}


uint8_t GPIBFileDevice::write(uint8_t *buffer, uint8_t bufferSize, bool eoi)
{
  if( m_channel < 15 )
    {
      // first pass on data that has been buffered (if any), if that is not
      // possible then return indicating that nothing of the new data has been sent
      emptyWriteBuffer();
      if( m_writeBufferLen>0 ) return 0;

      // now pass on new data
      m_eoi |= eoi;
      uint8_t nn = write(m_channel, buffer, bufferSize, m_eoi);
#if DEBUG>0
      for(uint8_t i=0; i<nn; i++) dbg_data(buffer[i]);
#endif
      return nn;
    }
  else
    return 0;
}


void GPIBFileDevice::talk(uint8_t secondary)   
{
#if DEBUG>1
  Serial.write('T'); print_hex(secondary);
#endif

  m_channel = secondary & 0x0F;
  m_eoi = false;

  // Final Cartridge 3 sends TALK->CLOSE->UNLISTEN when
  // interrupting a DOS"$" command
  if( m_channel!=15 && (secondary & 0xF0) == 0xE0 )
    m_cmd = IFD_CLOSE;
}


void GPIBFileDevice::untalk() 
{
#if DEBUG>1
  Serial.write('t');
#endif

  // no current channel
  m_channel = 0xFF; 
}


void GPIBFileDevice::listen(uint8_t secondary) 
{
#if DEBUG>1
  Serial.write('L'); print_hex(secondary);
#endif
  m_channel = secondary & 0x0F;
  m_eoi = false;

  if( m_channel==15 )
    m_writeBufferLen = 0;
  else if( (secondary & 0xF0) == 0xF0 )
    {
      m_opening = true;
      m_writeBufferLen = 0;
    }
  else if( (secondary & 0xF0) == 0xE0 )
    {
      m_cmd = IFD_CLOSE;
    }
}


void GPIBFileDevice::unlisten() 
{
#if DEBUG>1
  Serial.write('l'); Serial.write('0'+m_channel);
#endif

  if( m_channel==15 )
    {
      if( m_writeBufferLen>0 ) m_cmd = IFD_EXEC;
      m_channel = 0xFF;
    }
  else if( m_opening )
    {
      m_opening = false;
      m_writeBuffer[m_writeBufferLen] = 0;
      m_cmd = IFD_OPEN;
      // m_channel gets set to 0xFF after IFD_OPEN is processed
    }
  else if( m_writeBufferLen>0 )
    {
      m_cmd = IFD_WRITE;
      // m_channel gets set to 0xFF after IFD_WRITE is processed
    }
}


#if defined(GPIB_FP_EPYX) && defined(GPIB_FP_EPYX_SECTOROPS)
bool GPIBFileDevice::epyxReadSector(uint8_t track, uint8_t sector, uint8_t *buffer)
{
#if DEBUG>0
  dbg_print_data();
  Serial.print("Read track "); Serial.print(track); Serial.print(" sector "); Serial.println(sector);
  for(int i=0; i<256; i++) dbg_data(buffer[i]);
  dbg_print_data();
  Serial.flush();
  return true;
#else
  return false;
#endif
}


bool GPIBFileDevice::epyxWriteSector(uint8_t track, uint8_t sector, uint8_t *buffer)
{
#if DEBUG>0
  dbg_print_data();
  Serial.print("Write track "); Serial.print(track); Serial.print(" sector "); Serial.println(sector); Serial.flush();
  for(int i=0; i<256; i++) dbg_data(buffer[i]);
  dbg_print_data();
  return true;
#else
  return false;
#endif
}
#endif


void GPIBFileDevice::fillReadBuffer()
{
  while( m_readBufferLen[m_channel]<2 && !m_eoi )
    {
      uint8_t n = 2-m_readBufferLen[m_channel];
      n = read(m_channel, m_readBuffer[m_channel]+m_readBufferLen[m_channel], n, &m_eoi);
      if( n==0 ) m_eoi = true;
#if DEBUG==1
      for(uint8_t i=0; i<n; i++) dbg_data(m_readBuffer[m_channel][m_readBufferLen[m_channel]+i]);
#endif
      m_readBufferLen[m_channel] += n;
    }
}


void GPIBFileDevice::emptyWriteBuffer()
{
  if( m_writeBufferLen>0 )
    {
      uint8_t n = write(m_channel, m_writeBuffer, m_writeBufferLen, m_eoi);
#if DEBUG==1
      for(uint8_t i=0; i<n; i++) dbg_data(m_writeBuffer[i]);
#endif
      if( n<m_writeBufferLen ) 
        {
          memmove(m_writeBuffer, m_writeBuffer+n, m_writeBufferLen-n);
          m_writeBufferLen -= n;
        }
      else
        m_writeBufferLen = 0;
    }
}


void GPIBFileDevice::clearReadBuffer(uint8_t channel)
{
  if( channel<16 ) m_readBufferLen[channel] = 0;
}


void GPIBFileDevice::fileTask()
{
#ifdef GPIB_FP_AR6
  if( m_cmd!=IFD_NONE )
    {
      // Unfortunately when writing a file, Action Replay 6 sends two extra bytes (0x00 and 0x01)
      // via the regular protocol BEFORE switching to the fast-save protocol. At that point we
      // don't know whether this will become an actual AR6 fast-save operation. 
      // But we can't wait whether maybe the actual fast-save sequence (M-W,M-E) will arrive later
      // because doing so could mess with regular "save" operations.
      // So we assume this is an AR6 fast-save if we detect the following sequence:
      // - memory read at 0xFFFE (we answer with "3" - identifying as 1581 drive)
      // - open file with channel number>0 (i.e. not loading)
      // - send exactly 0x00 and 0x01 in one LISTEN/UNLISTEN transaction as the first data for the file.
      // If this sequence is detected, we discard those extra bytes.
      if( m_ar6detect==0 && m_cmd==IFD_EXEC && strncmp_P((const char *) m_writeBuffer, PSTR("M-R\xfe\xff\x01"), 6)==0 )
        m_ar6detect = 1;
      else if( m_ar6detect==1 && m_cmd==IFD_OPEN && m_channel>0 )
        m_ar6detect = 2;
      else if( m_ar6detect==2 && m_cmd==IFD_WRITE && m_writeBufferLen==2 && m_writeBuffer[0]==0 && m_writeBuffer[1]==1 )
        {
          m_writeBufferLen = 0;
          m_cmd = IFD_NONE;
          m_ar6detect = 0;
        }
      else if( m_ar6detect!=0 )
        m_ar6detect = 0;
    }
#endif

  switch( m_cmd )
    {
    case IFD_OPEN:
      {
#if DEBUG>0
        for(uint8_t i=0; m_writeBuffer[i]; i++) dbg_data(m_writeBuffer[i]);
        dbg_print_data();
        Serial.print(F("OPEN #")); 
#if GPIB_MAX_DEVICES>1
        Serial.print(m_devnr); Serial.write('#');
#endif
        Serial.print(m_channel); Serial.print(F(": ")); Serial.println((const char *) m_writeBuffer);
#endif
        bool ok = open(m_channel, (const char *) m_writeBuffer);
        
        m_readBufferLen[m_channel] = ok ? 0 : -128;
        m_writeBufferLen = 0;
        m_channel = 0xFF; 
        break;
      }
      
    case IFD_CLOSE: 
      {
#if DEBUG>0
        dbg_print_data();
        Serial.print(F("CLOSE #")); 
#if GPIB_MAX_DEVICES>1
        Serial.print(m_devnr); Serial.write('#');
#endif
        Serial.println(m_channel);
#endif
        // note: any data that cannot be sent on at this point is lost!
        emptyWriteBuffer();
        m_writeBufferLen = 0;

        close(m_channel); 
        m_readBufferLen[m_channel] = 0;
        m_channel = 0xFF;
        break;
      }
      
    case IFD_WRITE:
      {
        // note: any data that cannot be sent on at this point is lost!
        emptyWriteBuffer();
        m_writeBufferLen = 0;
        m_channel = 0xFF;
        break;
      }

    case IFD_EXEC:  
      {
        bool handled = false;
        const char *cmd = (const char *) m_writeBuffer;

#if DEBUG>0
#ifdef GPIB_FP_DOLPHIN
        // Printing debug output here may delay our response to DolphinDos
        // 'XQ' and 'XZ' commands (burst mode request) too long and cause
        // the C64 to time out, causing the transmission to hang
        if( cmd[0]!='X' || (cmd[1]!='Q' && cmd[1]!='Z') )
#endif
          {
            for(uint8_t i=0; i<m_writeBufferLen; i++) dbg_data(m_writeBuffer[i]);
            dbg_print_data();
            Serial.print(F("EXECUTE: ")); Serial.println(cmd);
          }
#endif
#ifdef GPIB_FP_EPYX
        static const struct MWSignature epyxV1sig[2] PROGMEM   = 
          { {0x0180, 0x20, 0x2E}, {0x01A0, 0x20, 0xA5} };
        static const struct MWSignature epyxV2V3sig[3] PROGMEM = 
          { {0x0180, 0x19, 0x53}, {0x0199, 0x19, 0xA6}, {0x01B2, 0x19, 0x8F} };

        if( checkMWcmds(epyxV1sig, 2, 10) || checkMWcmds(epyxV2V3sig, 3, 20) )
          handled = true;
        else if( m_uploadCtr==12 && strncmp_P(cmd, PSTR("M-E\xa2\x01"), 5)==0 )
          m_uploadCtr = 99;
        else if( m_uploadCtr==23 && strncmp_P(cmd, PSTR("M-E\xa9\x01"), 5)==0 )
          m_uploadCtr = 99;

        if( m_uploadCtr==99 )
          {
#if DEBUG>0
            Serial.println(F("EPYX FASTLOAD DETECTED"));
#endif
            fastLoadRequest(GPIB_FP_EPYX, GPIB_FL_PROT_HEADER);
            m_uploadCtr = 0;
            handled = true;
          }
#endif
#ifdef GPIB_FP_AR6
        static const struct MWSignature ar6LoadSig[9] PROGMEM = 
          { {0x500,0x23,0x8c}, {0x523,0x23,0xa8}, {0x546,0x23,0x93}, {0x569,0x23,0x0f},
            {0x58c,0x23,0xea}, {0x5af,0x23,0x94}, {0x5d2,0x23,0x6a}, {0x5f5,0x23,0xb2},
            {0x618,0x23,0xbc} };

        static const struct MWSignature ar6SaveSig[12] PROGMEM = 
          { {0x59b,0x23,0xe3}, {0x5be,0x23,0x35}, {0x5e1,0x23,0x3c}, {0x604,0x23,0x2c},
            {0x627,0x23,0xe0}, {0x64a,0x23,0x0e}, {0x66d,0x23,0xd7}, {0x690,0x23,0x03},
            {0x6b3,0x23,0xb8}, {0x6d6,0x23,0xad}, {0x6f9,0x23,0x38}, {0x71c,0x23,0x8c} };

        // Action Replay 6 reads $FFFE to determine drive type. We return 3 which identifies
        // us as a 1581 drive. The 1581 fastloader is very much more suited for our needs.
        // The 1541 fastloader transfers the whole directory track (18) to the C64 and then
        // the C64 finds the file to load, i.e. it never actually transmits the name of the
        // file to load to the drive, which obviously can't work for us here.
        if( strncmp_P(cmd, PSTR("M-R\xfe\xff\x01"), 6)==0 )
          {
            // returning 0x03 when reading $FFFE identifies this as a 1581 drive
            m_statusBuffer[0] = 0x03;
            m_statusBufferLen = 1;
            handled = true;
          }
        else if( checkMWcmds(ar6LoadSig, 9, 180) )
          handled = true;
        else if( checkMWcmds(ar6SaveSig, 12, 190) )
          handled = true;
        else if( m_uploadCtr==189 && strncmp_P(cmd, PSTR("M-E\x00\x05"), 5)==0 )
          {
#if DEBUG>0
            Serial.println(F("ACTION REPLAY 6 FASTLOAD DETECTED"));
#endif
            fastLoadRequest(GPIB_FP_AR6, GPIB_FL_PROT_LOAD);
            m_uploadCtr = 0;
            handled = true;
            m_eoi = false;
            m_channel = 0;
          }
        else if( m_uploadCtr==202 && strncmp_P(cmd, PSTR("M-E\xf4\x05"), 5)==0 )
          {
#if DEBUG>0
            Serial.println(F("ACTION REPLAY 6 FASTSAVE DETECTED"));
#endif
            fastLoadRequest(GPIB_FP_AR6, GPIB_FL_PROT_SAVE);
            m_uploadCtr = 0;
            handled = true;
            m_eoi = false;
            m_channel = 1;
          }
#endif
#ifdef GPIB_FP_FC3
        static const struct MWSignature fc3LoadSig[16] PROGMEM =
          { {0x400,0x20,0xb0}, {0x420,0x20,0x6a}, {0x440,0x20,0x51}, {0x460,0x20,0x0a},
            {0x480,0x20,0x61}, {0x4a0,0x20,0x9b}, {0x4c0,0x20,0x18}, {0x4e0,0x20,0x0b},
            {0x500,0x20,0xb7}, {0x520,0x20,0xb7}, {0x540,0x20,0xf7}, {0x560,0x20,0x8f},
            {0x580,0x20,0x6b}, {0x5a0,0x20,0x5d}, {0x5c0,0x20,0x7b}, {0x5e0,0x20,0x52} };

        static const struct MWSignature fc3LoadImageSig[16] PROGMEM =
          { {0x400,0x20,0x23}, {0x420,0x20,0xfc}, {0x440,0x20,0x25}, {0x460,0x20,0xcd},
            {0x480,0x20,0x4d}, {0x4a0,0x20,0xc2}, {0x4c0,0x20,0x9e}, {0x4e0,0x20,0x5a},
            {0x500,0x20,0xc9}, {0x520,0x20,0xb2}, {0x540,0x20,0x5c}, {0x560,0x20,0x8a},
            {0x580,0x20,0xc5}, {0x5a0,0x20,0x02}, {0x5c0,0x20,0xae}, {0x5e0,0x20,0x2d} };

        static const struct MWSignature fc3SaveSig[16] PROGMEM =
          { {0x500,0x20,0x16}, {0x520,0x20,0x7e}, {0x540,0x20,0xe1}, {0x560,0x20,0xd8},
            {0x580,0x20,0xf2}, {0x5a0,0x20,0xe2}, {0x5c0,0x20,0xa3}, {0x5e0,0x20,0x2e},
            {0x600,0x20,0x27}, {0x620,0x20,0x09}, {0x640,0x20,0xfc}, {0x660,0x20,0x8e},
            {0x680,0x20,0xaf}, {0x6a0,0x20,0xe0}, {0x6c0,0x20,0xc9}, {0x6e0,0x20,0xa4} };

        if( checkMWcmds(fc3LoadSig, 16, 120) )
          handled = true;
        else if( checkMWcmds(fc3LoadImageSig, 16, 140) )
          handled = true;
        else if( checkMWcmds(fc3SaveSig, 16, 160) )
          handled = true;
        else if( m_uploadCtr==136 && strncmp_P(cmd, PSTR("M-E\x9a\x05"), 5)==0 )
          {
#if DEBUG>0
            Serial.println(F("FINAL CARTRIDGE 3 FASTLOAD DETECTED"));
#endif
            fastLoadRequest(GPIB_FP_FC3, GPIB_FL_PROT_LOAD);
            m_uploadCtr = 0;
            handled = true;
            m_eoi = false;
            m_channel = 0;
          }
        else if( m_uploadCtr==156 && strncmp_P(cmd, PSTR("M-E\x03\x04"), 5)==0 )
          {
#if DEBUG>0
            Serial.println(F("FINAL CARTRIDGE 3 SNAPSHOT FASTLOAD DETECTED"));
#endif
            fastLoadRequest(GPIB_FP_FC3, GPIB_FL_PROT_LOADIMG);
            m_uploadCtr = 0;
            handled = true;
            m_eoi = false;
            m_channel = 0;
          }
        else if( m_uploadCtr==176 && (strncmp_P(cmd, PSTR("M-E\x9c\x05"), 5)==0 || strncmp_P(cmd, PSTR("M-E\xaf\x05"), 5)==0) )
          {
            // 059c entry address for PAL, 05af for NTSC (slightly different timing)
#if DEBUG>0
            Serial.println(F("FINAL CARTRIDGE 3 FASTSAVE DETECTED"));
#endif
            fastLoadRequest(GPIB_FP_FC3, GPIB_FL_PROT_SAVE);
            m_uploadCtr = 0;
            handled = true;
            m_eoi = false;
            m_channel = 1;
          }
#endif
#ifdef GPIB_FP_SPEEDDOS
        static const struct MWSignature speedDosLoadSig[18] PROGMEM =
          { {0x300,0x1e,0xc9}, {0x31e,0x1e,0xe9}, {0x33c,0x1e,0xb9}, {0x35a,0x1e,0x4d},
            {0x378,0x1e,0x5c}, {0x396,0x1e,0x96}, {0x3b4,0x1e,0x39}, {0x3d2,0x1e,0xde},
            {0x3f0,0x1e,0x0d}, {0x40e,0x1e,0xaf}, {0x42c,0x1e,0x1e}, {0x44a,0x1e,0xf6},
            {0x468,0x1e,0xd2}, {0x486,0x1e,0x1b}, {0x4a4,0x1e,0x5f}, {0x4c2,0x1e,0x96},
            {0x4e0,0x1e,0x53}, {0x4fe,0x1e,0x16} };

        if( checkMWcmds(speedDosLoadSig, 18, 100) )
          handled = true;
        else if( m_uploadCtr==118 && strncmp_P(cmd, PSTR("M-E\x03\x03"), 5)==0 )
          {
#if DEBUG>0
            Serial.println(F("SPEEDDOS FASTLOAD DETECTED"));
#endif
            fastLoadRequest(GPIB_FP_SPEEDDOS, GPIB_FL_PROT_LOAD);
            m_uploadCtr = 0;
            handled = true;
            m_eoi = false;
            m_channel = 0;
          }
#endif
#ifdef GPIB_FP_DOLPHIN
        if( m_writeBufferLen==2 && strncmp_P(cmd, PSTR("XQ"), 2)==0 )
          { fastLoadRequest(GPIB_FP_DOLPHIN, GPIB_FL_PROT_LOAD);
            m_channel = 0; handled = true; m_eoi = false; }
        else if( m_writeBufferLen==2 && strncmp_P(cmd, PSTR("XZ"), 2)==0 )
          { fastLoadRequest(GPIB_FP_DOLPHIN, GPIB_FL_PROT_SAVE);
            m_channel = 1; handled = true; m_eoi = false; }
        else if( m_writeBufferLen==3 && strncmp_P(cmd, PSTR("XF+"), 2)==0 )
          { enableDolphinBurstMode(true); setStatus(NULL, 0); handled = true; }
        else if( m_writeBufferLen==3 && strncmp_P(cmd, PSTR("XF-"), 2)==0 )
          { enableDolphinBurstMode(false); setStatus(NULL, 0); handled = true; }
#endif
        if( !handled )
          {
            if( m_writeBuffer[m_writeBufferLen-1]==13 ) m_writeBufferLen--;
            m_writeBuffer[m_writeBufferLen]=0;
            execute(cmd, m_writeBufferLen);
            m_uploadCtr = 0;
          }

        m_writeBufferLen = 0;
        break;
      }
    }

  m_cmd = IFD_NONE;
}


bool GPIBFileDevice::checkMWcmds(const struct MWSignature *sig, uint8_t sigLen, uint8_t offset)
{
  if( m_uploadCtr==0 && 
      checkMWcmd(pgm_read_word_near(&(sig[0].address)),
                 pgm_read_byte_near(&(sig[0].len)),
                 pgm_read_byte_near(&(sig[0].checksum))) )
    {
      m_uploadCtr = offset+1;
      return true;
    }
  else if( m_uploadCtr >= offset && m_uploadCtr < offset+sigLen && 
           checkMWcmd(pgm_read_word_near(&(sig[m_uploadCtr-offset].address)),
                      pgm_read_byte_near(&(sig[m_uploadCtr-offset].len)),
                      pgm_read_byte_near(&(sig[m_uploadCtr-offset].checksum))) )
    {
      m_uploadCtr++;
      return true;
    }
  else
    return false;
}


bool GPIBFileDevice::checkMWcmd(uint16_t addr, uint8_t len, uint8_t checksum) const
{
  // check buffer length and M-W command
  if( m_writeBufferLen<len+6 || strncmp_P((const char *) m_writeBuffer, PSTR("M-W"), 3)!=0 )
    return false;

  // check data length and destination address
  if( m_writeBuffer[3]!=(addr&0xFF) || m_writeBuffer[4]!=((addr>>8)&0xFF) || m_writeBuffer[5]!=len )
    return false;

  // check checksum
  uint8_t c = 0;
  for(uint8_t i=0; i<len; i++) c += m_writeBuffer[6+i];

  return c==checksum;
}


void GPIBFileDevice::setStatus(const char *data, uint8_t dataLen)
{
#if DEBUG>0
  Serial.print(F("SETSTATUS ")); 
#if GPIB_MAX_DEVICES>1
  Serial.write('#'); Serial.print(m_devnr); Serial.write(' ');
#endif
  Serial.println(dataLen);
#endif

  m_statusBufferPtr = 0;
  m_statusBufferLen = min((uint8_t) GPIBFILEDEVICE_STATUS_BUFFER_SIZE, dataLen);
  memcpy(m_statusBuffer, data, m_statusBufferLen);
}


void GPIBFileDevice::clearStatus() 
{ 
  setStatus(NULL, 0); 
}


void GPIBFileDevice::reset()
{
#if DEBUG>0
#if GPIB_MAX_DEVICES>1
  Serial.write('#'); Serial.print(m_devnr); Serial.write(' ');
#endif
  Serial.println(F("RESET"));
#endif

  m_statusBufferPtr = 0;
  m_statusBufferLen = 0;
  m_writeBufferLen = 0;
  memset(m_readBufferLen, 0, 15);
  m_channel = 0xFF;
  m_cmd = IFD_NONE;
  m_opening = false;
  m_uploadCtr = 0;

  GPIBDevice::reset();
}


void GPIBFileDevice::task()
{
  // see comment in GPIBFileDevice constructor
  if( m_canServeATN ) fileTask();
}
