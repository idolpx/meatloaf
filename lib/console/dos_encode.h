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

#ifndef CONSOLE_DOS_ENCODE_H
#define CONSOLE_DOS_ENCODE_H

#include <string>

namespace ESP32Console
{
    // Encode a console-typed DOS command or filename the way the C64 would put
    // it on the wire.  Shared by "exec", "open" and "write" so all three speak
    // the same language.
    //
    // Text is PETSCII encoded and nothing else.  mstr::toPETSCII2() maps
    // LOWERCASE ASCII onto $41-$5A, which is exactly what an unshifted C64
    // sends and what executeData() dispatches on ("CD", "N0", "T-Z") -- so TYPE
    // IN LOWERCASE, as you would at the READY prompt, and the translation makes
    // it the right case on the wire.
    //
    // Nothing is case-folded here, and that is deliberate: commands and their
    // parameters can be mixed case, and lowercasing the line to make the VERB
    // match would corrupt every filename and path in it.  One encoding, no
    // hidden transforms.
    //
    // Uppercase input is therefore not equivalent -- it maps to the SHIFTED
    // range (measured: "M-R" -> CD 2D D2, "I0:" -> C9 30 3A), valid PETSCII
    // that matches no command.  It is not a silent trap: executeData() falls
    // through to ST_SYNTAX_INVALID, so "exec M-R" answers "31,INVALID COMMAND".
    //
    // Binary bytes -- M-R/M-W/B-P carry addresses and data that are not text at
    // all -- are written as "0x" followed by an even number of hex digits, and
    // pass through verbatim, unconverted.  ONE "0x" introduces a RUN of bytes:
    // "0x000009" is three bytes 00 00 09, not one byte followed by the text
    // "0009".  The per-byte form still works, so "0x000x030x01" is the same
    // three bytes as "0x000301" -- the run stops at the 'x', which is not a hex
    // digit.
    //
    // The ambiguity this accepts, deliberately: a hex escape immediately
    // followed by text whose first two characters are hex digits will swallow
    // them ("0x00cd" is two bytes 00 CD, not one byte then "cd").  That is
    // tolerable because every command carrying binary -- M-R, M-W, M-E, B-P --
    // is all binary after the verb.  Put the text before the escape, or split
    // the run with a space, when it matters.
    //
    // A trailing odd hex digit is left as text rather than guessed at.
    std::string encodeDosCommand(const std::string &line);
}

#endif // CONSOLE_DOS_ENCODE_H
