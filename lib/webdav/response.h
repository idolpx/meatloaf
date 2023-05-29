#pragma once

#include <string>
#include <vector>
#include <map>

#include <esp_http_server.h>

namespace WebDav {

class MultiStatusResponse {
public:
        std::string href;
        std::string status;
        std::map<std::string, std::string> props;
        bool isCollection;
};

class Response {
public:
        Response() {}
        ~Response() {}

        void setDavHeaders();
        void setHeader(std::string header, std::string value);
        void setHeader(std::string header, size_t value);
        void flushHeaders();

        // Functions that depend on the underlying web server implementation
        void setStatus(int code, std::string message) {
                free(status);
                status = NULL;

                asprintf(&status, "%d %s", code, message.c_str());
                httpd_resp_set_status(req, status);
        }

        void writeHeader(const char *header, const char *value) {
                httpd_resp_set_hdr(req, header, value);
        }

        void setContentType(const char *ct) {
                httpd_resp_set_type(req, ct);
        }

        bool sendChunk(const char *buf, ssize_t len = -1) {
                chunked = true;

                if (len == -1)
                        len = strlen(buf);

                return httpd_resp_send_chunk(req, buf, len) == ESP_OK;
        }

        void closeChunk() {
                httpd_resp_send_chunk(req, NULL, 0);
        }

        void closeBody() {
                if (!chunked)
                        httpd_resp_send(req, "", 0);
        }

private:
        httpd_req_t *req;
        char *status;
        bool chunked;

        std::map<std::string, std::string> headers;
};

} // namespace