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

int sam(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    fprintf(stderr, "host_stubs: SAM speech called unexpectedly\n");
    abort();
}
