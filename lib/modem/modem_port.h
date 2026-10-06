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

// One console's connection to the modem task.
//
// The modem task must never touch a console file descriptor. Two tasks on one
// socket is the condition behind the NFS 0x6400 heap corruption and the reason
// console.printf() is banned from I/O hot paths; the ESC-cancel design relies
// on the shell task being blocked while a command runs, so there is never a
// second reader. A modem task writing to the console fd would break that.
//
// So the shell task stays the sole reader and writer of its own fd and becomes
// a pump: bytes off the fd go into rx, bytes out of tx go to the fd. FreeRTOS
// StreamBuffers are single-writer/single-reader by design, so no mutex is
// needed as long as exactly one task is on each end -- which is the case here.

#ifndef MEATLOAF_MODEM_PORT
#define MEATLOAF_MODEM_PORT

#ifdef ENABLE_MODEM

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

class ModemPort
{
public:
    // Terminal input is slow and arrives a keystroke at a time; modem output
    // is a BBS at full speed. Sizing them differently keeps the total small,
    // and these buffers are internal DRAM -- StreamBuffer storage cannot live
    // in PSRAM.
    static constexpr size_t RX_BYTES = 256;
    static constexpr size_t TX_BYTES = 1024;

    ModemPort() = default;
    ~ModemPort();

    ModemPort(const ModemPort &) = delete;
    ModemPort &operator=(const ModemPort &) = delete;

    // Idempotent. Returns false when either buffer cannot be allocated, which
    // the caller reports rather than entering modem mode half-wired.
    bool begin();
    void end();
    bool isOpen() const { return rx_ != nullptr && tx_ != nullptr; }

    // Shell task -> modem task.
    size_t pushRx(const uint8_t *buf, size_t n, uint32_t timeout_ms);
    // Modem task <- shell task.
    size_t popRx(uint8_t *buf, size_t n, uint32_t timeout_ms);

    // Modem task -> shell task.
    size_t pushTx(const uint8_t *buf, size_t n, uint32_t timeout_ms);
    // Shell task <- modem task.
    size_t popTx(uint8_t *buf, size_t n, uint32_t timeout_ms);

    // True for the one port that receives stream-mode data. Command-mode
    // responses go to every open port so both consoles see the modem's state;
    // stream data goes only to the attached one.
    void setAttached(bool a) { attached_ = a; }
    bool attached() const { return attached_; }

private:
    StreamBufferHandle_t rx_ = nullptr;
    StreamBufferHandle_t tx_ = nullptr;
    volatile bool        attached_ = false;
};

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM_PORT
