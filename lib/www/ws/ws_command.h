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

#include <cstddef>
#include <cstdint>

// Dispatches a raw WebSocket frame payload to the appropriate handler:
//   - RFC 1055 SLIP frame (first byte == 0xC0): decoded and executed
//   - JSON object or array (first non-whitespace byte is '{' or '['): JSON handler
//   - Binary content (non-printable bytes in the first 4 bytes): protobuf handler
//   - Anything else: console.execute()
class WsCommandExecutor {
public:
    void dispatch(const uint8_t* data, size_t len);

private:
    static bool isSlip(const uint8_t* data, size_t len);
    static bool isJson(const uint8_t* data, size_t len);
    static bool isProtobuf(const uint8_t* data, size_t len);

    static void handleSlip(const uint8_t* data, size_t len);
    static void handleJson(const uint8_t* data, size_t len);
    static void handleProtobuf(const uint8_t* data, size_t len);
    static void handleText(const uint8_t* data, size_t len);

    // RFC 1055 decode; returns number of bytes written to out.
    static size_t slipDecode(const uint8_t* in, size_t inLen,
                             uint8_t* out, size_t outMax);
};
