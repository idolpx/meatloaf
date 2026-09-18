// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

// The one owner of the console UART's rate.
//
// Both the "baud" shell command and the modem's AT+IPR end here, but they
// reach it by different routes, and that difference is the reason this module
// exists. The shell command runs on the same task that printed its notice, so
// it applies the change itself. AT+IPR runs on the modem task, which never
// touches a console file descriptor (see lib/modem/modem_port.h) -- its "OK"
// is still sitting in a ModemPort StreamBuffer when the handler returns, so
// switching the line there would clock that reply out at the new rate as
// garbage. It records a pending rate instead, and modem_shell_pump() applies
// it once those bytes are actually on the wire.

#ifndef MEATLOAF_CONSOLE_BAUD
#define MEATLOAF_CONSOLE_BAUD

#include "esp_err.h"

namespace ESP32Console
{
    // A range check only. The driver picks the closest achievable divisor, and
    // a silly-but-legal rate is recoverable over the TCP console, so refusing a
    // legitimate odd rate would cost more than accepting one.
    static constexpr int BAUD_MIN = 300;
    static constexpr int BAUD_MAX = 4000000;

    // False on the five boards whose console is USB-Serial-JTAG or USB-CDC.
    // There is no UART there and a rate is discarded by the USB stack, so the
    // commands refuse rather than appearing to succeed.
    bool consoleBaudSupported();

    // "UART", "USB-Serial-JTAG" or "USB-CDC", for the refusal message.
    const char *consoleBaudTransportName();

    // The rate in effect, read from the driver rather than from a shadow copy
    // that could drift. 0 when this console has no UART.
    int consoleBaudGet();

    // Validate, drain, switch, and only then persist. Safe to call from a task
    // that owns the console fd, or from a TCP-origin command, whose reply never
    // crosses the UART at all.
    esp_err_t consoleBaudSet(int baud);

    // Record a rate for modem_shell_pump() to apply after its next write.
    void consoleBaudSetPending(int baud);
    void consoleBaudApplyPending();

    // Boot: apply preferences.baud if it is set. Called after mlConfig.load().
    void consoleBaudRestore();
}

#endif // MEATLOAF_CONSOLE_BAUD
