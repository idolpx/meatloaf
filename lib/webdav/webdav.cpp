#include "webdav.h"

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include <esp_http_server.h>
#include <esp_log.h>

#include <server.h>
#include <request-espidf.h>
#include <response-espidf.h>

static const char *TAG = "webdav";

esp_err_t webdav_handler(httpd_req_t *httpd_req) {
        WebDav::Server *server = (WebDav::Server *) httpd_req->user_ctx;
        WebDav::RequestEspIdf req(httpd_req, httpd_req->uri);
        WebDav::ResponseEspIdf resp(httpd_req);
        int ret;

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
        asprintf(&uri, "%s/*", root_uri);

        httpd_uri_t uri_dav = {
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

    if (httpd_start(&server, &config) == ESP_OK)
        webdav_register(server, "/sd", "/dav");
}