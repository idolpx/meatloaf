#if defined(BUILD_IEC)

#include "cmd_dos.h"

bool CmdHdDos::cmdChangePartition(const std::string& command)
{
    return partitionFromCommand(command);
}

#endif
