// Link-only stubs for symbols d64.cpp/meatloaf.h reference but native tests
// never call. If a test ever reaches one of these, that is a bug in the test,
// so they abort loudly rather than returning something plausible.
//
// The MStream::getTrackCount/getSectorCount/seekBlock/seekSector(x2) and
// MFile::exists() stubs exist because those are non-pure virtuals whose real
// implementations live in meatloaf.cpp - which registers every network/media
// filesystem (FTP, SMB, NFS, mDNS, archive formats, ...) and directly
// includes esp_timer.h/esp_spiffs.h/esp_littlefs.h, so it can't be compiled
// natively. D64MStream (via d64.cpp) provides its own real overrides for the
// track/sector methods it actually uses; FileContainerStream and the base
// MStream/MFile vtables just need *a* definition to link, never a call.
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include "meatloaf.h"

MFile* MFSOwner::File(std::string path, bool default_to_flash)
{
    (void)path; (void)default_to_flash;
    fprintf(stderr, "native_stubs: MFSOwner::File called unexpectedly\n");
    abort();
}

std::shared_ptr<MStream> MFile::getSourceStream(std::ios_base::openmode mode)
{
    (void)mode;
    fprintf(stderr, "native_stubs: MFile::getSourceStream called unexpectedly\n");
    abort();
}

uint64_t MFile::getAvailableSpace()
{
    return 0;
}

uint16_t MStream::getTrackCount()
{
    fprintf(stderr, "native_stubs: MStream::getTrackCount called unexpectedly\n");
    abort();
}

uint16_t MStream::getSectorCount(uint16_t track)
{
    (void)track;
    fprintf(stderr, "native_stubs: MStream::getSectorCount called unexpectedly\n");
    abort();
}

bool MStream::seekBlock(uint64_t index, uint8_t offset)
{
    (void)index; (void)offset;
    fprintf(stderr, "native_stubs: MStream::seekBlock called unexpectedly\n");
    abort();
}

bool MStream::seekSector(uint8_t track, uint8_t sector, uint8_t offset)
{
    (void)track; (void)sector; (void)offset;
    fprintf(stderr, "native_stubs: MStream::seekSector(track,sector,offset) called unexpectedly\n");
    abort();
}

bool MStream::seekSector(std::vector<uint8_t> trackSectorOffset)
{
    (void)trackSectorOffset;
    fprintf(stderr, "native_stubs: MStream::seekSector(vector) called unexpectedly\n");
    abort();
}

bool MFile::exists()
{
    fprintf(stderr, "native_stubs: MFile::exists called unexpectedly\n");
    abort();
}

// util_debug_printf() is the non-ESP backend for the Debug_print*() macros
// (see include/debug.h's !ESP_PLATFORM branch). Its real implementation
// lives in lib/utils/utils.cpp, which transitively pulls in the SAM speech
// synthesizer (SDL2 audio + an ESP pinmap header) via samlib.h - unrelated to
// the disk-write engine and not natively compilable. This stub forwards to
// stderr so test failures still get useful diagnostic output.
void util_debug_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}
