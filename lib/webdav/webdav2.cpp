#include "webdav2.h"

#include "../../include/debug.h"

#include "string_utils.h"
#include "meat_io.h"
#include "meat_stream.h"

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include <esp_http_server.h>
#include <esp_log.h>

#include <server.h>
#include <request-espidf.h>
#include <response-espidf.h>

#define MIN(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#define MAX(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

static const char *TAG = "webdav";


/* Our URI handler function to be called during GET /uri request */
esp_err_t get_handler(httpd_req_t *req)
{
    Debug_printv("url[%s]", req->uri);

    std::string uri = mstr::urlDecode(req->uri);
    if ( uri == "/" )
    {
        uri = "/index.html";
    }

    auto file = MFSOwner::File( "/www" + uri );
    auto istream = file->meatStream();
    if ( istream != nullptr )
    {
        Debug_printv("sending [%s]", file->url.c_str());
        
        esp_err_t r;
        const char buf[1024] = { '\x00' };
        do
        {
            uint32_t len = istream->read( (uint8_t *)buf, 1024 );
            r = httpd_resp_send_chunk( req, buf, len );
            Debug_printv("len[%u]", len);
        } while ( istream->available() && r == ESP_OK );
        httpd_resp_send_chunk( req, buf, 0 );
        Debug_printv("complete");

        return ESP_OK;
    }

    /* Send a simple response */
    Debug_printv("not found! [%s]", file->url.c_str());
    httpd_resp_send_404(req);
    return ESP_OK;
}

/* Our URI handler function to be called during POST /uri request */
esp_err_t post_handler(httpd_req_t *req)
{
    Debug_printv("url[%s]", req->uri);

    /* Destination buffer for content of HTTP POST request.
     * httpd_req_recv() accepts char* only, but content could
     * as well be any binary data (needs type casting).
     * In case of string data, null termination will be absent, and
     * content length would give length of string */
    char content[100];

    /* Truncate if content length larger than the buffer */
    size_t recv_size = MIN(req->content_len, sizeof(content));

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {  /* 0 return value indicates connection closed */
        /* Check if timeout occurred */
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            /* In case of timeout one can choose to retry calling
             * httpd_req_recv(), but to keep it simple, here we
             * respond with an HTTP 408 (Request Timeout) error */
            httpd_resp_send_408(req);
        }
        /* In case of error, returning ESP_FAIL will
         * ensure that the underlying socket is closed */
        return ESP_FAIL;
    }

    /* Send a simple response */
    const char resp[] = "URI POST Response";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t webdav_handler(httpd_req_t *httpd_req) {
        WebDav::Server *server = (WebDav::Server *) httpd_req->user_ctx;
        WebDav::RequestEspIdf req(httpd_req, httpd_req->uri);
        WebDav::ResponseEspIdf resp(httpd_req);
        int ret;

        Debug_printv("url[%s]", httpd_req->uri);

        if (!req.parseRequest()) {
                resp.setStatus(400, "Invalid request");
                resp.flushHeaders();
                resp.closeBody();
                return ESP_OK;
        }

        httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Headers", "*");
        httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Methods", "*");

        ESP_LOGI(TAG, "%s: >%s<", http_method_str((enum http_method) httpd_req->method), httpd_req->uri);

        switch (httpd_req->method) {
                case HTTP_COPY:
                        ret = server->doCopy(req, resp);
                        break;
                case HTTP_DELETE:
                        ret = server->doDelete(req, resp);
                        break;
                case HTTP_GET:
                        ret = server->doGet(req, resp);
                        break;
                case HTTP_HEAD:
                        ret = server->doHead(req, resp);
                        break;
                case HTTP_LOCK:
                        ret = server->doLock(req, resp);
                        break;
                case HTTP_MKCOL:
                        ret = server->doMkcol(req, resp);
                        break;
                case HTTP_MOVE:
                        ret = server->doMove(req, resp);
                        break;
                case HTTP_OPTIONS:
                        ret = server->doOptions(req, resp);
                        break;
                case HTTP_PROPFIND:
                        ret = server->doPropfind(req, resp);
                        break;
                case HTTP_PROPPATCH:
                        ret = server->doProppatch(req, resp);
                        break;
                case HTTP_PUT:
                        ret = server->doPut(req, resp);
                        break;
                case HTTP_UNLOCK:
                        ret = server->doUnlock(req, resp);
                        break;
                default:
                        ret = ESP_ERR_HTTPD_INVALID_REQ;
                        break;
        }

        resp.setStatus(ret, "");
        resp.flushHeaders();
        resp.closeBody();

        return ret;
}

void webdav_register(httpd_handle_t server, const char *root_path, const char *root_uri) {
        WebDav::Server *webDavServer = new WebDav::Server(root_path, root_uri);

        char *uri;
        asprintf(&uri, "%s/?*", root_uri);

        httpd_uri_t uri_dav = 
        {
            .uri      = uri,
            .method   = http_method(0),
            .handler  = webdav_handler,
            .user_ctx = webDavServer,
        };

        http_method methods[] = {
                HTTP_COPY,
                HTTP_DELETE,
                HTTP_GET,
                HTTP_HEAD,
                HTTP_LOCK,
                HTTP_MKCOL,
                HTTP_MOVE,
                HTTP_OPTIONS,
                HTTP_PROPFIND,
                HTTP_PROPPATCH,
                HTTP_PUT,
                HTTP_UNLOCK,
        };

        for (int i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
                uri_dav.method = methods[i];
                httpd_register_uri_handler(server, &uri_dav);
        }
}

void http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 32;

    esp_log_level_set("httpd_uri", ESP_LOG_DEBUG);

    /* URI handler structure for GET /uri */
    httpd_uri_t uri_get = {
        .uri      = "/*",
        .method   = HTTP_GET,
        .handler  = get_handler,
        .user_ctx = NULL
    };

    /* URI handler structure for POST /uri */
    httpd_uri_t uri_post = {
        .uri      = "/*",
        .method   = HTTP_POST,
        .handler  = post_handler,
        .user_ctx = NULL
    };

    if (httpd_start(&server, &config) == ESP_OK)
    {
        /* Register URI handlers */
        webdav_register(server, "/sd", "/dav");

        // Default handlers
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_post);

        Serial.println( ANSI_GREEN_BOLD "WWW/WebDAV Server Started!" ANSI_RESET );
    }
    else
    {
        Serial.println( ANSI_RED_BOLD "WWW/WebDAV Server FAILED to start!" ANSI_RESET );
    }
}

/* Function for stopping the webserver */
void http_server_stop(httpd_handle_t server)
{
    if (server) {
        /* Stop the httpd server */
        httpd_stop(server);
    }
}