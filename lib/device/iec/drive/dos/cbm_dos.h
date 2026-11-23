#ifndef IEC_DOS_CBM_DOS_H
#define IEC_DOS_CBM_DOS_H

// The command channel: CBM DOS 2.6, SD2IEC and Meatloaf commands, in the order
// iecDrive::executeData() used to try them. A DOS for particular media (CMD HD,
// IDE64) derives from this and overrides one command family.

#include <cstdint>
#include <string>

#include "dos_host.h"
#include "../../../../meatloaf/media/hd/partition_select.h"

class CbmDos {
public:
  explicit CbmDos(DosHost& host) : m_host(host) {}
  virtual ~CbmDos() = default;
  virtual hdpart::Kind kind() const { return hdpart::Kind::None; }
  void execute(std::string command, const uint8_t* raw, uint8_t rawLen);  // command is CR-stripped and non-empty
protected:
  DosHost& m_host;
  // Each returns true if it consumed the command (old code's `return`), false to fall through (old `break`).
  virtual bool cmdMeatloafPrefix(std::string& command);                        // CD, E?+/-, Dolphin M-R hack
  virtual bool cmdCbm(std::string& command, uint8_t media, size_t colon);     // first switch
  virtual bool cmdSd2iec(std::string& command, size_t colon);                 // second switch
  virtual bool cmdChangePartition(const std::string& command);                // CP / C<shift-P>: 31 here, media DOSes override
  bool partitionFromCommand(const std::string& command);                      // parses the partition number, asks the host to select it
  virtual void cmdMeatloafExtended(std::string& command);                     // auth:, else 31
};

#endif
