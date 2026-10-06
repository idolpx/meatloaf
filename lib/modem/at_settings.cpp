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

#include "at_settings.h"

#ifdef ENABLE_MODEM

#include <cstdlib>

namespace
{

// The one place a default lives. factory() and toKeyValues() both read it, so
// they cannot drift apart.
uint8_t default_register(long n)
{
    switch (n)
    {
    case AT_S_AUTOANSWER:  return 0;
    case AT_S_ESCCHAR:     return 43;   // '+'
    case AT_S_CR:          return 13;
    case AT_S_LF:          return 10;
    case AT_S_BS:          return 8;
    case AT_S_CONNTIMEOUT: return 60;
    case AT_S_GUARD:       return 50;   // fiftieths of a second == 1 s
    case AT_S_AUTOSTREAM:  return 0;
    case AT_S_TELNET:      return 0;
    default:               return 0;
    }
}

} // namespace

void AtSettings::factory()
{
    for (long n = 0; n < REGISTER_COUNT; ++n)
        s[n] = default_register(n);

    echo    = true;
    quiet   = false;
    verbose = true;
    xlevel  = 4;
}

bool AtSettings::setRegister(long n, long v)
{
    if (n < 0 || n >= REGISTER_COUNT)
        return false;
    if (v < 0 || v > 255)
        return false;
    s[n] = (uint8_t)v;
    return true;
}

long AtSettings::getRegister(long n) const
{
    if (n < 0 || n >= REGISTER_COUNT)
        return -1;
    return (long)s[n];
}

std::map<std::string, long> AtSettings::toKeyValues() const
{
    std::map<std::string, long> kv;

    for (long n = 0; n < REGISTER_COUNT; ++n)
    {
        if (s[n] != default_register(n))
            kv["s" + std::to_string(n)] = (long)s[n];
    }

    if (!echo)       kv["echo"]    = 0;
    if (quiet)       kv["quiet"]   = 1;
    if (!verbose)    kv["verbose"] = 0;
    if (xlevel != 4) kv["xlevel"]  = (long)xlevel;

    return kv;
}

void AtSettings::fromKeyValues(const std::map<std::string, long> &kv)
{
    for (const auto &pair : kv)
    {
        const std::string &k = pair.first;
        long v = pair.second;

        if (k.size() > 1 && k[0] == 's')
        {
            // strtol rather than std::stoi: ESP-IDF builds -fno-exceptions, so
            // a throwing conversion on malformed input becomes std::terminate.
            const char *start = k.c_str() + 1;
            char *end = nullptr;
            long n = std::strtol(start, &end, 10);
            if (end == start || *end != '\0')
                continue;

            setRegister(n, v);  // refuses out-of-range itself
            continue;
        }

        if (k == "echo")    { echo    = (v != 0); continue; }
        if (k == "quiet")   { quiet   = (v != 0); continue; }
        if (k == "verbose") { verbose = (v != 0); continue; }
        if (k == "xlevel")  { if (v >= 0 && v <= 4) xlevel = (uint8_t)v; continue; }
    }
}

#endif // ENABLE_MODEM
