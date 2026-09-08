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

#include "at_parser.h"

#ifdef ENABLE_MODEM

#include <cctype>

namespace
{

char upper(char c)
{
    return (char)std::toupper((unsigned char)c);
}

bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

// Verbs that take an optional numeric suffix and nothing else.
bool verb_takes_number(char prefix, char v)
{
    if (prefix == '&')
        return v == 'W' || v == 'F';
    return v == 'E' || v == 'Q' || v == 'V' || v == 'X' || v == 'Z' ||
           v == 'O' || v == 'H' || v == 'I' || v == 'A';
}

// Reads consecutive digits starting at i, advancing i. Returns -1 when there
// are none, so the caller can tell "absent" from a literal 0.
long read_number(const std::string &s, size_t &i)
{
    size_t start = i;
    long n = 0;
    while (i < s.size() && is_digit(s[i]))
    {
        n = n * 10 + (s[i] - '0');
        ++i;
        // A modem's numbers are all small. Clamp rather than overflow.
        if (n > 1000000)
            n = 1000000;
    }
    return (i == start) ? -1 : n;
}

// Reads a "..." argument starting at the opening quote. Returns false when it
// is never closed, leaving i at the opening quote for the error report.
bool read_quoted(const std::string &s, size_t &i, std::string &out)
{
    size_t quote_at = i;
    ++i;
    while (i < s.size())
    {
        if (s[i] == '"')
        {
            ++i;
            return true;
        }
        out.push_back(s[i]);
        ++i;
    }
    i = quote_at;
    return false;
}

} // namespace

bool at_is_repeat(const std::string &line)
{
    return line.size() == 2 && upper(line[0]) == 'A' && line[1] == '/';
}

bool at_parse(const std::string &line, AtLine &out, size_t *err_pos)
{
    out.clear();

    auto fail = [&](size_t pos) {
        if (err_pos)
            *err_pos = pos;
        out.clear();
        return false;
    };

    if (line.size() < 2 || upper(line[0]) != 'A' || upper(line[1]) != 'T')
        return fail(0);

    size_t i = 2;
    while (i < line.size())
    {
        if (std::isspace((unsigned char)line[i]))
        {
            ++i;
            continue;
        }

        AtCommand cmd;
        size_t cmd_start = i;

        // ---- AT+NAME -------------------------------------------------------
        if (line[i] == '+')
        {
            cmd.prefix = '+';
            ++i;
            size_t name_start = i;
            while (i < line.size() && std::isalnum((unsigned char)line[i]))
            {
                cmd.name.push_back(upper(line[i]));
                ++i;
            }
            if (i == name_start)
                return fail(cmd_start);
            out.push_back(cmd);
            continue;
        }

        // ---- AT&X ----------------------------------------------------------
        if (line[i] == '&')
        {
            cmd.prefix = '&';
            ++i;
            if (i >= line.size() || !std::isalpha((unsigned char)line[i]))
                return fail(cmd_start);
            cmd.verb = upper(line[i]);
            ++i;
        }
        else
        {
            if (!std::isalpha((unsigned char)line[i]))
                return fail(i);
            cmd.verb = upper(line[i]);
            ++i;
        }

        // ---- S<n>=<v> and S<n>? -------------------------------------------
        // AT&Snn=v is the same command with an ampersand prefix; Zimodem
        // writes telnet mode that way and terminal programs copy it, so it
        // must parse identically to ATSnn=v.
        if (cmd.verb == 'S')
        {
            long n = read_number(line, i);
            if (n < 0)
                return fail(cmd_start);
            cmd.number = n;

            if (i < line.size() && line[i] == '=')
            {
                ++i;
                cmd.assign = true;
                long v = read_number(line, i);
                if (v < 0)
                    return fail(i);
                cmd.value = v;
            }
            else if (i < line.size() && line[i] == '?')
            {
                ++i;
                cmd.query = true;
            }
            else
            {
                return fail(i);
            }

            out.push_back(cmd);
            continue;
        }

        // ---- ATP, ATPn="host:port"[,mods] ---------------------------------
        if (cmd.prefix == 0 && cmd.verb == 'P')
        {
            long n = read_number(line, i);
            cmd.number = (n < 0) ? 0 : n;

            if (i < line.size() && line[i] == '=')
            {
                ++i;
                cmd.assign = true;

                if (i < line.size() && line[i] == '"')
                {
                    if (!read_quoted(line, i, cmd.arg))
                        return fail(i);

                    // Modifiers follow the quoted argument here, unlike ATD
                    // where they precede it -- ATP1="host:23",T is how a user
                    // expects to type it.
                    if (i < line.size() && line[i] == ',')
                    {
                        ++i;
                        while (i < line.size() && std::isalpha((unsigned char)line[i]))
                        {
                            char m = upper(line[i]);
                            if (m != 'T' && m != 'E')
                                return fail(i);
                            cmd.mods.push_back(m);
                            ++i;
                        }
                    }
                }
                // An '=' with nothing after it is the delete form; arg stays
                // empty and assign distinguishes it from a bare ATP.
            }

            out.push_back(cmd);
            continue;
        }

        // ---- ATD[mods]"host:port" or ATD<digits> ---------------------------
        if (cmd.prefix == 0 && cmd.verb == 'D')
        {
            // Phase 1 accepts T (telnet) and E (local echo). P, X and S are
            // phase 2 and later; they are rejected rather than silently
            // ignored, so a user is told rather than surprised.
            while (i < line.size() && std::isalpha((unsigned char)line[i]))
            {
                char m = upper(line[i]);
                if (m != 'T' && m != 'E')
                    return fail(i);
                cmd.mods.push_back(m);
                ++i;
            }

            if (i < line.size() && line[i] == '"')
            {
                if (!read_quoted(line, i, cmd.arg))
                    return fail(i);
            }
            else
            {
                // Bare digits are a phonebook entry. Kept as a string because
                // leading zeros are significant to the lookup.
                while (i < line.size() && is_digit(line[i]))
                {
                    cmd.arg.push_back(line[i]);
                    ++i;
                }
            }

            out.push_back(cmd);
            continue;
        }

        // ---- everything else: an optional numeric suffix --------------------
        if (!verb_takes_number(cmd.prefix, cmd.verb))
            return fail(cmd_start);

        long n = read_number(line, i);
        cmd.number = (n < 0) ? 0 : n;
        out.push_back(cmd);
    }

    return true;
}

#endif // ENABLE_MODEM
