#ifndef MIN_CONFIG
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "httpd_proxy.h"
#include "httpd_server.h"

#include "string_utils.h"

#include <esp_heap_caps.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>

static inline void *psram_malloc(size_t sz)
{
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(sz);
}

// Capture upstream response Content-Type for relay back to the client.
static esp_err_t proxy_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->user_data
            && strcasecmp(evt->header_key, "Content-Type") == 0)
        *static_cast<std::string *>(evt->user_data) = evt->header_value;
    return ESP_OK;
}

esp_err_t cHttpdServer::proxy_handler(httpd_req_t *req)
{
    // Target URL is the full query string: /proxy?http://host/path
    const char *q = strchr(req->uri, '?');
    if (!q || *(q + 1) == '\0') {
        send_http_error(req, 400);
        return ESP_OK;
    }
    std::string target_url = mstr::urlDecode(q + 1);

    esp_http_client_method_t method =
        (req->method == HTTP_POST) ? HTTP_METHOD_POST : HTTP_METHOD_GET;

    std::string resp_content_type;
    esp_http_client_config_t cfg = {};
    cfg.url           = target_url.c_str();
    cfg.method        = method;
    cfg.timeout_ms    = 10000;
    cfg.event_handler = proxy_event_handler;
    cfg.user_data     = &resp_content_type;
    cfg.skip_cert_common_name_check = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        send_http_error(req, 500);
        return ESP_OK;
    }

    // ESP-IDF httpd has no header-enumeration API so we probe a known set.
    // For each header we prefer the X-prefixed version (stripping "X-" before
    // forwarding) so that CORS-constrained clients can pass X-Authorization etc.
    static const char * const kFwdHeaders[] = {
        "Accept", "Accept-Encoding", "Accept-Language",
        "Authorization", "Cache-Control", "Client-Id", "Content-Type",
        "If-Modified-Since", "If-None-Match",
        "Origin", "Referer", "User-Agent",
        nullptr
    };
    for (int i = 0; kFwdHeaders[i]; i++) {
        // Try X-prefixed first; fall back to bare name.
        std::string xname = std::string("X-") + kFwdHeaders[i];
        const char *candidates[2] = { xname.c_str(), kFwdHeaders[i] };
        for (const char *hdr : candidates) {
            size_t hlen = httpd_req_get_hdr_value_len(req, hdr);
            if (hlen == 0) continue;
            char *val = (char *)malloc(hlen + 1);
            if (!val) continue;
            if (httpd_req_get_hdr_value_str(req, hdr, val, hlen + 1) == ESP_OK)
                esp_http_client_set_header(client, kFwdHeaders[i], val); // bare name, X- stripped
            free(val);
            break; // found; don't also try the other form
        }
    }

    // Open upstream connection (write_len > 0 tells the client to expect a body).
    int write_len = (req->method == HTTP_POST) ? (int)req->content_len : 0;
    if (esp_http_client_open(client, write_len) != ESP_OK) {
        esp_http_client_cleanup(client);
        send_http_error(req, 502);
        return ESP_OK;
    }

    // Stream POST body upstream.
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

    // Read upstream response headers (triggers proxy_event_handler callbacks).
    esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    if (status_code <= 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        send_http_error(req, 502);
        return ESP_OK;
    }

    // Relay status, content-type, and CORS header to the client.
    char status_str[16];
    snprintf(status_str, sizeof(status_str), "%d", status_code);
    httpd_resp_set_status(req, status_str);
    if (!resp_content_type.empty())
        httpd_resp_set_type(req, resp_content_type.c_str());
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // Stream upstream response body back to the client.
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

void cHttpdServer::proxy_register(httpd_handle_t server)
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
