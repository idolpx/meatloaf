// Translation units the MStream seek-contract tests need. See
// test/native/test_disk_write/engine_sources.cpp for why the real .cpp files
// are #include'd rather than discovered by PlatformIO's LDF.
#include "../../../lib/utils/punycode.cpp"
// punycode.cpp #define's a bare `min(a,b)` macro with no matching #undef.
#undef min
#include "../../../lib/utils/U8Char.cpp"
#include "../../../lib/utils/string_utils.cpp"

// Link-only stubs for symbols meatloaf.h references but these tests never
// call. Shared verbatim with the disk-write suite rather than copied.
#include "../test_disk_write/native_stubs.cpp"
