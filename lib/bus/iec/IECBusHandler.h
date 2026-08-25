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

#ifndef IECBUSHANDLER_H
#define IECBUSHANDLER_H

#include "IECConfig.h"
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

// fastload sub-protocols (used in IECFileDevice)
#define IEC_FL_PROT_NONE    0
#define IEC_FL_PROT_LOAD    1
#define IEC_FL_PROT_SAVE    2
#define IEC_FL_PROT_HEADER  3
#define IEC_FL_PROT_SECTOR  4
#define IEC_FL_PROT_LOADIMG 5

class IECDevice;

class IECBusHandler
{
 public:
  // pinATN should preferrably be a pin that can handle external interrupts
  // (e.g. 2 or 3 on the Arduino UNO), if not then make sure the task() function
  // gets called at least once evey millisecond, otherwise "device not present" 
  // errors may result
#ifdef IEC_USE_LINE_DRIVERS
  IECBusHandler(uint8_t pinATN, uint8_t pinCLKin, uint8_t pinCLKout, uint8_t pinDATAin, uint8_t pinDATAout, uint8_t pinRESET = 0xFF, uint8_t pinCTRL = 0xFF, uint8_t pinSRQ = 0xFF);
#else
  IECBusHandler(uint8_t pinATN, uint8_t pinCLK, uint8_t pinDATA, uint8_t pinRESET = 0xFF, uint8_t pinCTRL = 0xFF, uint8_t pinSRQ = 0xFF);
#endif

  // must be called once at startup before the first call to "task", devnr
  // is the IEC bus device number that this device should react to
  void begin();

  // releases the bus (CLK/DATA lines and ATN interrupt) and makes task()
  // a no-op until begin() is called again. Attached devices are left in
  // place; begin() re-initializes them the same way it did at startup.
  void end();

  bool attachDevice(IECDevice *dev);
  bool detachDevice(IECDevice *dev);

  // task must be called periodically to handle IEC bus communication
  // if the ATN signal is NOT on an interrupt-capable pin then task() must be
  // called at least once every millisecond, otherwise less frequent calls are
  // ok but bus communication will be slower if called less frequently.
  void task();

#if !defined(IEC_DEFAULT_FASTLOAD_BUFFER_SIZE)
  // if IEC_DEFAULT_FASTLOAD_BUFFER_SIZE is set to 0 then the buffer space used
  // by fastload protocols can be set dynamically using the setBuffer function.
  void setBuffer(uint8_t *buffer, uint8_t bufferSize);
#endif

  // bit-mask of supported fast loaders. 32 bits wide because the loader ids
  // (IEC_FP_* in IECConfig.h) go well past 8 once the software loaders are
  // included -- a uint8_t mask made every id >7 fail isFastLoaderSupported()
  // silently.
  static uint32_t getSupportedFastLoaders();
  static bool isFastLoaderSupported(uint8_t loader);
  bool enableFastLoader(IECDevice *dev, uint8_t protocol, bool enable);
  void fastLoadRequest(IECDevice *dev, uint8_t loader, uint8_t request);

#ifdef IEC_SUPPORT_SOFTLOAD
  // Switch in a software fast loader that has just been identified from the
  // code the computer uploaded -- called by IECFileDevice::startFastLoader.
  // "cmd"/"cmdLen" are the whole M-E command, which for some loaders carries
  // arguments after the address. Returns false for a loader that is detected
  // but has no implementation here, which leaves the computer free to fall
  // back to the standard protocol.
  bool runFastLoader(IECDevice *dev, uint8_t variant, uint8_t param, uint8_t rxtx, const uint8_t *cmd, uint8_t cmdLen, const uint8_t *captured);
#endif

#ifdef IEC_FP_DOLPHIN
  void enableDolphinBurstMode(IECDevice *dev, bool enable);
#endif

#ifdef IEC_SUPPORT_PARALLEL
  // call this BEFORE begin() if you do not want to use the default pins for the parallel cable
#ifdef IEC_SUPPORT_PARALLEL_XRA1405
  void setParallelPins(uint8_t pinHT, uint8_t pinHR, uint8_t pinSCK, uint8_t pinCOPI, uint8_t pinCIPO, uint8_t pinCS);
#else
  void setParallelPins(uint8_t pinHT, uint8_t pinHR, uint8_t pinD0, uint8_t pinD1, uint8_t pinD2, uint8_t pinD3, 
                         uint8_t pinD4, uint8_t pinD5, uint8_t pinD6, uint8_t pinD7);
#endif
#endif

  IECDevice *findDevice(uint8_t devnr, bool includeInactive = false);
  bool canServeATN();
  bool inTransaction();

  // True if the RESET pin currently reads idle (not asserted), or if this
  // board has no RESET pin wired at all. NOT declared inline (unlike the
  // private readPin* helpers) so it reliably links when called from other
  // translation units, e.g. IECHost.
  bool isResetPinIdle();
  void sendSRQ();
  void setATNInterruptEnabled(bool enable);
  bool isATNInterruptEnabled() const { return m_atnInterruptEnabled; }
  void setHostMode(bool enable) { m_hostMode = enable; }
  bool isHostMode() const { return m_hostMode; }
  bool isEnabled() const { return m_enabled; }

  IECDevice *m_currentDevice;
  IECDevice *m_devices[IEC_MAX_DEVICES];

  uint8_t m_numDevices;
  int  m_atnInterrupt;
  uint8_t m_pinATN, m_pinCLK, m_pinDATA, m_pinRESET, m_pinSRQ, m_pinCTRL;
#ifdef IEC_USE_LINE_DRIVERS
  uint8_t m_pinCLKout, m_pinDATAout;
#endif

  // IECHost acts as bus master and needs direct access to the low-level
  // pin control, wait, and timing methods.
  friend class IECHost;

 private:
  bool readPinATN();
  bool readPinCLK();
  bool readPinDATA();
  bool readPinRESET();
  void writePinCLK(bool v);
  void writePinDATA(bool v);
  void writePinCTRL(bool v);
  bool waitTimeout(uint16_t timeout, uint8_t cond = 0);
  bool waitPinDATA(bool state, uint16_t timeout = 1000);
  bool waitPinCLK(bool state, uint16_t timeout = 1000);
  void waitPinATN(bool state);
  void attachATNInterrupt();
  void atnRequest();
  bool receiveIECByteATN(uint8_t &data, uint8_t bytenum);
  bool receiveIECByte(bool canWriteOk);
  bool transmitIECByte(uint8_t numData);
  void handleFastLoadProtocols();
  void handleATNSequence();

  volatile uint16_t m_timeoutDuration;
  volatile uint32_t m_timeoutStart;
  volatile bool m_inTask;
  volatile bool m_hostMode;
  bool m_atnInterruptEnabled;
  volatile uint8_t m_flags;
  uint8_t m_primary, m_secondary;

  // Dedicated enable/disable latch for end()/begin(), checked by task() before
  // touching anything else.
  volatile bool m_enabled;

#ifdef IOREG_TYPE
  volatile IOREG_TYPE *m_regCLKwrite, *m_regCLKmode, *m_regDATAwrite, *m_regDATAmode;
  volatile const IOREG_TYPE *m_regATNread, *m_regCLKread, *m_regDATAread, *m_regRESETread;
  IOREG_TYPE m_bitATN, m_bitCLK, m_bitDATA, m_bitRESET;
#ifdef IEC_USE_LINE_DRIVERS
  IOREG_TYPE m_bitCLKout, m_bitDATAout;
#endif
#endif

#ifdef IEC_FP_JIFFY 
  bool receiveJiffyByte(bool canWriteOk);
  bool transmitJiffyByte(uint8_t numData);
  bool transmitJiffyBlock(uint8_t *buffer, uint8_t numBytes);
#endif

#ifdef IEC_FP_SPEEDDOS
  bool transmitSpeedDosByte(uint8_t numData);
  bool receiveSpeedDosByte(bool canWriteOk);
  bool transmitSpeedDosFile();
  bool transmitSpeedDosParallelByte(uint8_t data);
#endif


#ifdef IEC_FP_DOLPHIN
  bool transmitDolphinByte(uint8_t numData);
  bool receiveDolphinByte(bool canWriteOk);
  bool transmitDolphinBurst();
  bool receiveDolphinBurst();
#endif

#ifdef IEC_SUPPORT_PARALLEL
  void startParallelTransaction();
  void endParallelTransaction();
  bool parallelBusHandshakeReceived();
  bool waitParallelBusHandshakeReceived();
  bool waitParallelBusHandshakeReceivedISafe(bool exitOnCLKchange = false);
  void parallelBusHandshakeTransmit();
  void setParallelBusModeInput();
  void setParallelBusModeOutput();
  uint8_t readParallelData();
  void writeParallelData(uint8_t data);
  bool checkParallelPins();
  void enableParallelPins();
  bool isParallelPin(uint8_t pin);

#ifdef IEC_SUPPORT_PARALLEL_XRA1405
  uint8_t m_pinParallelSCK, m_pinParallelCOPI, m_pinParallelCIPO, m_pinParallelCS, m_inTransaction;
  uint8_t XRA1405_ReadReg(uint8_t reg);
  void    XRA1405_WriteReg(uint8_t reg, uint8_t data);

#ifdef IOREG_TYPE
  volatile IOREG_TYPE *m_regParallelCS;
  IOREG_TYPE m_bitParallelCS;
#endif

#else // !IEC_SUPPORT_PARALLEL_XRA1405

  uint8_t m_pinParallel[8];
#ifdef IOREG_TYPE
  volatile IOREG_TYPE *m_regParallelMode[8], *m_regParallelWrite[8];
  volatile const IOREG_TYPE *m_regParallelRead[8];
  IOREG_TYPE m_bitParallel[8];
#endif

#endif // IEC_SUPPORT_PARALLEL_XRA1405

  uint8_t m_pinParallelHandshakeTransmit;
  uint8_t m_pinParallelHandshakeReceive;
  uint8_t m_bufferCtr;

#ifdef IOREG_TYPE
  volatile IOREG_TYPE *m_regParallelHandshakeTransmitMode;
  IOREG_TYPE m_bitParallelHandshakeTransmit;
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega2560__)
  IOREG_TYPE m_bitParallelhandshakeReceived = 0;
#endif
#endif // IOREG_TYPE
#endif // IEC_SUPPORT_PARALLEL

#ifdef IEC_FP_EPYX
  bool receiveEpyxByte(uint8_t &data);
  bool transmitEpyxByte(uint8_t data);
  bool receiveEpyxHeader();
  bool transmitEpyxBlock();
#ifdef IEC_FP_EPYX_SECTOROPS
  bool startEpyxSectorCommand(uint8_t command);
  bool finishEpyxSectorCommand();
#endif
#endif

#ifdef IEC_FP_FC3
  void transmitFC3Bytes(uint8_t *data);
  bool receiveFC3Byte(uint8_t *data);
  int8_t transmitFC3Block();
  int8_t transmitFC3ImageBlock();
  int8_t receiveFC3Block();
#endif
  
#ifdef IEC_FP_AR6
  bool transmitAR6Byte(uint8_t data, bool ar6Protocol);
  bool receiveAR6Byte(uint8_t *data);
  int8_t transmitAR6Block(bool ar6Protocol);
  int8_t receiveAR6Block();
#endif

#ifdef IEC_FP_HYPRALOAD
  bool transmitHypraLoadByte(uint8_t data);
  bool transmitHypraLoadBlock();
#endif

#if (defined(IEC_FP_ULOAD3) || defined(IEC_FP_ELOAD1)) && defined(IEC_IMPL_SOFTLOAD)
  // shared bit protocol -- ELoad1 sends and receives bytes exactly as ULoad3 does
  bool uload3Handshake();
  bool receiveULoad3Byte(uint8_t &data);
  bool transmitULoad3Byte(uint8_t value);
#endif

#if defined(IEC_FP_ULOAD3) && defined(IEC_IMPL_SOFTLOAD)
  bool uload3TransferChain(uint8_t track, uint8_t sector, bool saving);
  bool runULoad3Loader();
#endif

#if defined(IEC_FP_ELOAD1) && defined(IEC_IMPL_SOFTLOAD)
  bool runELoad1Loader();
#endif

#if defined(IEC_FP_NIPPON) && defined(IEC_IMPL_SOFTLOAD)
  bool nipponAbort();
  bool nipponWait(bool clkNotAtn, bool state);
  bool nipponHandshake();
  bool receiveNipponByte(uint8_t &data);
  bool transmitNipponByte(uint8_t value);
  bool runNipponLoader();
#endif

#if defined(IEC_FP_MMZAK) && defined(IEC_IMPL_SOFTLOAD)
  bool mmzakWaitCLK(bool state);
  bool transmitMMZakByte(uint8_t value);
  bool receiveMMZakByte(uint8_t &data);
  bool transmitMMZakError();
  bool mmzakReadSector(uint8_t track, uint8_t sector);
  bool mmzakWriteSector(uint8_t track, uint8_t sector);
  bool runMMZakLoader();
#endif

#if defined(IEC_FP_BITFIRE) && defined(IEC_IMPL_SOFTLOAD)
  // Bitfire keeps its per-session state in one struct: thirteen revisions
  // differ in directory layout and block header, not in the bit protocol.
  struct BitfireSession {
    uint8_t variant, dirSector, interleave, nextFile, track, sector, offset;
    uint16_t fileCrc;
    const uint8_t *hdr;
  };

  bool receiveBitfireByte(uint8_t rxtx, uint8_t &data);
  bool bitfireLoadDrivecode(uint8_t variant);
  bool bitfireLoadDir(uint8_t *dirBuf, uint8_t sector);
  void bitfireIterateSector(BitfireSession &s);
  void bitfireIterateFile(BitfireSession &s, const uint8_t *dirBuf, uint8_t file);
  void bitfireDirEntry(BitfireSession &s, const uint8_t *dirBuf, uint8_t i,
                       uint16_t &addr, uint16_t &length);
  bool bitfireLoadFile(BitfireSession &s, uint8_t *dirBuf, uint8_t file);
  bool runBitfireLoader(uint8_t rxtx, uint8_t variant, uint8_t proto);
#endif

#if defined(IEC_FP_ULTRABOOT) && defined(IEC_IMPL_SOFTLOAD)
  void ultrabootMapSector(uint8_t &track, uint8_t &sector, uint8_t speedzone);
  bool transmitUltrabootByte(uint8_t value);
  bool ultrabootDetect(const uint8_t *cmd, uint8_t cmdLen,
                       uint8_t &track, uint8_t &sector, uint8_t &speedzone);
  bool runUltrabootLoader(const uint8_t *cmd, uint8_t cmdLen);
#endif

#if defined(IEC_FP_KRILL) && defined(IEC_IMPL_SOFTLOAD)
  bool krillReadByte(uint8_t rxtx, uint8_t &data);
  bool krillSendByte(uint8_t rxtx, uint8_t b);
  bool krillLoadDrivecode(uint8_t rxtx);
  int16_t krillReadFilename(uint8_t rxtx, uint8_t variant, char *name,
                            uint8_t maxLen, bool firstFile);
  void krillBlockHeader(uint8_t variant, uint8_t bi, uint8_t lastUsed,
                        bool eoi, uint8_t hd[2]);
  bool krillSendFile(uint8_t rxtx, uint8_t variant, const char *name,
                     bool byTS, uint16_t &fileCrc);
  bool runKrillLoader(uint8_t rxtx, uint8_t variant,
                      const uint8_t *cmd, uint8_t cmdLen);
#endif

#if defined(IEC_FP_SPARKLE) && defined(IEC_IMPL_SOFTLOAD)
  // Sparkle: almost nothing is fixed, so the session carries the disk's
  // parameters, the byte encoding and the four production quirks.
  enum { SPK_ENC_NONE, SPK_ENC_20, SPK_ENC_21, SPK_ENC_21FF };

  struct SparkleSession {
    uint8_t variant, enc;
    uint8_t bundleLen, track, sector;
    uint8_t currentIl, numSectors, used[3], remaining;
    uint8_t interleave[4], prodId[3], nextId;
    uint8_t currentDir;
    bool    hasSaver, saveActive;
    bool    hasSkew, hasNsreset, bundleInv, fullSubsct;
    bool    dirReversed, dirLayoutKnown;
  };

  static uint8_t sparkleDecode(uint8_t enc, uint8_t v);
  uint8_t sparkleParam(SparkleSession &s, const uint8_t *bam, uint8_t pm);
  void sparkleDecodeBlock(SparkleSession &s, uint8_t *data);
  bool sparkleLoadDir(SparkleSession &s, uint8_t *dirBuf, uint8_t dirIndex);
  void sparkleAdvanceSector(SparkleSession &s, uint8_t ds);
  uint8_t sparkleIterateSector(SparkleSession &s);
  void sparkleTrackChanged(SparkleSession &s);
  bool sparkleInitDisk(SparkleSession &s, uint8_t *dirBuf, uint16_t bootCrc);
  bool sparkleFindDirEntry(SparkleSession &s, uint8_t *dirBuf, uint8_t bundle, uint8_t &bptr);
  bool sparkleReadByte(uint8_t &data, uint32_t timeoutMs);
  bool sparkleSendBundle(SparkleSession &s, uint8_t *dirBuf, uint8_t bundle);
  bool sparkleHandleSave(SparkleSession &s);
  bool runSparkleLoader(const uint8_t *cmd, uint8_t cmdLen);
#endif

#if defined(IEC_FP_SPINDLE) && defined(IEC_IMPL_SOFTLOAD)
  bool spindleWriteByte(uint8_t b, uint32_t timeoutMs);
  uint8_t spindleDetectVersion(const uint8_t *initSector);
  bool spindleSendBlock(uint8_t variant, uint8_t *nextCmd);
  bool runSpindleLoader(const uint8_t *cmd, uint8_t cmdLen);

  // Spindle 3.x, in protocol/spindle3.cpp. It shares only the sector bitmap
  // with 2.x; a sector is cut into length-prefixed UNITS walked backwards, and
  // the computer can interrupt to request a job by number.
  struct Spindle3Session {
    uint8_t  cmd[3], nextCmd[3], nextId[3];
    uint8_t  ppUnits[0x60];
    uint8_t  track, blockDelay;
    uint16_t jobCrc;
    bool     initDone, async;
  };

  bool spindleV3WriteByte(uint8_t b, uint32_t timeoutMs);
  uint8_t spindleReceiveJobNo();
  uint8_t spindleCopyCR(Spindle3Session &s);
  bool spindleSendUnits(Spindle3Session &s, uint8_t pos, bool pp);
  bool runSpindleV3Loader();
#endif

#if (defined(IEC_FP_BOOZE) || defined(IEC_FP_BITFIRE) || defined(IEC_FP_SPINDLE) || defined(IEC_FP_SPARKLE) || defined(IEC_FP_KRILL)) && defined(IEC_IMPL_SOFTLOAD)
  // shared by Booze and Bitfire, which move bytes the same way
  bool fastWaitATN(bool state, uint32_t timeoutMs);
  bool clockedWriteByte(uint8_t b, uint32_t timeoutMs);
#endif

#if defined(IEC_FP_BOOZE) && defined(IEC_IMPL_SOFTLOAD)
  bool receiveBoozeByte(uint8_t &data);
  bool boozeSendBlock(const uint8_t *data, uint8_t from, uint16_t *crc);
  bool boozeSendFile(uint16_t &fileCrc);
  void boozeBusLock();
  bool boozeFindDir(uint8_t &dirSector, uint8_t *dirBuf);
  bool runBoozeLoader();
#endif

#if defined(IEC_FP_DREAMLOAD) && defined(IEC_IMPL_SOFTLOAD)
  bool dreamloadWait(bool atnNotCLK, bool state);
  bool receiveDreamloadByte(uint8_t &data);
  bool transmitDreamloadByte(uint8_t value);
  bool dreamloadSendBlock(const uint8_t *data);
  bool runDreamloadLoader();
#endif

#if defined(IEC_FP_GEOS) && defined(IEC_IMPL_SOFTLOAD)
  // GEOS/Wheels byte layer. The session loops are not ported yet -- see
  // protocol/geos.cpp.
  bool geosWaitByteStart();
  bool geosSendByteCommon();
  bool receiveGeosByte(uint8_t rxtx, uint8_t &data);
  bool transmitGeosByte(uint8_t rxtx, uint8_t value);
  // session layer, protocol/geos_session.cpp
  bool geosWaitCLK(bool state);
  bool geosTransmitByteWait(uint8_t rxtx, uint8_t byte);
  bool geosTransmitBuffer(uint8_t rxtx, const uint8_t *data, uint16_t len);
  bool geosReceiveBuffer(uint8_t rxtx, uint8_t *data, uint16_t len);
  bool geosReceiveLenBlock(uint8_t rxtx, uint8_t *data);
  bool geosTransmitStatus(uint8_t rxtx, bool ok);
  bool geosSendChain(uint8_t rxtx, uint8_t track, uint8_t sector, const uint8_t *key);
  bool runGeosLoader(uint8_t rxtx, uint8_t variant);
  bool runGeosStage1Loader(uint8_t rxtx, uint8_t variant, const uint8_t *key);
#ifdef IEC_FP_WHEELS
  bool wheelsTransmitBuffer(uint8_t rxtx, uint8_t variant, const uint8_t *data, uint16_t len);
  bool wheelsTransmitByteWait(uint8_t rxtx, uint8_t variant, uint8_t byte);
  bool wheelsReceiveBuffer(uint8_t rxtx, uint8_t variant, uint8_t *data, uint16_t len);
  bool runWheelsStage1Loader(uint8_t rxtx, uint8_t variant);
  bool runWheelsStage2Loader(uint8_t rxtx, uint8_t variant);
#endif
#endif

#if defined(IEC_FP_FC3) && defined(IEC_IMPL_SOFTLOAD)
  bool transmitFC3OldFreezeByte(uint8_t value, bool ntsc);
  bool runFC3OldFreezeLoader(uint8_t rxtx);
#endif

#if defined(IEC_FP_SAMSJOURNEY) && defined(IEC_IMPL_SOFTLOAD)
  bool samsWaitATN(bool state);
  bool receiveSamsByte(uint8_t &data);
  bool transmitSamsByte(uint8_t value);
  bool transmitSamsBlock(uint8_t marker, uint8_t length, const uint8_t *data);
  bool samsOpenByName(uint8_t channel, uint8_t name, bool replace);
  bool samsReadFile(uint8_t name);
  bool samsWriteFile(uint8_t name, bool &aborted);
  bool samsScanDirectory();
  bool runSamsJourneyLoader();
  static uint8_t samsNameFromTS(uint8_t track, uint8_t sector);
  static uint8_t samsHexPairToByte(const char *s, uint8_t len);
#endif

#if defined(IEC_FP_N0SDOS) && defined(IEC_IMPL_SOFTLOAD)
  bool receiveN0SDOSByte(uint8_t &data);
  bool transmitN0SDOSByte(uint8_t value);
  bool runN0SDOSLoader();
#endif

#if defined(IEC_FP_GIJOE) && defined(IEC_IMPL_SOFTLOAD)
  bool gijoeAbort();
  bool gijoeWaitCLK(bool state);
  bool receiveGIJoeByte(uint8_t &data);
  bool transmitGIJoeByte(uint8_t value);
  bool transmitGIJoeData(uint8_t value);
  bool transmitGIJoeError();
  bool runGIJoeLoader();
#endif

#ifdef IEC_FP_TURBODISK
  bool waitTurbodiskHandshake();
  bool transmitTurbodiskByte(uint8_t data);
  bool transmitTurbodiskBuffer(const uint8_t *data, uint8_t len);
  bool transmitTurbodiskBlock();
  bool startTurbodiskLoad(const uint8_t *cmd, uint8_t cmdLen);
#endif

#if defined(IEC_SUPPORT_FASTLOAD)
  uint8_t m_bufferSize;
#if IEC_DEFAULT_FASTLOAD_BUFFER_SIZE>0
#if defined(IEC_FP_FC3)
  uint8_t  m_buffer[260];
#elif (defined(IEC_FP_EPYX) && defined(IEC_FP_EPYX_SECTOROPS)) || defined(IEC_FP_AR6) || defined(IEC_FP_HYPRALOAD) || defined(IEC_FP_TURBODISK)
  uint8_t  m_buffer[256];
#else
  uint8_t  m_buffer[IEC_DEFAULT_FASTLOAD_BUFFER_SIZE];
#endif
#else
  uint8_t *m_buffer;
#endif
#endif

  static IECBusHandler *s_bushandler;
  static void atnInterruptFcn(INTERRUPT_FCN_ARG);
};

#endif
