// Pulls in the exact translation units the P64 read tests need, by
// #include-ing the real .cpp files by relative path. See
// test/native/test_disk_write/engine_sources.cpp for the full explanation of
// why PlatformIO's library dependency finder can't be used here.
#include "../../../lib/utils/punycode.cpp"
// punycode.cpp #define's a bare `min(a,b)` macro with no matching #undef, and
// this file concatenates several .cpp files into ONE translation unit, so it
// would otherwise leak forward into later std::min(...) calls (d64.cpp uses
// std::min several times).
#undef min
#include "../../../lib/utils/U8Char.cpp"
#include "../../../lib/utils/string_utils.cpp"
#include "../../../lib/meatloaf/meat_media.cpp"
#include "../../../lib/meatloaf/media/disk/d64.cpp"
#include "../../../lib/meatloaf/media/disk/p64.cpp"

// Link-only stubs for symbols meatloaf.h references but these tests never
// call. Shared verbatim with the disk-write suite rather than copied. Note
// MFSOwner::File() aborts, so P64MFile is out of reach here - these tests
// drive P64MStream, which is where the format knowledge lives.
#include "../test_disk_write/native_stubs.cpp"
