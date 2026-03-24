#pragma once

#include "../ConsoleCommand.h"

namespace ESP32Console::Commands
{
#ifdef ENABLE_DISPLAY
    const ConsoleCommand getLEDCommand();
#endif
}
