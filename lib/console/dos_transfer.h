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

#ifndef CONSOLE_DOS_TRANSFER_H
#define CONSOLE_DOS_TRANSFER_H

#include <stddef.h>
#include <stdint.h>

namespace ESP32Console
{
    // How many bytes may pass before the cancel predicate is consulted again.
    // The poll is cheap but not free -- on the TCP console it is a syscall --
    // and a caller moving bytes one at a time would otherwise pay it per byte.
    static const size_t DOS_CANCEL_INTERVAL = 256;

    // The largest single IEC read or write.  iecDrive::read()/write() take a
    // uint8_t length, so 255 is the ceiling, not a tuning choice.
    static const uint8_t DOS_CHUNK_MAX = 255;

    // Move bytes off a channel until the device signals end of file, the byte
    // limit is reached, or the caller cancels.  Templated rather than taking
    // std::function so nothing is allocated and nothing is called indirectly.
    //
    //   Reader  uint8_t(uint8_t *buf, uint8_t len)
    //             bytes read; 0 means END OF FILE.  A 0 on the FIRST call is
    //             the device's error signal (e.g. FILE NOT FOUND) rather than
    //             an empty file -- the caller tells them apart by this
    //             function returning 0 with *cancelled false.
    //   Sink    void(const uint8_t *buf, size_t len, size_t offset)
    //             consumes one chunk; offset is its position in the transfer.
    //   Cancel  bool()
    //             true to stop now.
    //
    // max_bytes of 0 means "no limit".  Returns the number of bytes read.
    //
    // The three callables are forwarding references so a stateful one is used
    // in place rather than copied -- a by-value sink accumulates into a copy
    // the caller never sees.
    template <class Reader, class Sink, class Cancel>
    size_t dos_read_loop(size_t max_bytes, Reader &&reader, Sink &&sink, Cancel &&cancel,
                         bool *cancelled = nullptr)
    {
        uint8_t buffer[DOS_CHUNK_MAX];
        size_t total = 0;
        size_t pending = 0;   // bytes since the last cancel check

        if (cancelled != nullptr)
            *cancelled = false;

        while (max_bytes == 0 || total < max_bytes)
        {
            uint8_t want = DOS_CHUNK_MAX;
            if (max_bytes != 0 && (max_bytes - total) < want)
                want = (uint8_t)(max_bytes - total);

            uint8_t got = reader(buffer, want);
            if (got == 0)
                break;      // end of file, or the error the caller checks for

            sink(buffer, got, total);
            total += got;

            pending += got;
            if (pending >= DOS_CANCEL_INTERVAL)
            {
                pending = 0;
                if (cancel())
                {
                    if (cancelled != nullptr)
                        *cancelled = true;
                    break;
                }
            }
        }

        return total;
    }

    // Push bytes onto a channel, in chunks the IEC layer can take.
    //
    //   Writer  uint8_t(const uint8_t *buf, uint8_t len, bool eoi)
    //             bytes accepted.  Returning LESS than len is the device's
    //             "cannot receive more data" signal and ends the transfer.
    //   Cancel  bool()
    //
    // EOI is set on the last chunk only.  Returns the number of bytes accepted.
    template <class Writer, class Cancel>
    size_t dos_write_loop(const uint8_t *data, size_t len, Writer &&writer, Cancel &&cancel,
                          bool *cancelled = nullptr)
    {
        size_t total = 0;
        size_t pending = 0;

        if (cancelled != nullptr)
            *cancelled = false;

        while (total < len)
        {
            size_t remaining = len - total;
            uint8_t chunk = remaining > DOS_CHUNK_MAX ? DOS_CHUNK_MAX : (uint8_t)remaining;
            bool eoi = (total + chunk) >= len;

            uint8_t took = writer(data + total, chunk, eoi);
            total += took;

            if (took < chunk)
                break;      // device cannot receive more

            pending += took;
            if (pending >= DOS_CANCEL_INTERVAL)
            {
                pending = 0;
                if (cancel())
                {
                    if (cancelled != nullptr)
                        *cancelled = true;
                    break;
                }
            }
        }

        return total;
    }
}

#endif // CONSOLE_DOS_TRANSFER_H
