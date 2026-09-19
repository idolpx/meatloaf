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

#include <chrono>
#include <cstring>

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
    if (!open_ || inner_ == nullptr)
        return false;

    // The remote sent EOF: keep answering true until rx_ has drained (a
    // caller mid-read still needs what already arrived), then false for
    // good -- a closed TCP connection never produces more bytes.
    //
    // rx_ is tested BEFORE the inner stream, not after. TCPMStream::isOpen()
    // now answers false the moment its read() sees the peer's FIN, so asking
    // it first would discard bytes this stream has already decoded out of the
    // last chunk before that FIN -- the drain grace would exist only for a
    // hangup this stream noticed before the one underneath it did, which is
    // never.
    if (!rx_.empty())
        return true;

    if (eof_)
        return false;

    return inner_->isOpen();
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
            if (inner_ == nullptr)
            {
                tx_error_ = true;
                return;
            }
            uint32_t written = inner_->write(b, (uint32_t)n);
            if (written != (uint32_t)n)
                tx_error_ = true;
        });

    if (!ok)
    {
        Debug_printv("telnet: telnet_init failed url[%s]", url.c_str());
        inner_->close();
        return false;
    }

    mode = m;
    open_ = true;
    eof_ = false;
    tx_error_ = false;
    _error = 0;

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
    eof_ = false;
    tx_error_ = false;
}

bool TelnetMStream::pump()
{
    if (inner_ == nullptr || !inner_->isOpen())
        return false;

    // Already at EOF: nothing further will ever arrive from this socket.
    if (eof_)
        return false;

    uint8_t buf[CHUNK];
    uint32_t got = inner_->read(buf, CHUNK);

    // recv(2) returning exactly 0 means the remote performed an orderly
    // shutdown -- genuine EOF, not "no data right now". Conflating the two
    // left isOpen() true forever on a dead connection and waitReadable()
    // burning its full timeout on every call: recv() returning 0 is the
    // proof the peer is gone, and inner_->isOpen() alone can't tell (the
    // local socket descriptor stays valid after a remote FIN).
    if (got == 0)
    {
        eof_ = true;
        return false;
    }

    // The inner stream is TCPMStream, whose read() passes MeatSocket::read()'s
    // int return straight back as uint32_t. A non-blocking socket with
    // nothing waiting (as opposed to closed) returns _MEAT_NO_DATA_AVAIL
    // (0xFFFFFFFEu, i.e. -2), and "not open" returns -100 -- both land here
    // as a count far larger than a real read can produce. Passing `got`
    // straight to filter_.receive() would hand it that sentinel as a byte
    // count against a 512-byte stack buffer: exactly the
    // _MEAT_NO_DATA_AVAIL-as-length defect AGENTS.md documents for
    // mfilebuf::underflow(). A real read can never exceed what was asked
    // for, so anything over CHUNK is rejected the same as a would-block.
    if (got > CHUNK)
        return inner_->isOpen();

    // Appends payload to rx_ and writes any negotiation reply to inner_.
    filter_.receive(buf, got);
    return true;
}

uint32_t TelnetMStream::read(uint8_t *buf, uint32_t size)
{
    if (buf == nullptr || size == 0)
        return 0;

    // `!open_`, not `!isOpen()`. isOpen() also reports false once eof_ has
    // been set and rx_ has drained -- a session the peer has genuinely hung
    // up on. Guarding on isOpen() would treat that as "never opened" and
    // redial through open() on every subsequent call: churning
    // telnet_free()/telnet_init() forever on a dead connection (open()
    // skips re-opening inner_ since a local fd stays valid after a remote
    // FIN, so nothing here would ever detect the redial as pointless). open_
    // is only false before the first successful open() and after an explicit
    // close(), which is exactly when lazy-opening is wanted.
    if (!open_ && !open(std::ios_base::in))
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
    if (buf == nullptr || size == 0)
        return 0;

    // See the matching comment in read(): `!open_`, not `!isOpen()`, for the
    // same reason -- plus one more that bites specifically here. open()
    // resets `_error = 0` on success (a genuine reopen should clear a stale
    // error). Guarding on isOpen() would let a drained-EOF stream re-enter
    // open(), which would clear `_error` out from under a caller that has a
    // write failure still pending -- iecChannelHandlerFile's destructor
    // reads `_error` after close() to map it to drive status, so a real
    // failure would be silently reported as success.
    if (!open_ && !open(std::ios_base::out))
        return 0;

    if (!filter_.isOpen())
    {
        // begin() never succeeded (or end() already ran). transmit() would
        // silently no-op rather than fail, which without this check would
        // report success for a write that never reached the wire.
        _error = 1;
        return 0;
    }

    // Reset right before this call so the flag reflects only this transmit,
    // not an earlier negotiation reply that happened to go through the same
    // to_peer_ sink.
    tx_error_ = false;

    // The filter escapes a literal 0xFF and writes through to inner_.
    filter_.transmit(buf, size);

    if (tx_error_)
    {
        _error = 1;
        return 0;
    }

    // Report the caller's own count: the wire count differs whenever an IAC
    // was doubled, and a caller told "more bytes written than I gave you"
    // would treat it as an error.
    return size;
}

uint32_t TelnetMStream::available()
{
    return (uint32_t)rx_.size();
}

bool TelnetMStream::eos()
{
    // Mirrors read()/write()'s lazy-open guard (see the comment in read()).
    // A fresh, never-opened stream -- exactly what createStream() hands
    // back -- must not read as "finished": !isOpen() alone cannot tell
    // "never opened" apart from "drained and dead", and without opening
    // first a caller that checks eos() before its first read()/write() saw
    // a live, never-dialed connection reported as already over. If opening
    // now fails, there is genuinely nothing more this stream will ever
    // produce, so true is the correct answer either way.
    if (!open_ && !open(std::ios_base::in))
        return true;

    return !isOpen();
}

bool TelnetMStream::waitReadable(uint32_t timeout_ms)
{
    // Mirrors read()/write()'s lazy-open guard (see the comment in read()).
    // A fresh, never-opened stream -- exactly what createStream() hands
    // back -- must actually dial before anything below can mean anything:
    // without this, `while (waitReadable(...)) { read(...); }` on a freshly
    // dialed stream never opened the connection at all, because every check
    // in this function (isOpen(), rx_) reads as "nothing here yet" whether
    // the stream was never opened or is genuinely dead. If opening now
    // fails, nothing will ever become readable, so false is correct either
    // way.
    if (!open_ && !open(std::ios_base::in))
        return false;

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
    //
    // Elapsed time is measured with a real clock rather than accumulated
    // from the requested slice: inner_->waitReadable(slice) is only
    // guaranteed to wait UP TO slice ms, not exactly slice ms -- with
    // TCPMStream specifically it always burns the whole slice today
    // (available() is hardcoded 0, so its own poll loop never returns
    // early), but nothing here should depend on that happening to be true
    // of whatever inner_ is. Assuming the full slice elapsed every time
    // would let this loop give up before timeout_ms has genuinely passed.
    auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        if (!isOpen())
            return false;

        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        uint32_t waited = (elapsed_ms < 0) ? 0 : (uint32_t)elapsed_ms;
        if (waited >= timeout_ms)
            return !rx_.empty();

        uint32_t remaining = timeout_ms - waited;
        uint32_t slice = (remaining > 50) ? 50 : remaining;
        if (slice == 0)
            slice = 1;

        auto slice_start = std::chrono::steady_clock::now();

        if (inner_ != nullptr)
            inner_->waitReadable(slice);

        if (!pump())
            return false;
        if (!rx_.empty())
            return true;

        // inner_->waitReadable() is only guaranteed to wait UP TO slice ms
        // (see the comment above) -- an inner that returns early without
        // blocking (exactly what (f2)'s instant_wait constructs) must not
        // turn this into a hot spin with no yield, burning a core on a
        // single-core-shared FreeRTOS task for up to timeout_ms. Sleep out
        // whatever part of the slice the inner wait plus pump() did not
        // already consume. Harmless with a real inner_, which already
        // blocks for close to the full slice.
        auto slice_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - slice_start).count();
        if (slice_elapsed_ms < (int64_t)slice)
        {
            uint32_t remaining_slice = slice - (uint32_t)slice_elapsed_ms;
#ifdef ESP_PLATFORM
            // pdMS_TO_TICKS() truncates and CONFIG_FREERTOS_HZ is 100 here, so
            // anything under 10 ms is ZERO ticks and vTaskDelay(0) yields only
            // to equal-or-higher priority tasks -- which is exactly the hot
            // spin this sleep exists to prevent. Floor it at one tick.
            TickType_t slice_ticks = pdMS_TO_TICKS(remaining_slice);
            vTaskDelay(slice_ticks > 0 ? slice_ticks : 1);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(remaining_slice));
#endif
        }
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

    // If the rewrite did not change anything, MFSOwner::File() below would
    // resolve THIS SAME "telnet:" URL again -- TelnetMFileSystem::handles()
    // still matches it -- getting back another TelnetMFile, whose
    // getSourceStream() calls straight back into this function: infinite
    // recursion until the stack overflows, not a clean failure. Guard it
    // rather than trust startsWith() to always fire.
    if (tcp_url == url)
    {
        Debug_printv("telnet: URL did not rewrite to tcp: url[%s]", url.c_str());
        return nullptr;
    }

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
