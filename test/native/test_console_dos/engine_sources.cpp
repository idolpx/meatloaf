// Pulls in the exact translation units the console DOS tests need, by
// #include-ing the real .cpp files by relative path. See
// test/native/test_disk_write/engine_sources.cpp for the full explanation of
// why PlatformIO's library dependency finder can't be used here.
#include "../../../lib/utils/punycode.cpp"
// punycode.cpp #define's a bare `min(a,b)` macro with no matching #undef, and
// this file concatenates several .cpp files into ONE translation unit, so it
// would otherwise leak forward into later std::min(...) calls.
#undef min
#include "../../../lib/utils/U8Char.cpp"
#include "../../../lib/utils/string_utils.cpp"

// The unit under test. It deliberately depends on nothing but <string> and
// mstr::toPETSCII2(), which is why it lives outside IECCommands.cpp.
#include "../../../lib/console/dos_encode.cpp"

// util_debug_printf() is the non-ESP backend for the Debug_print*() macros
// (include/debug.h's !ESP_PLATFORM branch); string_utils.cpp calls it. The real
// one lives in lib/utils/utils.cpp, which transitively pulls in the SAM speech
// synthesizer via samlib.h. This suite needs none of that, and unlike the
// disk-write and archive suites it needs no MFile/MFSOwner stubs either, so it
// defines the one missing symbol here rather than including native_stubs.cpp.
#include <cstdarg>
#include <cstdio>
void util_debug_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}
