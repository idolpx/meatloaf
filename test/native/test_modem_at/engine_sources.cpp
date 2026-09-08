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

// Pulls in the exact translation units the modem AT tests need, by
// #include-ing the real .cpp files by relative path. Same approach as
// test/native/test_console_dos/engine_sources.cpp -- see that file for why
// PlatformIO's library dependency finder cannot be used here.
//
// Everything included below is deliberately free of ESP-IDF, MStream and
// lib/console dependencies. If a future edit makes one of these files pull in
// FreeRTOS or MFSOwner, this suite stops building -- which is the point.

// ENABLE_MODEM gates the firmware build. The native suite always wants these
// units, so define it here rather than in the native env's build_flags, which
// would also switch on code paths that need ESP-IDF.
#define ENABLE_MODEM 1

#include "../../../lib/modem/at_parser.cpp"
#include "../../../lib/modem/at_settings.cpp"
#include "../../../lib/modem/at_result.cpp"
#include "../../../lib/modem/escape.cpp"
