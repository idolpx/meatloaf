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
