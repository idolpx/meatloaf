#pragma once

#include "../ConsoleCommand.h"

namespace ESP32Console::Commands
{
    const ConsoleCommand getClearCommand();

    const ConsoleCommand getEchoCommand();

    const ConsoleCommand getEnvCommand();

    const ConsoleCommand getDeclareCommand();

    const ConsoleCommand getRunCommand();

    const ConsoleCommand getRebootCommand();

    const ConsoleCommand getExitCommand();
}