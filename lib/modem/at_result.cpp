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

#include "at_result.h"

#ifdef ENABLE_MODEM

namespace
{

const char *verbose_text(AtResult r)
{
    switch (r)
    {
    case AtResult::OK:          return "OK";
    case AtResult::CONNECT:     return "CONNECT";
    case AtResult::RING:        return "RING";
    case AtResult::NO_CARRIER:  return "NO CARRIER";
    case AtResult::ERROR:       return "ERROR";
    case AtResult::NO_DIALTONE: return "NO DIALTONE";
    case AtResult::BUSY:        return "BUSY";
    case AtResult::NO_ANSWER:   return "NO ANSWER";
    }
    return "ERROR";
}

// The minimum X level at which each code is reportable. Below it the code
// degrades to NO CARRIER.
uint8_t min_xlevel(AtResult r)
{
    switch (r)
    {
    case AtResult::NO_DIALTONE: return 2;
    case AtResult::BUSY:        return 3;
    case AtResult::NO_ANSWER:   return 4;
    default:                    return 0;
    }
}

// S3 and S4 are the bytes themselves. A register set to 0 means "emit
// nothing", which is how a carriage-return-only terminal is served.
void append_eol(std::string &out, const AtSettings &s)
{
    long cr = s.getRegister(AT_S_CR);
    long lf = s.getRegister(AT_S_LF);
    if (cr > 0) out.push_back((char)cr);
    if (lf > 0) out.push_back((char)lf);
}

} // namespace

std::string at_format_result(AtResult r, const AtSettings &s)
{
    if (s.quiet)
        return std::string();

    if (s.xlevel < min_xlevel(r))
        r = AtResult::NO_CARRIER;

    std::string out;

    if (s.verbose)
    {
        append_eol(out, s);
        out += verbose_text(r);
        append_eol(out, s);
        return out;
    }

    out += std::to_string((int)r);
    long cr = s.getRegister(AT_S_CR);
    if (cr > 0)
        out.push_back((char)cr);
    return out;
}

#endif // ENABLE_MODEM
