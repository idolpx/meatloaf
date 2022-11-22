#pragma once

#include <string>
#include <libsoup/soup.h>

#include "response.h"

namespace WebDav {

class ResponseSoup : public Response {
public:
        ResponseSoup(SoupMessage *msg) : msg(msg), contentType("text/plain") {
                setDavHeaders();
        }
        ~ResponseSoup() {}

        void setStatus(int code, std::string message) override {
                soup_message_set_status_full(msg, code, message.c_str());
        }

        void setContentType(const char *ct) {
                contentType = ct;
        }

        void writeHeader(const char *header, const char *value) override {
                soup_message_headers_append(msg->response_headers, header, value);
        }

        bool sendChunk(const char *buf, ssize_t len = -1) override {
                if (len == -1)
                        len = strlen(buf);

                soup_message_body_append(msg->response_body, SOUP_MEMORY_COPY, buf, len);
                return true;
        }

        void closeChunk() override {
                soup_message_body_complete(msg->response_body);
        }

        void closeBody() override {
                soup_message_set_response(msg, contentType.c_str(), SOUP_MEMORY_COPY, NULL, 0);
        }

private:
        SoupMessage *msg;
        std::string contentType;
};

} // namespace