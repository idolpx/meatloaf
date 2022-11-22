#pragma once

#include <string>
#include <vector>
#include <map>

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
        virtual void setStatus(int code, std::string message) = 0;
        virtual void setContentType(const char *ct) = 0;
        virtual bool sendChunk(const char *buf, ssize_t len = -1) = 0;
        virtual void closeChunk() = 0;
        virtual void closeBody() {}

protected:
        virtual void writeHeader(const char *header, const char *value) = 0;

        std::map<std::string, std::string> headers;
};

} // namespace