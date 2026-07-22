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

#include "body_capture.h"

#include <cstdlib>
#include <cstring>
#include <esp_heap_caps.h>

#include "../../include/debug.h"

// The real socket read behind the session recv function. Declared in the
// component's private header (esp_httpd_priv.h), but a plain exported symbol --
// the same access the WebDAV code already relies on for httpd_recv(). Wrapping
// it (rather than calling recv() directly) keeps the component's timeout and
// control-socket semantics intact.
extern "C" int httpd_default_recv(httpd_handle_t hd, int sockfd, char *buf, size_t buf_len, int flags);

namespace WebDav
{
    namespace
    {
        struct BodyCapture
        {
            char  *buf;
            size_t cap;
            size_t len;
            bool   overflow;       // grew past cap: buffer no longer complete
            bool   reset_pending;  // clear on the next byte (new request)
        };

        void *cap_malloc(size_t n)
        {
            void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            return p ? p : malloc(n);   // fall back to internal RAM
        }

        void body_capture_free(void *ctx)
        {
            BodyCapture *cap = (BodyCapture *)ctx;
            if (!cap)
                return;
            free(cap->buf);
            free(cap);
        }

        // Session recv override: read as the stock server would, then tee the
        // bytes into the capture buffer.
        int body_capture_recv(httpd_handle_t hd, int sockfd, char *buf, size_t buf_len, int flags)
        {
            BodyCapture *cap = (BodyCapture *)httpd_sess_get_transport_ctx(hd, sockfd);
            int r = httpd_default_recv(hd, sockfd, buf, buf_len, flags);
            if (cap && r > 0)
            {
                if (cap->reset_pending)
                {
                    cap->len = 0;
                    cap->overflow = false;
                    cap->reset_pending = false;
                }
                if (!cap->overflow)
                {
                    if (cap->len + (size_t)r <= cap->cap)
                    {
                        memcpy(cap->buf + cap->len, buf, (size_t)r);
                        cap->len += (size_t)r;
                    }
                    else
                    {
                        cap->overflow = true;
                    }
                }
            }
            return r;
        }
    } // namespace

    void body_capture_install(httpd_handle_t hd, int sockfd)
    {
        BodyCapture *cap = (BodyCapture *)cap_malloc(sizeof(BodyCapture));
        if (!cap)
            return;
        cap->buf = (char *)cap_malloc(BODY_CAPTURE_CAP);
        if (!cap->buf)
        {
            free(cap);
            return;
        }
        cap->cap = BODY_CAPTURE_CAP;
        cap->len = 0;
        cap->overflow = false;
        cap->reset_pending = false;

        httpd_sess_set_transport_ctx(hd, sockfd, cap, body_capture_free);
        httpd_sess_set_recv_override(hd, sockfd, body_capture_recv);
    }

    void body_capture_reset(httpd_req_t *req)
    {
        if (!req)
            return;
        BodyCapture *cap = (BodyCapture *)httpd_sess_get_transport_ctx(req->handle,
                                                                       httpd_req_to_sockfd(req));
        if (cap)
            cap->reset_pending = true;
    }

    bool body_capture_get(httpd_req_t *req, const char **body, size_t *body_len)
    {
        if (!req)
            return false;
        BodyCapture *cap = (BodyCapture *)httpd_sess_get_transport_ctx(req->handle,
                                                                       httpd_req_to_sockfd(req));
        if (!cap || cap->overflow)
            return false;

        // Locate the CRLFCRLF that ends the headers; the body follows it.
        for (size_t i = 3; i < cap->len; ++i)
        {
            if (cap->buf[i - 3] == '\r' && cap->buf[i - 2] == '\n' &&
                cap->buf[i - 1] == '\r' && cap->buf[i] == '\n')
            {
                *body = cap->buf + i + 1;
                *body_len = cap->len - (i + 1);
                return true;
            }
        }
        return false;
    }

} // namespace WebDav
