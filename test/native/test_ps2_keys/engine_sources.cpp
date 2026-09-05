// Pulls in the exact translation units the PS/2 key tests need, by
// #include-ing the real .cpp files by relative path. Same approach as
// test/native/test_console_dos/engine_sources.cpp -- see that file for why
// PlatformIO's library dependency finder can't be used here.
#include "../../../lib/utils/punycode.cpp"
// punycode.cpp #define's a bare `min(a,b)` macro with no matching #undef, and
// this file concatenates several .cpp files into ONE translation unit, so it
// would otherwise leak forward into later std::min(...) calls.
#undef min
#include "../../../lib/utils/U8Char.cpp"
#include "../../../lib/utils/string_utils.cpp"

// The units under test.
#include "../../../lib/console/dos_encode.cpp"
#include "../../../lib/device/ps2/ps2_keynames.cpp"

// util_debug_printf() is the non-ESP backend for the Debug_print*() macros
// (include/debug.h's !ESP_PLATFORM branch); string_utils.cpp calls it. The real
// one lives in lib/utils/utils.cpp, which transitively pulls in the SAM speech
// synthesizer. This suite needs none of that.
#include <cstdarg>
#include <cstdio>
void util_debug_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}
