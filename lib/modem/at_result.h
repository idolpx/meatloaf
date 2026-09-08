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

// Modem result codes.
//
// What reaches the terminal is a five-way product: V (verbose or numeric),
// Q (suppress), X (which subset is reportable), S3 (CR byte) and S4 (LF byte).
// Terminal programs do break on getting this wrong, which is why it is a
// separate unit with its own tests rather than a printf at each call site.

#ifndef MEATLOAF_MODEM_AT_RESULT
#define MEATLOAF_MODEM_AT_RESULT

#ifdef ENABLE_MODEM

#include <string>

#include "at_settings.h"

// Values are the numeric codes emitted under ATV0 and must not be renumbered.
enum class AtResult
{
    OK          = 0,
    CONNECT     = 1,
    RING        = 2,
    NO_CARRIER  = 3,
    ERROR       = 4,
    NO_DIALTONE = 6,
    BUSY        = 7,
    NO_ANSWER   = 8,
};

// Returns the exact bytes to send, or an empty string under ATQ1.
//
// A code the current X level does not report degrades to NO CARRIER rather
// than disappearing -- that is what a real modem does, and a dialer script
// waiting for any terminal result would otherwise hang.
std::string at_format_result(AtResult r, const AtSettings &s);

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM_AT_RESULT
