#pragma once

#include "../ConsoleCommand.h"

namespace ESP32Console::Commands
{
    const ConsoleCommand getIECDetectCommand();
    const ConsoleCommand getIECSleepCommand();
    const ConsoleCommand getIECWakeCommand();
}
