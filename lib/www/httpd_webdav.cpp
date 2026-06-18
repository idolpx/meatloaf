#ifndef MIN_CONFIG
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "httpd_webdav.h"
#include "httpd_server.h"

#include "../../include/debug.h"

#include <cstring>

esp_err_t cHttpdServer::webdav_handler(httpd_req_t *httpd_req)
{
    WebDav::Server *server = (WebDav::Server *)httpd_req->user_ctx;
    WebDav::Request req(httpd_req);
    WebDav::Response resp(httpd_req);
    int ret;

    Debug_printv("uri[%s]", httpd_req->uri);

    if ( !req.parseRequest() )
    {
        resp.setStatus(400); // Bad Request
        resp.flushHeaders();
        resp.closeBody();
        return ESP_OK;
    }

    // httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Origin", "*");
    // httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Headers", "*");
    // httpd_resp_set_hdr(httpd_req, "Access-Control-Allow-Methods", "*");

    Debug_printv("%d %s[%s]", httpd_req->method, http_method_str((enum http_method)httpd_req->method), httpd_req->uri);
    //trace_webdav_request(httpd_req, req);
    //Debug_memory();

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
            return ESP_OK;
        break;
    case HTTP_HEAD:
        ret = server->doHead(req, resp);
        break;
    case HTTP_LOCK:
        ret = server->doLock(req, resp);
        if ( ret == 200 )
            return ESP_OK;
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
            return ESP_OK;
        break;
    case HTTP_PROPPATCH:
        ret = server->doProppatch(req, resp);
        if (ret == 207)
            return ESP_OK;
        break;
    case HTTP_PUT:
        ret = server->doPut(req, resp);
        break;
    case HTTP_UNLOCK:
        ret = server->doUnlock(req, resp);
        break;
    default:
        return ESP_ERR_HTTPD_INVALID_REQ;
        break;
    }

    resp.setStatus(ret);

    if ( (ret > 399) & (httpd_req->method != HTTP_HEAD) )
    {
        // Send error page
        send_http_error(httpd_req, ret);
    }
    else
    {
        // Send empty response
        resp.flushHeaders();
        resp.closeBody();
    }

    return ESP_OK;
}

void cHttpdServer::webdav_register(httpd_handle_t server, const char *root_uri, const char *root_path)
{
    WebDav::Server *webDavServer = new WebDav::Server(root_uri, root_path);

    // Use a static string so the pointer remains valid for the lifetime of the server.
    // asprintf() here was a leak: ESP-IDF stores the URI pointer without copying it.
    static std::string dav_uri_pattern;
    // "/*" matches bare "/" and all sub-paths; "/?*" requires at least one char
    // after the slash and excludes the root, breaking Windows Explorer WebDAV.
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
