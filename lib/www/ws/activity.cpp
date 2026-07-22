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

#include "activity.h"

#ifndef MIN_CONFIG
#include "ws.h"
#include <cstdio>

// Minimal JSON string escaping — avoids pulling nlohmann::json (and its
// heap allocations) into what's meant to be a lightweight, frequently-called,
// fire-and-forget notification path.
static void json_append_escaped(std::string &out, const std::string &s)
{
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
}

void notify_activity(const std::string &source, const std::string &event, const std::string &message)
{
    std::string json = "{\"type\":\"activity\",\"source\":\"";
    json_append_escaped(json, source);
    json += "\",\"event\":\"";
    json_append_escaped(json, event);
    json += "\"";
    if (!message.empty()) {
        json += ",\"message\":\"";
        json_append_escaped(json, message);
        json += "\"";
    }
    json += "}";

    ws_send_all(json.c_str(), json.length());
}

#else // MIN_CONFIG

void notify_activity(const std::string &, const std::string &, const std::string &) {}

#endif // MIN_CONFIG
