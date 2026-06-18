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

#include "ws.h"
#include "../web_server.h"

#include "../../../include/debug.h"

#include "ws_command.h"

#include <esp_heap_caps.h>
#include <cstdlib>
#include <cstring>
#include <string>

static inline void *psram_malloc(size_t sz)
{
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(sz);
}

struct broadcast_arg {
    httpd_handle_t hd;
    uint8_t *data;
    size_t len;
};

static void ws_broadcast_send(void *arg)
{
    broadcast_arg *b = (broadcast_arg *)arg;
    size_t max_clients = 8;
    int client_fds[8];

    if (httpd_get_client_list(b->hd, &max_clients, client_fds) == ESP_OK) {
        for (size_t i = 0; i < max_clients; i++) {
            if (httpd_ws_get_fd_info(b->hd, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                httpd_ws_frame_t ws_pkt = {};
                ws_pkt.payload = b->data;
                ws_pkt.len = b->len;
                ws_pkt.type = HTTPD_WS_TYPE_TEXT;
                httpd_ws_send_frame_async(b->hd, client_fds[i], &ws_pkt);
            }
        }
    }

    free(b->data);
    free(b);
}

esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        Debug_printv("Handshake done, the new connection was opened");
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        Debug_printv("httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    if (ws_pkt.len) {
        buf = (uint8_t *)calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            Debug_printv("Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            Debug_printv("httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        WsCommandExecutor executor;
        executor.dispatch(buf, ws_pkt.len);

        std::string msg = std::string((char *)ws_pkt.payload);
        ws_send_all(msg.c_str(), msg.length());
    }

    free(buf);
    return ESP_OK;
}

void ws_send_all(const char *data, size_t len)
{
    if (!HttpServer::s_server || !data || len == 0) return;

    broadcast_arg *b = (broadcast_arg *)malloc(sizeof(broadcast_arg));
    if (!b) return;

    b->hd   = HttpServer::s_server;
    b->data = (uint8_t *)psram_malloc(len);
    if (!b->data) { free(b); return; }

    memcpy(b->data, data, len);
    b->len = len;

    if (httpd_queue_work(HttpServer::s_server, ws_broadcast_send, b) != ESP_OK) {
        free(b->data);
        free(b);
    }
}

void ws_register(httpd_handle_t server)
{
    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };

    httpd_register_uri_handler(server, &ws);
}

#endif // MIN_CONFIG
