#pragma once

#include "../ConsoleCommand.h"

namespace ESP32Console::Commands
{
    // No #ifdef: PS2KeyboardDevice has a no-op stub on boards without
    // PIN_KB_CLK, so this compiles everywhere and `ps2 status` simply
    // reports that the board has no PS/2 pins.
    const ConsoleCommand getPS2Command();
}
