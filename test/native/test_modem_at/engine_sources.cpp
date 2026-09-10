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
#include "../../../lib/modem/phonebook.cpp"
#include "../../../lib/modem/telnet_filter.cpp"

// TelnetMStream (Task 8 fix round 2). telnet.cpp is a decorator over
// lib/meatloaf/meatloaf.h's MStream/MFile -- unlike everything above, this
// one line DOES pull MFile/MFSOwner into this suite. It is safe: the only
// thing telnet.cpp calls that touches MFSOwner is TelnetMFile::createStream()
// (a live TCP dial, deliberately never exercised here -- see
// test_modem_at.cpp's telnet_stream tests, which construct TelnetMStream
// directly against a FakeMStream and never build a TelnetMFile). The three
// lib/utils files below have nothing to do with sockets; they are pulled in
// because TelnetMFile::createStream() is telnet.cpp's one out-of-line virtual
// method, which under the Itanium C++ ABI makes telnet.cpp the "key function"
// translation unit that must emit TelnetMFile's full vtable -- including the
// MFile default virtuals (isText(), fullUrl()) that TelnetMFile does not
// override, which call mstr::isText()/startsWith()/endsWith(). Exactly the
// same trio, for exactly the same underlying reason (a concrete MStream/MFile
// subclass drags in its inherited virtuals' default bodies), is already
// proven native-safe by test/native/test_mstream_seek/engine_sources.cpp.
#include "../../../lib/utils/punycode.cpp"
// punycode.cpp #define's a bare `min(a,b)` macro with no matching #undef.
#undef min
#include "../../../lib/utils/U8Char.cpp"
#include "../../../lib/utils/string_utils.cpp"
#include "../../../lib/meatloaf/network/telnet.cpp"

// Link-only stubs for symbols telnet.cpp/meatloaf.h reference but this suite
// never calls (MFSOwner::File(), the four track/sector MStream hooks,
// util_debug_printf, ...). Shared verbatim with test_mstream_seek, which
// needs the identical set for the identical reason.
#include "../test_disk_write/native_stubs.cpp"
