// Host-only link stubs for this suite, on top of the shared
// test_disk_write/native_stubs.cpp.
//
// engine_sources.cpp compiles the real lib/utils/utils.cpp (peoples_url_parser
// needs util_get_canonical_path()/util_tokenize()/util_tolower() and those
// have real semantics worth exercising, not stubbing). utils.cpp also holds
// util_sam_say(), which calls into the SAM speech synthesizer. No archive test
// can reach it, so the entry point is stubbed rather than compiling an audio
// synthesizer into the archive suite. If a test ever gets here it is a bug in
// the test, so it aborts loudly.
#include <cstdio>
#include <cstdlib>

#include "meatloaf.h"
#include "media/archive/archive.h"

// MFile's one-argument constructor, needed to build an ArchiveMFile here. Its
// real definition is in meatloaf.cpp, which cannot compile natively (it
// registers every network/media filesystem and includes esp_timer.h etc).
//
// This is not a stub - it is the real body, verbatim from meatloaf.cpp:905,
// and resetURL() itself is the real one (peoples_url_parser.cpp is compiled
// into this suite). Keep it in sync if that constructor ever does more than
// parse the path.
MFile::MFile(std::string path)
{
    resetURL(path);
}

// MFSOwner::File() for this suite. The real one is in meatloaf.cpp, which
// cannot compile natively.
//
// It reproduces the case that crashed the device: MFSOwner::File() assigns
// sourceFile ONLY on its "look up path" branch, so a path it resolves without
// needing a container lookup comes back with sourceFile == nullptr. That is
// legitimate, and ImageBroker::obtain() must cope with it rather than
// dereference it. Real URL that produced it, from web.archive.org, where a
// second scheme appears mid-path:
//   https://web.archive.org/web/20180901151341/http://vic20tapes.org/...zip
MFile* MFSOwner::File(std::string path, bool default_to_flash)
{
    (void)default_to_flash;
    return new ArchiveMFile(path);   // sourceFile left null, as the real one can
}

int sam(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    fprintf(stderr, "host_stubs: SAM speech called unexpectedly\n");
    abort();
}
