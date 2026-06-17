#include "ws_command.h"

#include <cstdlib>

#include "../../include/debug.h"

#include "protobuf-c/protobuf-c.h"

#ifdef ENABLE_CONSOLE
#include "../console/Console.h"
extern ESP32Console::Console console;
#endif

// RFC 1055 SLIP framing bytes
static constexpr uint8_t SLIP_END     = 0xC0;
static constexpr uint8_t SLIP_ESC     = 0xDB;
static constexpr uint8_t SLIP_ESC_END = 0xDC;
static constexpr uint8_t SLIP_ESC_ESC = 0xDD;

bool WsCommandExecutor::isSlip(const uint8_t* data, size_t len)
{
    return len > 0 && data[0] == SLIP_END;
}

bool WsCommandExecutor::isJson(const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        return c == '{' || c == '[';
    }
    return false;
}

// Protobuf wire format is binary. If the frame contains non-printable,
// non-whitespace bytes in its leading content it is treated as protobuf.
// This works because console commands and JSON are always printable ASCII.
bool WsCommandExecutor::isProtobuf(const uint8_t* data, size_t len)
{
    if (len < 2) return false;
    size_t check = len < 4 ? len : 4;
    for (size_t i = 0; i < check; i++) {
        uint8_t c = data[i];
        // Printable ASCII (0x20-0x7E) plus tab/LF/CR are text; everything else is binary.
        bool is_text = (c >= 0x20 && c <= 0x7E) || c == 0x09 || c == 0x0A || c == 0x0D;
        if (!is_text) return true;
    }
    return false;
}

size_t WsCommandExecutor::slipDecode(const uint8_t* in, size_t inLen,
                                     uint8_t* out, size_t outMax)
{
    size_t i = 0, outLen = 0;

    // Packets may be wrapped in leading and/or trailing END bytes
    if (i < inLen && in[i] == SLIP_END) i++;

    while (i < inLen && outLen < outMax) {
        uint8_t b = in[i++];
        if (b == SLIP_END) break;

        if (b == SLIP_ESC) {
            if (i >= inLen) break;
            uint8_t esc = in[i++];
            if      (esc == SLIP_ESC_END) b = SLIP_END;
            else if (esc == SLIP_ESC_ESC) b = SLIP_ESC;
            else                          b = esc; // malformed; pass through
        }

        out[outLen++] = b;
    }
    return outLen;
}

void WsCommandExecutor::handleJson(const uint8_t* data, size_t len)
{
    Debug_printv("ws: JSON (%u bytes)", (unsigned)len);
    // TODO: parse JSON and route to structured command handlers
#ifdef ENABLE_CONSOLE
    console.execute(reinterpret_cast<const char*>(data));
#endif
}

void WsCommandExecutor::handleProtobuf(const uint8_t* data, size_t len)
{
    Debug_printv("ws: protobuf (%u bytes)", (unsigned)len);

    // TODO: unpack message using a generated descriptor, e.g.:
    //   MyMessage* msg = my_message__unpack(nullptr, len, data);
    //   if (msg) {
    //       // access msg->field_name ...
    //       my_message__free_unpacked(msg, nullptr);
    //   }
    //
    // Until a .proto schema is defined, log the raw bytes at debug level.
    for (size_t i = 0; i < len && i < 16; i++) {
        Debug_printv("  [%02zu] 0x%02X", i, data[i]);
    }
}

void WsCommandExecutor::handleSlip(const uint8_t* data, size_t len)
{
    Debug_printv("ws: SLIP (%u bytes encoded)", (unsigned)len);

    // Decoded payload is always <= encoded length; +1 for null terminator
    uint8_t* buf = static_cast<uint8_t*>(malloc(len + 1));
    if (!buf) {
        Debug_printv("ws: SLIP decode OOM");
        return;
    }
    size_t n = slipDecode(data, len, buf, len);
    buf[n] = '\0';
    Debug_printv("ws: SLIP decoded %u bytes", (unsigned)n);

#ifdef ENABLE_CONSOLE
    console.execute(reinterpret_cast<const char*>(buf));
#endif
    free(buf);
}

void WsCommandExecutor::handleText(const uint8_t* data, size_t len)
{
    Debug_printv("ws: text (%u bytes)", (unsigned)len);
#ifdef ENABLE_CONSOLE
    console.execute(reinterpret_cast<const char*>(data));
#endif
}

void WsCommandExecutor::dispatch(const uint8_t* data, size_t len)
{
    if (!data || len == 0) return;

    if (isSlip(data, len))
        handleSlip(data, len);
    else if (isJson(data, len))
        handleJson(data, len);
    else if (isProtobuf(data, len))
        handleProtobuf(data, len);
    else
        handleText(data, len);
}
