#include <map>
#include <string>
#include <vector>

#include "response.h"

using namespace WebDav;

void Response::setDavHeaders() {
    setHeader("DAV", "1");
    setHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
}

void Response::setHeader(std::string header, std::string value) {
    headers[header] = value;
}

void Response::setHeader(std::string header, size_t value) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%zu", value);
    headers[header] = tmp;
}

void Response::flushHeaders() {
    for (const auto &h: headers)
        writeHeader(h.first.c_str(), h.second.c_str());
}
