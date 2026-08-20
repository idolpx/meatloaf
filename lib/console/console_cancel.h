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

#ifndef CONSOLE_CANCEL_H
#define CONSOLE_CANCEL_H

namespace ESP32Console
{
    // ESC-to-cancel for console commands that can run long: "read", "cat" and
    // "hex" all stream a whole file, and at 460800 baud a large one buries the
    // console with no way out.
    //
    // The poll is NON-BLOCKING and reads from whichever transport issued the
    // running command (Console::execOrigin()), so a serial-origin command never
    // steals bytes from an idle TCP client and vice versa.
    //
    // Accepted cost: bytes that are not ESC are DISCARDED by the poll. Type-
    // ahead entered while a long command runs is lost. The rx/tx commands
    // already behave this way, and the alternative -- buffering input the shell
    // has not asked for -- would need a queue nothing else wants.

    // Clear the flag and drain anything already waiting, so a keypress from
    // before the command started cannot cancel it. Call once at the top of a
    // cancellable command.
    void cancel_begin();

    // True once ESC (0x1B) has arrived. Cheap, but not free -- on the TCP
    // console it is a syscall -- so callers consult it on an interval
    // (DOS_CANCEL_INTERVAL bytes), not per byte. Latches: once true it stays
    // true until the next cancel_begin().
    bool cancel_requested();

    // Set the flag directly, for a caller that has already seen the ESC.
    void cancel_request();
}

#endif // CONSOLE_CANCEL_H
