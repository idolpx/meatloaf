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

#include "telnet_filter.h"

#ifdef ENABLE_MODEM

namespace
{

// What Meatloaf offers and accepts as a telnet CLIENT.
//
// BINARY both ways so 8-bit data (ANSI art, high ASCII) survives; SGA both
// ways because a BBS is character-at-a-time, not line-at-a-time; the server
// may ECHO but we never will. TTYPE and NAWS are declined -- there is no
// terminal here to report a type or a size for, and offering them invites
// subnegotiation this filter would then have to answer.
const telnet_telopt_t kTelopts[] = {
    { TELNET_TELOPT_BINARY, TELNET_WILL, TELNET_DO   },
    { TELNET_TELOPT_SGA,    TELNET_WILL, TELNET_DO   },
    { TELNET_TELOPT_ECHO,   TELNET_WONT, TELNET_DO   },
    { TELNET_TELOPT_TTYPE,  TELNET_WONT, TELNET_DONT },
    { TELNET_TELOPT_NAWS,   TELNET_WONT, TELNET_DONT },
    { -1, 0, 0 }
};

} // namespace

TelnetFilter::~TelnetFilter()
{
    end();
}

void TelnetFilter::handler(telnet_t *t, telnet_event_t *ev, void *user)
{
    (void)t;
    TelnetFilter *self = (TelnetFilter *)user;
    if (self == nullptr)
        return;

    switch (ev->type)
    {
    case TELNET_EV_DATA:
        if (self->to_app_ && ev->data.size > 0)
            self->to_app_((const uint8_t *)ev->data.buffer, ev->data.size);
        break;

    case TELNET_EV_SEND:
        if (self->to_peer_ && ev->data.size > 0)
            self->to_peer_((const uint8_t *)ev->data.buffer, ev->data.size);
        break;

    default:
        // Every other event is negotiation libtelnet has already answered on
        // our behalf via TELNET_EV_SEND. Nothing to do, and in particular
        // nothing to forward -- that is the whole point of the filter.
        break;
    }
}

bool TelnetFilter::begin(Sink to_app, Sink to_peer)
{
    end();

    to_app_ = to_app;
    to_peer_ = to_peer;

    // Flags 0: not a proxy, and no NVT end-of-line translation. A BBS session
    // is a byte pipe; rewriting CR/LF here would corrupt ANSI positioning.
    telnet_ = telnet_init(kTelopts, &TelnetFilter::handler, 0, this);
    if (telnet_ == nullptr)
    {
        to_app_ = nullptr;
        to_peer_ = nullptr;
        return false;
    }
    return true;
}

void TelnetFilter::end()
{
    if (telnet_ != nullptr)
    {
        telnet_free(telnet_);
        telnet_ = nullptr;
    }
    to_app_ = nullptr;
    to_peer_ = nullptr;
}

void TelnetFilter::receive(const uint8_t *buf, size_t n)
{
    if (telnet_ == nullptr || buf == nullptr || n == 0)
        return;
    telnet_recv(telnet_, (const char *)buf, n);
}

void TelnetFilter::transmit(const uint8_t *buf, size_t n)
{
    if (telnet_ == nullptr || buf == nullptr || n == 0)
        return;
    // telnet_send, not telnet_send_text: send_text applies NVT end-of-line
    // translation, which corrupts ANSI cursor sequences.
    telnet_send(telnet_, (const char *)buf, n);
}

#endif // ENABLE_MODEM
