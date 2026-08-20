#pragma once

#include "../ConsoleCommand.h"

#include <string>

namespace ESP32Console::Commands
{
    const ConsoleCommand getIECCommand();

    const ConsoleCommand getEnableCommand();

    const ConsoleCommand getDisableCommand();

    const ConsoleCommand getUseCommand();

    const ConsoleCommand getExecCommand();

    // File channels on the selected device - the console's stand-in for
    // OPEN/GET#/PRINT#/CLOSE on a C64.
    const ConsoleCommand getOpenCommand();

    const ConsoleCommand getReadCommand();

    const ConsoleCommand getWriteCommand();

    const ConsoleCommand getCloseCommand();

    const ConsoleCommand getChannelsCommand();
}

namespace ESP32Console
{
    // Device id selected with the "use" command, or 0 when none is selected.
    // Shown in the console prompt via the "%dev%" token.
    int iecSelectedDeviceId();

    // Point the selected device at the console's working directory. Called by
    // setCurrentPath() so the device follows every console cd; no-op when no
    // device is selected.
    void iecSyncSelectedDeviceCwd(const std::string &url);
}
