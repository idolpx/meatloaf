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

    const ConsoleCommand getPartitionCommand();

    const ConsoleCommand getGzipCommand();

#ifndef MIN_CONFIG
    const ConsoleCommand getUnzipxCommand();
#endif

#ifdef SD_CARD
    const ConsoleCommand getFormatSDCommand();
    const ConsoleCommand getUpdatedbCommand();
    const ConsoleCommand getLocateCommand();
#endif
}

#ifdef SD_CARD
// Request cancellation of a running updatedb scan.
//
// The scan runs ON the console executor, so "updatedb stop" typed at a prompt
// would queue behind it and never run. The shells therefore intercept that line
// and call this directly, the same way they intercept "exit" and "reboot".
// Returns false if no scan is in progress. Safe from any task: it only sets a
// volatile flag the scan polls once per directory.
bool updatedb_request_stop();
#endif