// Pulls in the exact translation units the ARC read tests need. See
// test/native/test_disk_write/engine_sources.cpp for why PlatformIO's library
// dependency finder can't be used here.
#include "../../../lib/utils/punycode.cpp"
#undef min
#include "../../../lib/utils/U8Char.cpp"
#include "../../../lib/utils/string_utils.cpp"
#include "../../../lib/meatloaf/meat_media.cpp"
#include "../../../lib/meatloaf/media/archive/arc.cpp"

#include "../test_disk_write/native_stubs.cpp"
