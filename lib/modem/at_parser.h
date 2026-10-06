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

// AT command line parser.
//
// One line may carry several commands: "AT&S62=1DT\"host:23\"" is an
// S-register assignment followed by a dial. This is exactly why the modem
// cannot reuse esp_console_run()'s splitter, which drops arguments past
// CONSOLE_MAX_CMDLINE_ARGS and only strips a leading quote.
//
// Pure string work. No I/O, no ESP-IDF, no MStream -- so it is reachable from
// test/native/test_modem_at.

#ifndef MEATLOAF_MODEM_AT_PARSER
#define MEATLOAF_MODEM_AT_PARSER

#ifdef ENABLE_MODEM

#include <string>
#include <vector>

struct AtCommand
{
    // 0 for a plain command, '&' for AT&W, '+' for AT+SHELL.
    char prefix = 0;

    // The command letter, uppercased. Unused ('\0') for '+' commands, which
    // carry a word rather than a letter.
    char verb = 0;

    // For '+' commands only: the word after the '+', uppercased. "SHELL".
    std::string name;

    // Modifier letters. For ATDT these sit between the verb and the argument;
    // for ATP they follow it as ",T". Uppercased, in the order written.
    std::string mods;

    // Numeric suffix (ATE1 -> 1, ATS12=50 -> 12). 0 when a suffix is allowed
    // but absent, which is what a real modem assumes.
    long number = 0;

    // Right-hand side of an S-register assignment. -1 when there is none.
    long value = -1;

    // True for the "ATS12?" query form.
    bool query = false;

    // True when an '=' was present. Distinguishes "ATP1=" (delete entry 1)
    // from "ATP" (list), which otherwise both have an empty arg.
    bool assign = false;

    // Contents of a quoted argument, or the digits of a phonebook dial.
    std::string arg;
};

using AtLine = std::vector<AtCommand>;

// Parses one line into its commands. The line must begin with "AT" in any
// case; a bare "AT" is valid and yields an empty list.
//
// On failure returns false and, when err_pos is non-null, writes the offset of
// the offending character. The caller reports ERROR; it does not need to say
// where, but the offset makes the failure debuggable from a log.
bool at_parse(const std::string &line, AtLine &out, size_t *err_pos);

// True for the "A/" repeat-last-line form, which carries no AT prefix and so
// cannot go through at_parse().
bool at_is_repeat(const std::string &line);

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM_AT_PARSER
