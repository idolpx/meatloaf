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

#pragma once

#include <string>
#include <esp_http_server.h>

#include "string_utils.h"
#include "../../include/debug.h"

#define HTTPD_100      "100 Continue"

// How many consecutive socket recv timeouts to ride out before declaring the
// body lost. The socket's recv_wait_timeout is the unit, so this bounds the
// wait for a client that went quiet mid-body without hanging the (single-
// threaded) server on one that has died. See Request::recvRaw.
#define RECV_TIMEOUT_RETRIES 6

namespace WebDav
{

    class Request
    {
    public:
        enum Depth
        {
            DEPTH_0 = 0,
            DEPTH_1 = 1,
            DEPTH_INFINITY = 2,
        };

        Request(httpd_req_t *httpd_req)
        {
            req = httpd_req;
            path = httpd_req->uri;
            depth = DEPTH_INFINITY;
            overwrite = true;

            // Drop trailing slash from path
            if (mstr::endsWith(path, "/"))
                mstr::drop(path, 1);
        }

        bool parseRequest();
        std::string getDestination();

        // Chunked transfer encoding (macOS Finder PUTs bodies this way; the
        // esp_http_server can't dechunk, so we read the raw body captured by
        // the session recv override -- see body_capture.h -- and dechunk it
        // ourselves). readBodyChunked() returns >0 data bytes, 0 at
        // end-of-body, <0 on a socket/protocol error. After an error or an
        // undrained chunked body the connection byte stream is desynchronized;
        // connectionReusable() then says the handler must close the socket.
        bool isChunked();
        int readBodyChunked(char *buf, int len);
        void handleExpect();

        // macOS webdavfs (Finder) sends the whole body and then holds back the
        // CRLF + 0-chunk that terminate it until it has seen a response -- a
        // deadlock, since we are waiting for exactly those bytes. It gives us
        // the body length up front in X-Expected-Entity-Length, which is what
        // that header is for. 0 when absent.
        size_t expectedEntityLength();

        bool connectionReusable()
        {
            // unterminated: we stopped on the expected length without reading
            // the trailing framing, so the byte stream is out of step and the
            // socket must not be reused for another request.
            return !broken && !unterminated && (!isChunked() || chunksDone);
        }

        std::string getPath() { return path; }
        enum Depth getDepth() { return depth; }
        bool getOverwrite() { return overwrite; }

        // Functions that depend on the underlying web server implementation
        std::string getHeader(std::string name)
        {
            size_t len = httpd_req_get_hdr_value_len(req, name.c_str());
            if (len <= 0)
                return "";

            std::string s;
            s.resize(len);
            httpd_req_get_hdr_value_str(req, name.c_str(), &s[0], len + 1);

            return s;
        }

        void sendContinue()
        {
            std::string c = "HTTP/1.1 " HTTPD_100 "\r\n\r\n";
            httpd_send(req, c.c_str(), c.length());
        }

        size_t getContentLength()
        {
            if (!req)
                return 0;

            return req->content_len;
        }

        // Same timeout rule as recvRaw: a quiet moment mid-body is not the end
        // of the body. Retrying here rather than returning 0 matters because
        // doPut's Content-Length loop treats any <= 0 as end-of-stream and then
        // reports the short body as an error.
        int readBody(char *buf, int len)
        {
            for (int tries = 0; tries < RECV_TIMEOUT_RETRIES; ++tries)
            {
                int ret = httpd_req_recv(req, buf, len);
                if (ret != HTTPD_SOCK_ERR_TIMEOUT)
                    return ret;
            }

            Debug_printv("recv timed out %d times, giving up len[%d]",
                         RECV_TIMEOUT_RETRIES, len);
            return -1;
        }

    private:
        httpd_req_t *req;

        // Chunked-decoder state
        bool chunkedKnown = false;
        bool chunkedFlag = false;
        size_t chunkLeft = 0;       // data bytes left in the current chunk
        bool chunksDone = false;    // body complete
        bool broken = false;        // socket/protocol error mid-body
        bool continueSent = false;  // "100 Continue" already sent
        bool unterminated = false;  // stopped on the expected length, framing unread
        size_t bodyDone = 0;        // data bytes delivered so far
        bool expectedKnown = false;
        size_t expectedLen = 0;

        // Raw-body source: the prefix captured by the session recv override
        // (bytes the parser already read off the socket), then the socket for
        // the remainder. captureFailed means the prefix was unavailable, so a
        // chunked body cannot be read reliably.
        bool capInit = false;
        bool captureFailed = false;
        const char *capBuf = nullptr;
        size_t capPos = 0;
        size_t capEnd = 0;
        void initCapture();

        int recvRaw(char *buf, size_t len);
        bool recvByte(char *c);
        bool readChunkLine(char *line, size_t cap);
        int chunkFail()
        {
            broken = true;
            return -1;
        }

    protected:
        std::string path;
        enum Depth depth;
        bool overwrite;
    };

} // namespace
