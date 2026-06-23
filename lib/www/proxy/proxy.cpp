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

#ifndef MIN_CONFIG
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "proxy.h"
#include "../web_server.h"

#include "../include/debug.h"
#include "string_utils.h"

#include <esp_heap_caps.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <utility>

static inline void *psram_malloc(size_t sz)
{
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(sz);
}

// Hop-by-hop headers that must not be forwarded to the client.
// Content-Length is also excluded because we use chunked transfer.
static const char * const kHopByHop[] = {
    "Connection", "Keep-Alive", "Transfer-Encoding", "Content-Length",
    "TE", "Trailers", "Upgrade", "Proxy-Authenticate", "Proxy-Authorization",
    nullptr
};

struct ProxyRespInfo {
    std::string content_type;
    std::vector<std::pair<std::string, std::string>> headers;
};

// Stored origin from the last /proxy request; empty means proxy is inactive.
// Single-user embedded device — global state is appropriate here.
static std::string s_proxy_base_url;

const std::string &proxy_base_url() { return s_proxy_base_url; }
void proxy_clear_base()             { s_proxy_base_url.clear(); }

static std::string extract_origin(const std::string &url)
{
    size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return "";
    size_t path_start = url.find('/', scheme_end + 3);
    return (path_start == std::string::npos) ? url : url.substr(0, path_start);
}

static esp_err_t proxy_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_HEADER || !evt->user_data)
        return ESP_OK;
    ProxyRespInfo *info = static_cast<ProxyRespInfo *>(evt->user_data);
    if (strcasecmp(evt->header_key, "Content-Type") == 0)
        info->content_type = evt->header_value;
    else
        info->headers.emplace_back(evt->header_key, evt->header_value);
    return ESP_OK;
}

esp_err_t proxy_fetch(httpd_req_t *req, const char *target_url)
{
    esp_http_client_method_t method =
        (req->method == HTTP_POST) ? HTTP_METHOD_POST : HTTP_METHOD_GET;

    Debug_printv("proxy_fetch method[%d] url[%s]", method, target_url);

    ProxyRespInfo resp_info;
    esp_http_client_config_t cfg = {};
    cfg.url           = target_url;
    cfg.method        = method;
    cfg.timeout_ms    = 10000;
    cfg.event_handler = proxy_event_handler;
    cfg.user_data     = &resp_info;
    cfg.skip_cert_common_name_check = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        HttpServer::send_http_error(req, 500);
        return ESP_OK;
    }

    // ESP-IDF httpd has no header-enumeration API so we probe a known set.
    // For each header we prefer the X-prefixed version (stripping "X-" before
    // forwarding) so that CORS-constrained clients can pass X-Authorization etc.
    static const char * const kFwdHeaders[] = {
        "Accept", "Accept-Encoding", "Accept-Language",
        "Authorization", "Cache-Control", "Client-Id", "Content-Type",
        "If-Modified-Since", "If-None-Match",
        "Origin", "Referer", "Host", "User-Agent",
        nullptr
    };
    for (int i = 0; kFwdHeaders[i]; i++) {
        std::string xname = std::string("X-") + kFwdHeaders[i];
        const char *candidates[2] = { xname.c_str(), kFwdHeaders[i] };
        for (const char *hdr : candidates) {
            size_t hlen = httpd_req_get_hdr_value_len(req, hdr);
            if (hlen == 0) continue;
            char *val = (char *)malloc(hlen + 1);
            if (!val) continue;
            if (httpd_req_get_hdr_value_str(req, hdr, val, hlen + 1) == ESP_OK) {
                esp_http_client_set_header(client, kFwdHeaders[i], val);
                Debug_printv("  fwd header[%s: %s]", kFwdHeaders[i], val);
            }
            free(val);
            break;
        }
    }

    // Set Referer unless the client supplied an explicit X-Referer.
    // Prefer the stored proxy base; fall back to the target URL itself.
    if (httpd_req_get_hdr_value_len(req, "X-Referer") == 0) {
        const char *ref = s_proxy_base_url.empty() ? target_url : s_proxy_base_url.c_str();
        esp_http_client_set_header(client, "Referer", ref);
        Debug_printv("  upd header[%s: %s]", "Referer", ref);
    }

    int write_len = (req->method == HTTP_POST) ? (int)req->content_len : 0;
    if (esp_http_client_open(client, write_len) != ESP_OK) {
        esp_http_client_cleanup(client);
        HttpServer::send_http_error(req, 502);
        return ESP_OK;
    }

    if (write_len > 0) {
        char *ibuf = (char *)psram_malloc(http_RECV_BUFF_SIZE);
        if (ibuf) {
            int remaining = write_len;
            while (remaining > 0) {
                int chunk = std::min<int>(remaining, http_RECV_BUFF_SIZE);
                int got = httpd_req_recv(req, ibuf, chunk);
                if (got <= 0) break;
                esp_http_client_write(client, ibuf, got);
                remaining -= got;
            }
            free(ibuf);
        }
    }

    esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    if (status_code <= 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        HttpServer::send_http_error(req, 502);
        return ESP_OK;
    }

    char status_str[16];
    snprintf(status_str, sizeof(status_str), "%d", status_code);
    httpd_resp_set_status(req, status_str);

    if (!resp_info.content_type.empty())
        httpd_resp_set_type(req, resp_info.content_type.c_str());

    // Forward upstream response headers, skipping hop-by-hop ones.
    // resp_info lives on this stack frame and remains valid until send_chunk below.
    for (auto &h : resp_info.headers) {
        bool skip = false;
        for (int i = 0; kHopByHop[i]; i++)
            if (strcasecmp(h.first.c_str(), kHopByHop[i]) == 0) { skip = true; break; }
        if (!skip)
            httpd_resp_set_hdr(req, h.first.c_str(), h.second.c_str());
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char *obuf = (char *)psram_malloc(http_SEND_BUFF_SIZE);
    if (obuf) {
        int n;
        while ((n = esp_http_client_read(client, obuf, http_SEND_BUFF_SIZE)) > 0)
            httpd_resp_send_chunk(req, obuf, n);
        free(obuf);
    }
    httpd_resp_send_chunk(req, nullptr, 0);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}

esp_err_t proxy_handler(httpd_req_t *req)
{
    const char *q = strchr(req->uri, '?');
    if (!q || *(q + 1) == '\0') {
        // No URL argument — clear the stored proxy base
        proxy_clear_base();
        Debug_printv("proxy: base cleared");
        httpd_resp_send(req, "OK", 2);
        return ESP_OK;
    }
    std::string target_url = mstr::urlDecode(q + 1);

    // Strip the fragment before forwarding (fragments are not sent in HTTP requests).
    // If the fragment contains "base=1", store the origin as the active proxy base.
    size_t frag = target_url.find('#');
    if (frag != std::string::npos) {
        bool set_base = target_url.find("base=1", frag + 1) != std::string::npos;
        target_url = target_url.substr(0, frag);
        if (set_base) {
            s_proxy_base_url = extract_origin(target_url);
            Debug_printv("proxy: base set to [%s]", s_proxy_base_url.c_str());
        }
    }

    return proxy_fetch(req, target_url.c_str());
}

void proxy_register(httpd_handle_t server)
{
    httpd_uri_t proxy = {
        .uri = "/proxy",
        .method = HTTP_GET,
        .handler = proxy_handler,
        .user_ctx = NULL,
        .is_websocket = false
    };
    httpd_register_uri_handler(server, &proxy);
    proxy.method = HTTP_POST;
    httpd_register_uri_handler(server, &proxy);
}

#endif // MIN_CONFIG
