#pragma once

#include <string>
#include <libsoup/soup.h>

#include "request.h"

namespace WebDav {

class RequestSoup : public Request {
public:
        RequestSoup(SoupMessage *msg, std::string path) : Request(path), msg(msg), offset(0) {}

        std::string getHeader(std::string name) {
                const char *s = soup_message_headers_get_one(msg->request_headers, name.c_str());
                if (!s)
                        return "";

                return std::string(s);
        }

        size_t getContentLength() override {
                return soup_message_headers_get_content_length(msg->request_headers);
        }

        int readBody(char *buf, int len) override {
                int l = MIN(len, msg->request_body->length - offset);
                memcpy(buf, msg->request_body->data, l);
                offset += l;
                return l;
        }

private:
        SoupMessage *msg;
        goffset offset;
};

} // namespace