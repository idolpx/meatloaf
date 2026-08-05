// Pulls in the exact translation units the disk-write engine test needs, by
// #include-ing the real .cpp files by relative path, rather than letting
// PlatformIO discover them.
//
// Why: PlatformIO's Library Dependency Finder treats a `lib/<name>` folder as
// one monolithic "legacy-style" library and compiles EVERY .c/.cpp under it
// recursively once any file in that folder is referenced - not just the file
// that was #include'd. For `lib/meatloaf` and `lib/utils` that drags in files
// with real ESP-IDF/WiFi/mbedtls/libarchive dependencies (network/, service/,
// codec/, etc.) that have nothing to do with the disk-image write engine and
// cannot compile for a native host. Turning LDF on for the native environment
// (`lib_ldf_mode` != off) was tried and confirmed to pull in unrelated `lib/*`
// siblings too (e.g. lib/TNFSlib needing freertos/FreeRTOS.h).
//
// Files under test/native/<name>/ are always compiled by `pio test`
// regardless of `lib_ldf_mode`, so #include-ing the needed .cpp files here
// (a "unity build") selects exactly those translation units - and only
// those - with no PlatformIO library-discovery machinery involved at all.
// punycode.cpp/U8Char.cpp are included because string_utils.cpp's toUTF/
// toPETSCII helpers (used by d64.cpp/meat_media.cpp) link against them.
//
// Header resolution for the bare quote-includes inside these files (e.g.
// "string_utils.h", "utils.h", "media/disk/d64.h") still needs -I paths;
// see the `-I lib/meatloaf -I lib/utils` additions in [env:native]'s
// build_flags in platformio.ini.
#include "../../../lib/utils/punycode.cpp"
// punycode.cpp #define's a bare `min(a,b)` macro with no matching #undef.
// That's harmless when it's compiled as its own translation unit (the normal
// multi-file build), but this file deliberately concatenates several .cpp
// files into ONE translation unit, so the macro would otherwise leak forward
// and break every later std::min(...) call (it did - d64.cpp uses
// std::min() several times). Undo it here rather than touching punycode.cpp.
#undef min
#include "../../../lib/utils/U8Char.cpp"
#include "../../../lib/utils/string_utils.cpp"
#include "../../../lib/meatloaf/meat_media.cpp"
#include "../../../lib/meatloaf/media/disk/d64.cpp"
