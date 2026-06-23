#pragma once

#include "../ConsoleCommand.h"

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

    const ConsoleCommand getAuthCommand();

    const ConsoleCommand getWgetCommand();

    const ConsoleCommand getUpdateCommand();

    const ConsoleCommand getDFCommand();

    const ConsoleCommand getEnableCommand();

    const ConsoleCommand getDisableCommand();

    const ConsoleCommand getGzipCommand();

#ifndef MIN_CONFIG
    const ConsoleCommand getUnzipCommand();
#endif

#ifdef SD_CARD
    const ConsoleCommand getFormatSDCommand();
    const ConsoleCommand getUpdatedbCommand();
    const ConsoleCommand getLocateCommand();
#endif
}