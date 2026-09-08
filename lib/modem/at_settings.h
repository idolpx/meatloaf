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

// Modem settings: the S-register array and the E/Q/V/X flags.
//
// Serialization is to a plain key-value map rather than to JSON, so this unit
// stays free of nlohmann and reachable from the native suite. modem.cpp does
// the mlConfig read and write.

#ifndef MEATLOAF_MODEM_AT_SETTINGS
#define MEATLOAF_MODEM_AT_SETTINGS

#ifdef ENABLE_MODEM

#include <cstdint>
#include <map>
#include <string>

// The S-registers phase 1 knows by name. Others are storable and reportable
// but have no behaviour attached.
enum : long
{
    AT_S_AUTOANSWER  = 0,   // rings before auto-answer (acted on in phase 3)
    AT_S_ESCCHAR     = 2,   // escape character, default '+'
    AT_S_CR          = 3,   // carriage-return character
    AT_S_LF          = 4,   // line-feed character
    AT_S_BS          = 5,   // backspace character
    AT_S_CONNTIMEOUT = 7,   // seconds to wait for a connection
    AT_S_GUARD       = 12,  // escape guard time, in fiftieths of a second
    AT_S_AUTOSTREAM  = 41,  // auto-stream incoming calls (phase 3)
    AT_S_TELNET      = 62,  // negotiate telnet on a plain ATD (Zimodem's S62)
};

struct AtSettings
{
    static constexpr long REGISTER_COUNT = 128;

    uint8_t s[REGISTER_COUNT];

    bool    echo    = true;   // ATE
    bool    quiet   = false;  // ATQ
    bool    verbose = true;   // ATV
    uint8_t xlevel  = 4;      // ATX

    // Restores every documented default. AT&F.
    void factory();

    // Refuses an index outside [0, REGISTER_COUNT) and a value outside
    // [0, 255]. Returning false is what makes the caller emit ERROR instead of
    // silently applying a different setting than the user asked for.
    bool setRegister(long n, long v);

    // Returns -1 for an out-of-range index.
    long getRegister(long n) const;

    // Only values differing from the factory default are emitted, so the saved
    // config stays small and a later change of default is inherited.
    std::map<std::string, long> toKeyValues() const;

    // Applies what it recognises and ignores the rest. A config written by a
    // future version, or hand-edited, must not be able to reach a state the
    // setters would refuse.
    void fromKeyValues(const std::map<std::string, long> &kv);
};

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM_AT_SETTINGS
