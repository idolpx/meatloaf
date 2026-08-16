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
