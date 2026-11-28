#ifndef IEC_DOS_CMD_DOS_H
#define IEC_DOS_CMD_DOS_H

// The command channel for CMD HD / FD images (DHD, D1M, D2M, D4M): CBM DOS plus
// working CP / C<shift-P>, the partition-table commands G-P, R-P and D-P, and
// the Meatloaf partition:create / partition:delete / partition:default.

#include "cbm_dos.h"

class CmdHdDos : public CbmDos {
public:
  using CbmDos::CbmDos;
  hdpart::Kind kind() const override { return hdpart::Kind::CMD; }
protected:
  bool cmdChangePartition(const std::string& command) override;
  void cmdGetPartitionInfo() override;
  void cmdGeos() override;
  void cmdRenamePartition(const std::string& arg) override;
  void cmdDeletePartition() override;
  void cmdPartitionAdmin(const std::string& args) override;
private:
  std::string container();
};

#endif
