#ifndef IEC_DOS_IDE64_DOS_H
#define IEC_DOS_IDE64_DOS_H

// The command channel for IDE64 CFS images (.hdd): CBM DOS plus working CP / C<shift-P>.

#include "cbm_dos.h"

class Ide64Dos : public CbmDos {
public:
  using CbmDos::CbmDos;
  hdpart::Kind kind() const override { return hdpart::Kind::CFS; }
protected:
  bool cmdChangePartition(const std::string& command) override;
};

#endif
