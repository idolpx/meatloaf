#ifndef MIN_CONFIG
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "httpd_ws.h"
#include "httpd_server.h"

#include "../../include/debug.h"

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

struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
};

struct broadcast_arg {
    httpd_handle_t hd;
    uint8_t *data;
    size_t len;
};

static void websocket_broadcast_send(void *arg)
{
    broadcast_arg *b = (broadcast_arg *)arg;
    size_t max_clients = 8; // matches config.max_open_sockets in start_server()
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

esp_err_t cHttpdServer::websocket_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        Debug_printv("Handshake done, the new connection was opened");
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        Debug_printv("httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    if (ws_pkt.len) {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
        buf = (uint8_t *)calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            Debug_printv("Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            Debug_printv("httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        WsCommandExecutor executor;
        executor.dispatch(buf, ws_pkt.len);

        // Broadcast the message to all other connected WebSocket clients
        std::string msg = std::string((char *)ws_pkt.payload);
        websocket_send_all(msg.c_str(), msg.length());
    }

    free(buf);
    return ESP_OK;
}

void cHttpdServer::websocket_send_all(const char *data, size_t len)
{
    if (!s_server || !data || len == 0) return;

    broadcast_arg *b = (broadcast_arg *)malloc(sizeof(broadcast_arg));
    if (!b) return;

    b->hd   = s_server;
    b->data = (uint8_t *)psram_malloc(len);
    if (!b->data) { free(b); return; }

    memcpy(b->data, data, len);
    b->len = len;

    if (httpd_queue_work(s_server, websocket_broadcast_send, b) != ESP_OK) {
        free(b->data);
        free(b);
    }
}

void cHttpdServer::websocket_register(httpd_handle_t server)
{
    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };

    httpd_register_uri_handler(server, &ws);
}

#endif // MIN_CONFIG
