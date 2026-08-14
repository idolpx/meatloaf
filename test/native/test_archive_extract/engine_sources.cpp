// Pulls in the exact C++ translation units the archive tests need, by
// #include-ing the real .cpp files by relative path. See
// test/native/test_disk_write/engine_sources.cpp for the full explanation of
// why PlatformIO's library dependency finder can't be used here.
//
// The C side (libarchive + zlib + bzip2 + lz4) is built separately by
// host/build_libarchive.py, which this environment runs as an extra_script -
// those are C sources with file-static helpers that share names across files,
// so they cannot be concatenated into one translation unit the way these can.
#include "../../../lib/utils/punycode.cpp"
// punycode.cpp #define's a bare `min(a,b)` macro with no matching #undef, and
// this file concatenates several .cpp files into ONE translation unit, so it
// would otherwise leak forward into later std::min(...) calls.
#undef min
#include "../../../lib/utils/U8Char.cpp"
#include "../../../lib/utils/string_utils.cpp"
#include "../../../lib/utils/peoples_url_parser.cpp"
#include "../../../lib/meatloaf/meat_media.cpp"
// meat_session.cpp is NOT included here: it and archive.cpp each define a
// file-static psram_malloc(), which is a redefinition once concatenated.
// It gets its own translation unit in session_source.cpp.
#include "../../../lib/meatloaf/media/archive/archive.cpp"

// peoples_url_parser.cpp calls util_get_canonical_path()/util_tokenize()/
// util_tolower(), whose real implementations live in lib/utils/utils.cpp.
// That file also carries the SAM speech synthesizer wrapper, so pulling it in
// means the SAM entry point has to resolve too - stubbed in host_stubs.cpp
// rather than compiling an audio synthesizer no archive test can reach.
// NATIVE_STUBS_REAL_UTILS tells the shared disk-write stub file to leave
// util_debug_printf() to the real utils.cpp included here.
#define NATIVE_STUBS_REAL_UTILS 1
// This suite supplies its own MFSOwner::File(); see host_stubs.cpp.
#define NATIVE_STUBS_REAL_MFSOWNER 1
#include "../../../lib/utils/utils.cpp"

// Link-only stubs for symbols meatloaf.h references but these tests never
// call. Shared verbatim with the disk-write suite rather than copied.
// host_stubs.cpp is NOT included here - it lives in this test folder, so
// PlatformIO compiles it as its own translation unit already.
#include "../test_disk_write/native_stubs.cpp"
