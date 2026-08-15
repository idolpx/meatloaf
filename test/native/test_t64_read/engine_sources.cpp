// Pulls in the exact translation units the T64 read tests need, by
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
#include "../../../lib/meatloaf/meat_media.cpp"
#include "../../../lib/meatloaf/media/tape/t64.cpp"

// Link-only stubs for symbols meatloaf.h/t64.cpp reference but these tests
// never call. Shared verbatim with the disk-write suite rather than copied.
// Note MFSOwner::File() aborts, so T64MFile::rewindDirectory() and
// getNextFileInDir() are out of reach here - these tests drive T64MStream.
#include "../test_disk_write/native_stubs.cpp"
