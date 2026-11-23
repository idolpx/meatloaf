#if defined(BUILD_IEC)

#include "ide64_dos.h"

bool Ide64Dos::cmdChangePartition(const std::string& command)
{
    return partitionFromCommand(command);
}

#endif
