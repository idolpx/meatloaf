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

#ifdef ENABLE_CONSOLE

#include "console_cancel.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "Console.h"

#ifdef ENABLE_CONSOLE_TCP
#include "tcpsvr.h"
#endif

#define CANCEL_KEY 0x1B   // ESC

namespace ESP32Console
{
    static volatile bool s_cancelled = false;

    // Non-blocking sweep of the console's stdin. Same fcntl(O_NONBLOCK)+fgetc
    // idiom console_read_byte() in XFERCommands.cpp uses from this same task,
    // with a zero timeout -- this must never wait, it is called mid-transfer.
    //
    // No ConsoleRawIOGuard: the driver's RX CR->LF translation cannot turn any
    // byte into 0x1B or an 0x1B into anything else, so the line-ending mode is
    // irrelevant to what we are looking for.
    static bool drain_stdin(bool *saw_cancel)
    {
        int fd = fileno(stdin);
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0)
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        bool any = false;
        for (int c = fgetc(stdin); c != EOF; c = fgetc(stdin))
        {
            any = true;
            if (saw_cancel != nullptr && (uint8_t)c == CANCEL_KEY)
                *saw_cancel = true;
        }

        if (flags >= 0)
            fcntl(fd, F_SETFL, flags);

        return any;
    }

    static bool poll_transport()
    {
#ifdef ENABLE_CONSOLE_TCP
        // A remote-origin command runs on the executor while the TCP session
        // task is blocked inside console.execute() -- so nothing else is
        // reading that socket right now and this poll cannot steal a byte from
        // it. Only stdin is per-task; the socket is not, which is exactly why
        // the origin has to pick the transport.
        if (console.execOrigin() == Console::ORIGIN_REMOTE)
            return TCPServer::pollCancel();
#endif
        bool saw = false;
        drain_stdin(&saw);
        return saw;
    }

    void cancel_begin()
    {
        s_cancelled = false;

        // Discard anything typed before the command started: a stray ESC left
        // in the buffer would otherwise cancel it instantly.
#ifdef ENABLE_CONSOLE_TCP
        if (console.execOrigin() == Console::ORIGIN_REMOTE)
        {
            TCPServer::pollCancel();
            return;
        }
#endif
        drain_stdin(nullptr);
    }

    bool cancel_requested()
    {
        if (s_cancelled)
            return true;

        if (poll_transport())
            s_cancelled = true;

        return s_cancelled;
    }

    void cancel_request()
    {
        s_cancelled = true;
    }
}

#endif // ENABLE_CONSOLE
