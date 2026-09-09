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

// Telnet option negotiation, as a pure byte filter.
//
// This deliberately does NOT inherit MStream. It is a byte-in/byte-out unit
// over two sinks, which is what makes libtelnet's negotiation reachable from
// test/native/test_modem_at. TelnetMStream in lib/meatloaf/network/telnet.h is
// a thin wrapper over it.
//
// Without this, a BBS that negotiates sprays IAC bytes onto the user's screen.

#ifndef MEATLOAF_MODEM_TELNET_FILTER
#define MEATLOAF_MODEM_TELNET_FILTER

#ifdef ENABLE_MODEM

#include <cstddef>
#include <cstdint>
#include <functional>

extern "C" {
#include "libtelnet.h"
}

class TelnetFilter
{
public:
    using Sink = std::function<void(const uint8_t *buf, size_t n)>;

    TelnetFilter() = default;
    ~TelnetFilter();

    TelnetFilter(const TelnetFilter &) = delete;
    TelnetFilter &operator=(const TelnetFilter &) = delete;

    // to_app receives payload bytes destined for the terminal.
    // to_peer receives bytes that must be written to the socket -- both
    // negotiation responses and escaped outbound data.
    bool begin(Sink to_app, Sink to_peer);

    // Idempotent, so a caller may end() on error and again in a destructor.
    void end();

    bool isOpen() const { return telnet_ != nullptr; }

    // Bytes read from the remote. Negotiation is answered on to_peer and never
    // reaches to_app; a doubled IAC arrives as one literal 0xFF. A no-op after
    // end(), because the stream's close() path can race a final read.
    void receive(const uint8_t *buf, size_t n);

    // Bytes from the terminal. A literal 0xFF is doubled on the wire, or the
    // far end reads it as the start of a command.
    void transmit(const uint8_t *buf, size_t n);

private:
    static void handler(telnet_t *t, telnet_event_t *ev, void *user);

    telnet_t *telnet_ = nullptr;
    Sink to_app_;
    Sink to_peer_;
};

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM_TELNET_FILTER
