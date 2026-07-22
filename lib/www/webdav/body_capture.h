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

#include <esp_http_server.h>
#include <cstddef>

// Per-connection raw request capture ("tee") for reading chunked request bodies
// without forking esp_http_server.
//
// esp_http_server cannot hand a Transfer-Encoding: chunked request body to a
// URI handler: http_parser de-chunks the body itself and consumes the first
// chunk-size line before the handler runs, so by handler time the raw framing
// is gone and content_len is 0. The documented workaround is to override the
// session recv function (httpd_sess_set_recv_override, a public API) and read
// the raw stream yourself.
//
// This installs such an override at connection open: it copies every byte the
// parser reads off the socket into a small per-session buffer. The WebDAV
// handler then locates the request body inside that buffer (the bytes after the
// CRLFCRLF that ends the headers) and de-chunks straight from it, falling
// through to the socket for whatever had not been read yet. See
// WebDav::Request::readBodyChunked.
//
// The buffer is bounded and reset per request; on overflow it stops copying, so
// a large POST or a websocket stream on the same server never makes it grow.
// Only small WebDAV request bodies (Finder PUTs) rely on it, and they fit well
// inside the cap.

namespace WebDav
{
    // Cap on the captured prefix. Must exceed the largest request head plus the
    // leading body bytes the parser reads in the same block -- a few hundred
    // bytes in practice. 4 KB is comfortable and bounds per-connection memory.
    static const size_t BODY_CAPTURE_CAP = 4096;

    // Install the recv override + capture context on a freshly opened
    // connection. Call from the server's open_fn. A no-op on allocation
    // failure -- the connection then behaves exactly as the stock server would
    // (a chunked PUT would still write nothing, but nothing crashes).
    void body_capture_install(httpd_handle_t hd, int sockfd);

    // Flag the current request's capture as consumed so the next request on a
    // kept-alive connection starts fresh. Call once per request (all methods).
    void body_capture_reset(httpd_req_t *req);

    // Hand back the captured raw request body for this request: the bytes after
    // the end-of-headers marker. Returns false when there is no capture
    // context, the CRLFCRLF was not found, or the capture overflowed -- in all
    // of which the caller must not trust the buffer. *body points into the live
    // capture buffer (valid until the connection closes); *body_len is its
    // length.
    bool body_capture_get(httpd_req_t *req, const char **body, size_t *body_len);

} // namespace WebDav
