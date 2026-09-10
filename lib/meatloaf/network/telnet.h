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

// TELNET:// - Telnet with option negotiation
// https://datatracker.ietf.org/doc/html/rfc854
//
// A decorator over tcp://. The transport is exactly TCPMStream; this layer
// only runs libtelnet over the byte stream, via TelnetFilter (lib/modem), so
// that IAC negotiation is answered instead of landing on the user's screen.
//
// Decorating rather than subclassing TCPMStream is the same pattern the
// media layer uses for decoder streams, and it is what lets ATD and ATDT be one dial
// path with two URL schemes.

#ifndef MEATLOAF_NETWORK_TELNET
#define MEATLOAF_NETWORK_TELNET

#ifdef ENABLE_MODEM

#include <deque>
#include <memory>
#include <string>

#include "meatloaf.h"
#include "telnet_filter.h"

#include "../../../include/debug.h"

class TelnetMStream : public MStream
{
public:
    // inner is an already-constructed (not necessarily open) byte stream,
    // normally a TCPMStream over the same host and port.
    TelnetMStream(std::string url, std::shared_ptr<MStream> inner);
    ~TelnetMStream() override;

    bool isOpen() override;
    bool isNetwork() override { return true; }
    bool isRandomAccess() override { return false; }

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t *buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    // A telnet session is a byte pipe with no position or extent.
    bool seek(uint32_t) override { return false; }
    uint32_t size() override { return 0; }
    uint32_t position() override { return 0; }

    // Payload bytes already decoded and waiting in rx_. Does NOT reflect
    // anything still sitting unread in the inner socket -- TCPMStream::
    // available() (tcp.h) is hardcoded to 0, so there is nothing meaningful
    // to add from it. Non-zero here does not guarantee read() returns MORE
    // data next time -- a socket read may be entirely negotiation and add
    // nothing to rx_ -- which is why waitReadable() pumps in a loop rather
    // than trusting a single available() check.
    uint32_t available() override;

    // available()==0 does not mean end of stream: an idle-but-live BBS
    // session reports it constantly between sends. The real signal is
    // isOpen() -- false once the peer has sent EOF and rx_ has drained.
    bool eos() override;

    bool waitReadable(uint32_t timeout_ms) override;

private:
    // Pulls one chunk off the inner stream and runs it through the filter,
    // which appends payload to rx_ and writes any response to the inner
    // stream. Returns false when the inner stream is closed/errored, or has
    // reached EOF with nothing left to pump. A genuine EOF (inner_->read()
    // returning exactly 0 while the inner stream is still open -- an orderly
    // remote close, not the "no data right now" sentinel) sets eof_ rather
    // than being indistinguishable from idle.
    bool pump();

    static constexpr uint32_t CHUNK = 512;

    std::shared_ptr<MStream> inner_;
    TelnetFilter             filter_;
    std::deque<uint8_t>      rx_;
    bool                     open_ = false;

    // Set by pump() when the remote has closed its end. isOpen() keeps
    // answering true until rx_ has drained, so a caller mid-read still gets
    // what already arrived; once rx_ is empty, isOpen() goes false for good
    // (a closed TCP connection never produces more bytes).
    bool eof_ = false;

    // Set by the to_peer_ sink (bound in open()) when writing a transmitted
    // byte through to inner_ does not fully succeed. write() resets this
    // immediately before calling filter_.transmit() and checks it right
    // after, so it reflects only that one call's outcome -- not an earlier
    // negotiation reply's write, which also goes through the same sink.
    bool tx_error_ = false;
};

class TelnetMFile : public MFile
{
public:
    TelnetMFile(std::string path) : MFile(path) {}
    ~TelnetMFile() override = default;

    // Like tcp://, a telnet URL is a connection, not a file, so it is never
    // wrapped in a decoder.
    std::shared_ptr<MStream> getSourceStream(
        std::ios_base::openmode mode = std::ios_base::in) override
    {
        return createStream(mode);
    }

    std::shared_ptr<MStream> getDecodedStream(
        std::shared_ptr<MStream> src) override
    {
        return src;
    }

    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override;

    bool isDirectory() override { return false; }
    bool exists() override { return true; }
    bool rewindDirectory() override { return false; }
    MFile *getNextFileInDir() override { return nullptr; }
    bool remove() override { return false; }
    bool rename(std::string) override { return false; }
    time_t getLastWrite() override { return 0; }
    time_t getCreationTime() override { return 0; }
    uint64_t getAvailableSpace() override { return 0; }
    bool mkDir() override { return false; }
};

class TelnetMFileSystem : public MFileSystem
{
public:
    TelnetMFileSystem() : MFileSystem("telnet") { isRootFS = true; }

    bool handles(std::string name) override
    {
        return mstr::startsWith(name, (char *)"telnet:", false);
    }

    MFile *getFile(std::string path) override { return new TelnetMFile(path); }
};

#endif // ENABLE_MODEM
#endif // MEATLOAF_NETWORK_TELNET
