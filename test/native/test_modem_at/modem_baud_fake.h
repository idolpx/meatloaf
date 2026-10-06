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

// The seam that makes AT+IPR's staging testable.
//
// lib/console is not compiled in the native environment, so this suite
// supplies the bodies for lib/console/console_baud.h's declarations. It
// deliberately uses the REAL header rather than a copy: if a signature there
// changes, this stops compiling instead of silently testing a contract the
// firmware no longer has.
//
// The important member is on_promote. Every one of the four staging
// behaviours is about ORDER -- "the rate is published only after the reply is
// already queued" -- and an assertion made after executeLine() has returned
// passes just as happily against code that promotes inside the handler, which
// is the defect the staging exists to prevent. on_promote runs AT the moment
// consoleBaudSetPending() is called, so a test can photograph the rest of the
// world exactly then: what is in a port's TX buffer, whether a port is still
// attached.

#ifndef MEATLOAF_TEST_MODEM_BAUD_FAKE
#define MEATLOAF_TEST_MODEM_BAUD_FAKE

#include <functional>

namespace ModemBaudFake
{
    // What consoleBaudSupported() answers. Must be true for the AT+IPR branch
    // to be reached at all -- it returns false (-> ERROR) on a console with no
    // UART before it looks at the value.
    extern bool supported;

    // What consoleBaudGet() answers, for the bare "AT+IPR" / "AT+IPR?" report.
    extern int current;

    // Last value handed to consoleBaudSetPending(), and how many times. Zero
    // calls is what proves a staged rate was DISCARDED rather than published.
    extern int pending;
    extern int promote_count;

    // Called from inside consoleBaudSetPending(), before it records anything.
    extern std::function<void()> on_promote;

    void reset();
}

#endif
