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

#include "request.h"

#include <esp_http_server.h>
#include <cctype>
#include <string>

// Not in esp_http_server.h, but a plain (non-static) function of the
// esp_http_server component: like the recv() behind httpd_req_recv, but
// without the Content-Length cap (remaining_len), and it drains the parser's
// pending buffer first -- both required to read a chunked body, where
// content_len is 0 and the first body bytes may already sit in that buffer.
extern "C" int httpd_recv(httpd_req_t *r, char *buf, size_t buf_len);

using namespace WebDav;

bool Request::parseRequest() {
    std::string s;

    s = getHeader("Overwrite");
    if (!s.empty()) {
        if (s == "F")
            overwrite = false;
        else if (s != "T")
            return false;
    }

    s = getHeader("Depth");
    if (!s.empty()) {
        if (s == "0")
            depth = DEPTH_0;
        else if (s == "1")
            depth = DEPTH_1;
        else if (s != "infinity")
            return false;
    }

    return true;
}

bool Request::isChunked() {
    if (!chunkedKnown) {
        std::string te = getHeader("Transfer-Encoding");
        for (auto &c : te)
            c = tolower((unsigned char)c);
        chunkedFlag = te.find("chunked") != std::string::npos;
        chunkedKnown = true;
    }
    return chunkedFlag;
}

// A client that sent "Expect: 100-continue" holds the body back until we
// answer; esp_http_server never does, so we must (macOS Finder stalls its
// PUT body without this).
void Request::handleExpect() {
    if (continueSent)
        return;
    if (getHeader("Expect").empty())
        return;
    sendContinue();
    continueSent = true;
}

// A recv timeout is NOT an error -- it only means the next TCP segment has not
// arrived yet, and the client may still be mid-body. macOS webdavfs routinely
// sends a chunk's data and the CRLF that terminates it in separate segments,
// with a gap longer than the socket's recv_wait_timeout; treating that timeout
// as fatal threw away a fully-received body and answered 500, leaving the
// zero-length file the placeholder PUT had created. Retry a bounded number of
// times (the Content-Length path has always retried this way) and only fail on
// a real socket error or a close.
int Request::recvRaw(char *buf, size_t len) {
    for (int tries = 0; tries < RECV_TIMEOUT_RETRIES; ++tries) {
        int r = httpd_recv(req, buf, len);
        if (r != HTTPD_SOCK_ERR_TIMEOUT)
            return r;   // data, peer close (0), or a genuine error
    }

    Debug_printv("recv timed out %d times, giving up len[%d]",
                 RECV_TIMEOUT_RETRIES, (int)len);
    return -1;
}

bool Request::recvByte(char *c) {
    return recvRaw(c, 1) == 1;
}

// Read one CRLF-terminated framing line (chunk-size line or trailer) into
// line[], NUL-terminated, CR/LF stripped. False on socket error, a stray CR,
// or an overlong line.
bool Request::readChunkLine(char *line, size_t cap) {
    size_t n = 0;
    for (;;) {
        char c;
        if (!recvByte(&c))
            return false;
        if (c == '\r') {
            if (!recvByte(&c) || c != '\n')
                return false;
            break;
        }
        if (c == '\n')      // tolerate bare LF
            break;
        if (n + 1 >= cap)
            return false;
        line[n++] = c;
    }
    line[n] = 0;
    return true;
}

int Request::readBodyChunked(char *buf, int len) {
    if (broken)
        return -1;
    if (chunksDone || len <= 0)
        return 0;

    if (chunkLeft == 0) {
        // chunk-size line: hex digits, then optional ";ext" up to CRLF
        char line[128];
        if (!readChunkLine(line, sizeof(line))) {
            Debug_printv("bad chunk-size line");
            return chunkFail();
        }
        size_t size = 0;
        int i = 0;
        bool any = false;
        for (; line[i]; ++i) {
            char c = line[i];
            unsigned d;
            if (c >= '0' && c <= '9')      d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            if (size > 0x0FFFFFFF)         // absurd chunk size: bail out
                return chunkFail();
            size = (size << 4) | d;
            any = true;
        }
        if (!any)
            return chunkFail();
        if (line[i] && line[i] != ';' && line[i] != ' ' && line[i] != '\t')
            return chunkFail();
        if (size == 0) {
            // trailer section: header lines until an empty line
            for (;;) {
                if (!readChunkLine(line, sizeof(line)))
                    return chunkFail();
                if (!line[0])
                    break;
            }
            chunksDone = true;
            return 0;
        }
        chunkLeft = size;
    }

    int want = (size_t)len < chunkLeft ? len : (int)chunkLeft;
    int r = recvRaw(buf, want);
    if (r <= 0)
        return chunkFail();
    chunkLeft -= r;
    bodyDone += r;

    if (chunkLeft == 0) {
        // Stop here if the client told us the body length and we now have it.
        // macOS webdavfs holds back the CRLF + 0-chunk that close the body
        // until it has seen a response, so reading on would deadlock: it waits
        // for our reply, we wait for its framing. The bytes we leave unread
        // desync the connection, so it must not be reused -- see
        // connectionReusable().
        size_t expected = expectedEntityLength();
        if (expected > 0 && bodyDone >= expected) {
            chunksDone = true;
            unterminated = true;
            return r;
        }

        // consume the CRLF that closes the chunk data
        char c;
        if (!recvByte(&c))
            return chunkFail();
        if (c == '\r' && !recvByte(&c))
            return chunkFail();
        if (c != '\n')
            return chunkFail();
    }
    return r;
}

size_t Request::expectedEntityLength() {
    if (!expectedKnown) {
        std::string v = getHeader("X-Expected-Entity-Length");
        expectedLen = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] < '0' || v[i] > '9') {
                expectedLen = 0;        // not a plain number: ignore it
                break;
            }
            expectedLen = expectedLen * 10 + (v[i] - '0');
        }
        expectedKnown = true;
    }
    return expectedLen;
}

std::string Request::getDestination() {
    std::string destination = getHeader("Destination");
    std::string host = getHeader("Host");

    if (destination.empty() || host.empty())
        return "";

    size_t pos = destination.find(host);
    if (pos == std::string::npos)
        return "";

    return destination.substr(pos + host.length());
}
