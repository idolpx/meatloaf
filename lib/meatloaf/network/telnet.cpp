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

#include "telnet.h"

#ifdef ENABLE_MODEM

#include <cstring>

#include "tcp.h"

TelnetMStream::TelnetMStream(std::string url, std::shared_ptr<MStream> inner)
    : MStream(url), inner_(inner)
{
}

TelnetMStream::~TelnetMStream()
{
    close();
}

bool TelnetMStream::isOpen()
{
    return open_ && inner_ != nullptr && inner_->isOpen();
}

bool TelnetMStream::open(std::ios_base::openmode m)
{
    if (inner_ == nullptr)
        return false;

    if (!inner_->isOpen() && !inner_->open(m))
    {
        Debug_printv("telnet: inner stream failed to open url[%s]", url.c_str());
        return false;
    }

    // The filter writes negotiation responses straight back to the inner
    // stream. Capturing `this` is safe: the filter is a member and cannot
    // outlive the stream.
    bool ok = filter_.begin(
        [this](const uint8_t *b, size_t n) {
            for (size_t i = 0; i < n; ++i)
                rx_.push_back(b[i]);
        },
        [this](const uint8_t *b, size_t n) {
            if (inner_ != nullptr)
                inner_->write(b, (uint32_t)n);
        });

    if (!ok)
    {
        Debug_printv("telnet: telnet_init failed url[%s]", url.c_str());
        inner_->close();
        return false;
    }

    mode = m;
    open_ = true;

    // Nothing is sent proactively. libtelnet emits our WILL/DO offers as the
    // remote's negotiation arrives, which keeps a server that negotiates
    // nothing from seeing bytes it never asked for.
    return true;
}

void TelnetMStream::close()
{
    filter_.end();
    rx_.clear();
    if (inner_ != nullptr)
        inner_->close();
    open_ = false;
}

bool TelnetMStream::pump()
{
    if (inner_ == nullptr || !inner_->isOpen())
        return false;

    uint8_t buf[CHUNK];
    uint32_t got = inner_->read(buf, CHUNK);

    // The inner stream is TCPMStream, whose read() passes MeatSocket::read()'s
    // int return straight back as uint32_t. A non-blocking socket with
    // nothing waiting returns _MEAT_NO_DATA_AVAIL (0xFFFFFFFEu, i.e. -2), and
    // "not open" returns -100 -- both land here as a count far larger than a
    // real read can produce. Treating only 0 as "nothing yet" and passing
    // `got` straight to filter_.receive() would hand it that sentinel as a
    // byte count against a 512-byte stack buffer: exactly the
    // _MEAT_NO_DATA_AVAIL-as-length defect AGENTS.md documents for
    // mfilebuf::underflow(). A real read can never exceed what was asked for,
    // so anything over CHUNK is rejected the same as 0.
    if (got == 0 || got > CHUNK)
        return inner_->isOpen();

    // Appends payload to rx_ and writes any negotiation reply to inner_.
    filter_.receive(buf, got);
    return true;
}

uint32_t TelnetMStream::read(uint8_t *buf, uint32_t size)
{
    if (buf == nullptr || size == 0)
        return 0;

    if (rx_.empty())
        pump();

    uint32_t n = 0;
    while (n < size && !rx_.empty())
    {
        buf[n++] = rx_.front();
        rx_.pop_front();
    }
    return n;
}

uint32_t TelnetMStream::write(const uint8_t *buf, uint32_t size)
{
    if (!isOpen() || buf == nullptr || size == 0)
        return 0;

    // The filter escapes a literal 0xFF and writes through to inner_.
    filter_.transmit(buf, size);

    // Report the caller's own count: the wire count differs whenever an IAC
    // was doubled, and a caller told "more bytes written than I gave you"
    // would treat it as an error.
    return size;
}

uint32_t TelnetMStream::available()
{
    uint32_t n = (uint32_t)rx_.size();
    if (inner_ != nullptr)
        n += inner_->available();
    return n;
}

bool TelnetMStream::waitReadable(uint32_t timeout_ms)
{
    // Decoded payload already waiting.
    if (!rx_.empty())
        return true;

    // A socket read can be entirely negotiation, leaving no payload -- so
    // "the inner stream is readable" is not the same question as "this stream
    // is readable", and the wait has to be re-entered rather than returned
    // from. The inner waitReadable() does the actual blocking; this loop only
    // re-checks after each pump.
    //
    // TCPMStream::available() is hardcoded to return 0 (tcp.h), so its
    // inherited MStream::waitReadable() can never see available() > 0 and
    // always returns false once its slice elapses -- its answer is not a
    // usable readability signal. pump() runs unconditionally after every
    // slice instead of only when the inner wait claims data is ready: the
    // underlying recv() is non-blocking (MeatSocket::blocking is never set),
    // so an extra call costs nothing, and it is the only way this loop
    // actually discovers arriving bytes.
    uint32_t waited = 0;
    for (;;)
    {
        if (!isOpen())
            return false;

        uint32_t remaining = (timeout_ms > waited) ? (timeout_ms - waited) : 0;
        uint32_t slice = (remaining > 50) ? 50 : remaining;

        if (inner_ != nullptr)
            inner_->waitReadable(slice);

        if (!pump())
            return false;
        if (!rx_.empty())
            return true;

        waited += (slice == 0) ? 1 : slice;
        if (waited >= timeout_ms)
            return !rx_.empty();
    }
}

std::shared_ptr<MStream> TelnetMFile::createStream(std::ios_base::openmode mode)
{
    // Reuse tcp:// verbatim for the transport. Building a TCPMFile from the
    // rewritten URL rather than a TCPMStream directly keeps the SessionBroker
    // bookkeeping identical to a plain tcp:// dial.
    std::string tcp_url = url;
    if (mstr::startsWith(tcp_url, (char *)"telnet:", false))
        tcp_url = "tcp:" + tcp_url.substr(strlen("telnet:"));

    auto tcp_file = std::shared_ptr<MFile>(MFSOwner::File(tcp_url));
    if (tcp_file == nullptr)
    {
        Debug_printv("telnet: could not resolve transport url[%s]", tcp_url.c_str());
        return nullptr;
    }

    auto inner = tcp_file->getSourceStream(mode);
    if (inner == nullptr)
    {
        Debug_printv("telnet: no transport stream url[%s]", tcp_url.c_str());
        return nullptr;
    }

    return std::make_shared<TelnetMStream>(url, inner);
}

#endif // ENABLE_MODEM
