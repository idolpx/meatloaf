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

#include "handler.h"
#include "../web_server.h"

#include "../../../include/debug.h"

#include "webdav_server.h"
#include "request.h"
#include "response.h"

#include <cstring>

// A non-ESP_OK return makes esp_http_server close the socket, which is required
// whenever the request body was not read to its end: with a chunked body there
// is no Content-Length for the server to skip by, so the leftover bytes would
// be parsed as the start of the next request. See Request::connectionReusable.
static esp_err_t finish(WebDav::Request &req)
{
    return req.connectionReusable() ? ESP_OK : ESP_FAIL;
}

esp_err_t webdav_handler(httpd_req_t *httpd_req)
{
    WebDav::Server *server = (WebDav::Server *)httpd_req->user_ctx;
    WebDav::Request req(httpd_req);
    WebDav::Response resp(httpd_req);
    int ret;

    Debug_printv("uri[%s]", httpd_req->uri);

    if ( !req.parseRequest() )
    {
        resp.setStatus(400);
        resp.flushHeaders();
        resp.closeBody();
        return ESP_OK;
    }

    Debug_printv("%d %s[%s]", httpd_req->method, http_method_str((enum http_method)httpd_req->method), httpd_req->uri);

    switch (httpd_req->method)
    {
    case HTTP_COPY:
        ret = server->doCopy(req, resp);
        break;
    case HTTP_DELETE:
        ret = server->doDelete(req, resp);
        break;
    case HTTP_GET:
        ret = server->doGet(req, resp);
        if ( ret == 200 )
            return finish(req);
        break;
    case HTTP_HEAD:
        ret = server->doHead(req, resp);
        break;
    case HTTP_LOCK:
        ret = server->doLock(req, resp);
        if ( ret == 200 )
            return finish(req);
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
        if (ret == 207)
            return finish(req);
        break;
    case HTTP_PROPPATCH:
        ret = server->doProppatch(req, resp);
        if (ret == 207)
            return finish(req);
        break;
    case HTTP_PUT:
        ret = server->doPut(req, resp);
        break;
    case HTTP_UNLOCK:
        ret = server->doUnlock(req, resp);
        break;
    default:
        return ESP_ERR_HTTPD_INVALID_REQ;
    }

    // A successful mutating request may have created, changed, moved or
    // deleted a .config file; drop the cached lookups so base_url redirects
    // take effect immediately instead of after the cache TTL.
    if ( ret < 300 )
    {
        switch (httpd_req->method)
        {
        case HTTP_PUT:
        case HTTP_DELETE:
        case HTTP_MOVE:
        case HTTP_COPY:
            MFSOwner::invalidateConfigCache();
            break;
        default:
            break;
        }
    }

    resp.setStatus(ret);

    if ( (ret > 399) && (httpd_req->method == HTTP_GET) )
    {
        // Browser-facing GET errors get the HTML error page.
        HttpServer::send_http_error(httpd_req, ret);
    }
    else
    {
        // WebDAV clients only need the status line. Building the HTML error
        // page costs several flash opens per miss, and bulk transfers hit
        // this path with a PROPFIND 404 for every file before its PUT.
        resp.flushHeaders();
        resp.closeBody();
    }

    return finish(req);
}

void webdav_register(httpd_handle_t server, const char *root_uri, const char *root_path)
{
    WebDav::Server *webDavServer = new WebDav::Server(root_uri, root_path);

    static std::string dav_uri_pattern;
    if (strlen(root_uri) > 1)
        dav_uri_pattern = std::string(root_uri) + "/*";
    else
        dav_uri_pattern = "/*";

    httpd_uri_t uri_dav = {
        .uri = dav_uri_pattern.c_str(),
        .method = http_method(0),
        .handler = webdav_handler,
        .user_ctx = webDavServer,
        .is_websocket = false
    };

    http_method methods[] = {
        HTTP_COPY,
        HTTP_DELETE,
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

    for (int i = 0; i < (int)(sizeof(methods) / sizeof(methods[0])); i++)
    {
        uri_dav.method = methods[i];
        httpd_register_uri_handler(server, &uri_dav);
    }
}

#endif // MIN_CONFIG
