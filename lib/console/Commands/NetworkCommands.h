#pragma once

#include "../ConsoleCommand.h"

namespace ESP32Console::Commands
{
    const ConsoleCommand getPingCommand();

    const ConsoleCommand getIfconfigCommand();

    const ConsoleCommand getNetstatCommand();

    const ConsoleCommand getScanCommand();

    const ConsoleCommand getConnectCommand();

    const ConsoleCommand getExitCommand();

#ifndef MIN_CONFIG
    const ConsoleCommand getWsCommand();
#endif
}