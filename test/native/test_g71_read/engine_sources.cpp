// Pulls in the exact translation units the G71 read tests need. See
// test/native/test_disk_write/engine_sources.cpp for why PlatformIO's library
// dependency finder can't be used here.
#include "../../../lib/utils/punycode.cpp"
// punycode.cpp #define's a bare `min(a,b)` with no matching #undef, and this
// file concatenates several .cpp files into ONE translation unit.
#undef min
#include "../../../lib/utils/U8Char.cpp"
#include "../../../lib/utils/string_utils.cpp"
#include "../../../lib/meatloaf/meat_media.cpp"
#include "../../../lib/meatloaf/media/disk/d64.cpp"
#include "../../../lib/meatloaf/media/disk/g64.cpp"

#include "../test_disk_write/native_stubs.cpp"
