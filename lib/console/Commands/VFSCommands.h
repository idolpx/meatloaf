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
#ifndef DISABLE_LOCATEDB
    const ConsoleCommand getUpdatedbCommand();
    const ConsoleCommand getLocateCommand();
#endif
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
#ifndef DISABLE_LOCATEDB
bool updatedb_request_stop();
#else
// No locate database compiled in: nothing can be running, so nothing to stop.
// Kept as an inline no-op so the shells' intercept sites need no #ifdef.
static inline bool updatedb_request_stop() { return false; }
#endif
#endif