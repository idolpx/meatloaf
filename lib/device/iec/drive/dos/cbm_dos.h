#ifndef IEC_DOS_CBM_DOS_H
#define IEC_DOS_CBM_DOS_H

// The command channel. execute() tries four groups in order, the first that
// consumes the command wins:
//
//   cmdMeatloafPrefix()    meatloaf_dos.cpp  Meatloaf commands that must win
//                                            over a CBM one with the same letters
//   cmdCbm()               cbm_dos.cpp       CBM DOS 2.6 / 1581
//   cmdSd2iec()            sd2iec_dos.cpp    SD2IEC and CMD extensions
//   cmdMeatloafExtended()  meatloaf_dos.cpp  auth:, else 31
//
// Each cmdXxx() dispatcher only decides which command it is; every command has
// its own function. A DOS for particular media (CMD HD, IDE64) derives from
// this and overrides the partition commands.

#include <cstdint>
#include <memory>
#include <string>

#include "dos_host.h"
#include "../../../../meatloaf/media/hd/partition_select.h"

class MFile;
class MStream;

class CbmDos {
public:
  explicit CbmDos(DosHost& host) : m_host(host) {}
  virtual ~CbmDos() = default;
  virtual hdpart::Kind kind() const { return hdpart::Kind::None; }
  void execute(std::string command, const uint8_t* raw, uint8_t rawLen);  // command is CR-stripped and non-empty
protected:
  DosHost& m_host;
  const uint8_t* m_raw = nullptr;   // the command as received (binary arguments); valid only inside execute()
  uint8_t m_rawLen = 0;

  // The dispatchers. Each returns true if it consumed the command, false to
  // let the next group try it.
  virtual bool cmdMeatloafPrefix(std::string& command);
  virtual bool cmdCbm(std::string& command, uint8_t media, size_t colon);
  virtual bool cmdSd2iec(std::string& command, size_t colon);
  virtual void cmdMeatloafExtended(std::string& command);

  // --- cbm_dos.cpp: CBM DOS 2.6 / 1581 ---------------------------------------
  bool cmdBlock(const std::string& command);                                  // B-x: false when not a block command
  void cmdBlockPointer(const std::string& command);                           // B-P
  void cmdBlockReadWrite(const std::string& command, bool for_write);         // B-R / B-W
  void cmdBlockAllocate(const std::string& command, bool allocate);           // B-A / B-F
  void cmdBlockExecute(const std::string& command);                           // B-E
  void cmdInitialize();                                                       // I
  void cmdPosition();                                                         // P (REL record)
  bool cmdMemory(const std::string& command);                                 // M-R / M-W / M-E
  void cmdMemoryRead(uint16_t address);
  void cmdMemoryWrite(uint16_t address);
  void cmdNew(const std::string& command, size_t colon);                      // N
  void cmdLock(const std::string& arg);                                       // L
  void cmdScratch(const std::string& command, size_t colon);                  // S
  void cmdValidate();                                                         // V
  bool cmdUser(const std::string& command);                                   // U-x
  void cmdUserBlock(const std::string& command, bool for_write);              // U1/UA, U2/UB
  void cmdRename(const std::string& arg);                                     // R:new=old
  void cmdRenameHeader(const std::string& arg);                               // R-H:name
  void cmdCopy(const std::string& arg);                                       // C:new=old[,old2...]
  void cmdSelectSubPartition(const std::string& command, size_t colon);       // /[d:]name (1581)

  // --- sd2iec_dos.cpp: SD2IEC and CMD extensions ------------------------------
  virtual bool cmdChangePartition(const std::string& command);                // CP / C<shift-P>: 31 here, media DOSes override
  bool partitionFromCommand(const std::string& command);                      // parses the partition number, asks the host to select it
  virtual void cmdGetPartitionInfo();                                         // G-P: 31 here, CmdHdDos overrides
  virtual void cmdGeos();                                                     // GEOS: 31 here, CmdHdDos starts its resident turbo
  virtual void cmdRenamePartition(const std::string& arg);                    // R-P: 31 here, CmdHdDos overrides
  virtual void cmdDeletePartition();                                          // D-P: 31 here, CmdHdDos overrides
  void cmdMakeDirectory(const std::string& arg);                              // MD
  void cmdRemoveDirectory(const std::string& arg);                            // RD
  void cmdPrintWorkingDirectory();                                            // PWD
  void cmdSetRom(const std::string& arg);                                     // XR:name
  void cmdReadTime(const std::string& command);                               // T-RA/T-RI/T-RD/T-RB/T-RZ
  void cmdSetTime(const std::string& command, size_t colon);                  // T-WA/T-WI/T-WD/T-WB
  void cmdSetTimezone(const std::string& command, size_t colon);              // T-WZ

  // --- meatloaf_dos.cpp: Meatloaf's own commands ------------------------------
  bool cmdFastLoaderSwitch(const std::string& command);                       // EJ/EE/EA/EF/ED +/-
  bool cmdFilePosition(std::string command);                                  // F-P
  void cmdHardReset(const std::string& command);                              // U{Shift-J}[+]
  virtual void cmdPartitionAdmin(const std::string& args);                    // partition:...: 31 here, CmdHdDos has the commands
  std::string mediaPath(const std::string& name);                             // a file named in a partition command

  // --- Helpers (cbm_dos.cpp). Each that can fail sets the status itself. ------
  bool isBarePwd(const std::string& command) const;                           // PWD, not a binary P
  bool blockParams(const std::string& command, int p[4], int needed);         // 30 on a malformed or short list
  bool trackSectorInRange(int track, int sector);                             // 66 when either exceeds a byte
  std::shared_ptr<MStream> openImageForUpdate(std::unique_ptr<MFile>& image); // in|out on the cwd's container; 26/74
  void invalidateListings();                                                  // after writing the image behind ImageBroker
  static std::string stripDrivePrefix(const std::string& name);              // "0:NAME" -> "NAME"
  static bool hasWildcard(const std::string& name);
};

#endif
