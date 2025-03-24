#pragma once

#include "../ConsoleCommand.h"
#include "meatloaf.h"

namespace ESP32Console::Commands
{
    const ConsoleCommand getCatCommand();

    const ConsoleCommand getHexCommand();

    const ConsoleCommand getPWDCommand();

    const ConsoleCommand getCDCommand();

    const ConsoleCommand getLsCommand();

    const ConsoleCommand getMvCommand();

    const ConsoleCommand getCPCommand();

    const ConsoleCommand getRMCommand();

    const ConsoleCommand getRMDirCommand();

    const ConsoleCommand getMKDirCommand();

    const ConsoleCommand getEditCommand();

    const ConsoleCommand getStatusCommand();

    const ConsoleCommand getMountCommand();

    const ConsoleCommand getCRC32Command();

    const ConsoleCommand getWgetCommand();

    const ConsoleCommand getEnableCommand();

    const ConsoleCommand getDisableCommand();
}