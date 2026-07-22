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

#pragma once

#include <string>

// Broadcasts a JSON activity notification to every connected WebSocket
// client, so the web app can reflect live device/system status. Safe to
// call from ANY task, including the real-time IEC bus task: it never
// touches a socket or blocks on the calling task — the message is copied
// and handed to the httpd task's own async work queue (see ws_send_all()
// in ws.cpp). A no-op if no web server/clients are active, or in
// MIN_CONFIG builds (no WebSocket support), so call sites never need to
// guard the call themselves.
//
// Wire format sent to clients:
//   {"type":"activity","source":"<source>","event":"<event>"[,"message":"<message>"]}
//
// `source` identifies what's reporting (e.g. "drive8", "wifi", "webdav").
// `event` is a short machine-readable tag (e.g. "mount", "read", "error").
// `message` is optional human-readable detail.
void notify_activity(const std::string &source, const std::string &event, const std::string &message = "");
