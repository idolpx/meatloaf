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

#include "dos_encode.h"

#include <cctype>
#include <cstdlib>

#include "string_utils.h"

namespace
{
    // The 0xNN run-escape walk, shared by both encoders. `xform` is applied to
    // each run of pending TEXT; hex escapes always pass through verbatim.
    std::string encodeEscapes(const std::string &line,
                              std::string (*xform)(const std::string &))
    {
        std::string out;
        std::string text;   // pending text, converted on the next flush

        auto flushText = [&out, &text, xform]()
        {
            if (text.empty())
                return;
            out += xform(text);
            text.clear();
        };

        for (size_t i = 0; i < line.size(); )
        {
            // Hex bytes are written verbatim -- one "0x" introduces a run.
            if (i + 4 <= line.size() && line[i] == '0' && (line[i + 1] == 'x' || line[i + 1] == 'X') &&
                isxdigit(static_cast<unsigned char>(line[i + 2])) &&
                isxdigit(static_cast<unsigned char>(line[i + 3])))
            {
                flushText();
                size_t j = i + 2;
                while (j + 2 <= line.size() &&
                       isxdigit(static_cast<unsigned char>(line[j])) &&
                       isxdigit(static_cast<unsigned char>(line[j + 1])))
                {
                    char hex[3] = { line[j], line[j + 1], '\0' };
                    out += static_cast<char>(strtol(hex, nullptr, 16));
                    j += 2;
                }
                i = j;
            }
            else
            {
                text += line[i++];
            }
        }
        flushText();

        return out;
    }

    std::string xformPetscii(const std::string &s) { return mstr::toPETSCII2(s); }
    std::string xformNone(const std::string &s)    { return s; }
}

namespace ESP32Console
{
    std::string encodeDosCommand(const std::string &line)
    {
        return encodeEscapes(line, xformPetscii);
    }

    std::string encodeAsciiCommand(const std::string &line)
    {
        return encodeEscapes(line, xformNone);
    }
}
