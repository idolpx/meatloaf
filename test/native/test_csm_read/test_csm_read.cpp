// Read-path tests for the CSM cassette format.
//
// Two things are being pinned, and they are independent:
//
//   1. The WALK. CSM has no magic number, no version and no directory - it is
//      a flat run of decoded CBM tape blocks, and every entry's offset depends
//      on the size of the one before it. Walking that chain correctly IS the
//      format.
//   2. The DATASETTE BEHAVIOUR. A listing serves one entry at a time and wraps
//      at the end of the tape, and a load searches forward from the current
//      position, wrapping once. That is what makes a tape carrying the same
//      name twice - a BASIC loader and its payload - resolve positionally.
//
// The images are synthesized rather than taken from .data/media/csm because the
// interesting cases (a truncated tape, a header whose end address precedes its
// start, a missing end-of-tape block) do not occur in the corpus. The layout
// they encode was verified against all 12 of those samples first, and the last
// test here re-checks it against them.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../test_disk_write/file_container_stream.h"
#include "media/tape/csm.h"

// Written fresh by each test and removed by tearDown(), in the working
// directory, matching the disk-write suite's "build_*" artifact convention.
// Never remove an artifact inline: on Windows remove() fails while the stream
// still holds the file open, and a failed assertion skips any cleanup below it.
//
// The path is DISTINCT PER TEST, and that is load-bearing rather than tidy.
// CSMState is shared per container URL through a weak_ptr registry, and Unity's
// TEST_ASSERT failure path is a longjmp - which does not unwind C++ destructors,
// so a failing test leaks its stream, the state never expires from the registry,
// and every later test on the same URL inherits a stale tape. One real failure
// then cascades into a dozen meaningless ones. Same reason the archive suite
// gives its tests distinct URLs for SessionBroker.
static std::string CSM_PATH;

static const uint32_t HEADER_BLOCK = 192;

// readHeader()/serveCurrent()/entries are protected; the tests drive them the
// way the drive and the directory listing do.
class TestCSMStream : public CSMMStream
{
public:
    using CSMMStream::CSMMStream;
    using CSMMStream::entries;
    using CSMMStream::readHeader;
    using CSMMStream::decodeType;
};

// Appends one 192-byte header block. `name` is placed in the 16-byte field and
// padded with `pad`, as a real tape header is.
static void appendHeader(std::vector<uint8_t>& img, uint8_t type,
                         uint16_t start, uint16_t end, const std::string& name,
                         uint8_t pad = 0x20)
{
    std::vector<uint8_t> block(HEADER_BLOCK, pad);
    block[0] = type;
    block[1] = start & 0xFF;
    block[2] = (start >> 8) & 0xFF;
    block[3] = end & 0xFF;
    block[4] = (end >> 8) & 0xFF;
    for (size_t i = 0; i < 16; i++)
        block[5 + i] = (i < name.size()) ? (uint8_t)name[i] : pad;
    img.insert(img.end(), block.begin(), block.end());
}

// Appends a data block of (end - start) bytes, filled with `fill` so a read can
// be checked byte for byte.
static void appendData(std::vector<uint8_t>& img, uint16_t start, uint16_t end, uint8_t fill)
{
    img.insert(img.end(), (size_t)(end - start), fill);
}

static void writeImage(const std::vector<uint8_t>& img)
{
    FILE* fp = fopen(CSM_PATH.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(fp);
    if (!img.empty())
        fwrite(img.data(), 1, img.size(), fp);
    fclose(fp);
}

// Two program entries then an end-of-tape block, which is the shape every
// multi-part tape in the corpus has:
//   0: "LOADER"  $1001-$1101  (256 bytes, fill $AA)
//   1: ""        $1000-$1400  (1024 bytes, fill $BB) - blank name, as on tape
static void writeStandardImage()
{
    std::vector<uint8_t> img;
    appendHeader(img, 1, 0x1001, 0x1101, "LOADER");
    appendData(img, 0x1001, 0x1101, 0xAA);
    appendHeader(img, 3, 0x1000, 0x1400, "");
    appendData(img, 0x1000, 0x1400, 0xBB);
    appendHeader(img, 5, 0x1000, 0x1400, "");   // end of tape, no data follows
    writeImage(img);
}

// Name matching compares against mstr::toUTF8(the entry name), because
// what arrives from the drive is PETSCII that has been through the same
// conversion. A test that passes a bare ASCII literal is asking a question the
// drive never asks.
static std::string q(const char* name)
{
    return mstr::toUTF8(name);
}

// Two streams opened on the same path share one tape position, which is what
// test_streams_share_tape_position relies on - and why each test needs its own
// path (see CSM_PATH).
static std::shared_ptr<TestCSMStream> openImage()
{
    auto src = std::make_shared<FileContainerStream>(CSM_PATH);
    if (!src->isOpen())
        return nullptr;

    auto image = std::make_shared<TestCSMStream>(src);
    // A directly constructed media stream has an uninitialised `mode` - only
    // MFile::getSourceStream() sets it, and garbage carrying the `out` bit
    // makes seekNextEntry() refuse as a write.
    image->mode = std::ios_base::in;
    image->setDefaultName("TAPEIMAGE");
    return image;
}

// Drives the stream exactly as MFile::getSourceStream()'s browsable branch
// does, so these tests exercise the real resolution path rather than a
// convenient shortcut. On success the stream has already been served and is
// ready to read.
//
// Note this takes a FRESH stream per call in the tests below, because that is
// what the drive does - a new decoded stream is built for every open, which is
// why the scan bookkeeping is per-instance while the tape position is shared.
static bool loadByName(std::shared_ptr<TestCSMStream>& image, const char* want)
{
    std::string wanted = q(want);
    bool wildcard = (mstr::contains(wanted, "*") || mstr::contains(wanted, "?"));

    std::string pointed = image->seekNextEntry();
    while (!pointed.empty())
    {
        // No conversion: seekNextEntry() returns UTF-8, exactly as
        // MFile::getSourceStream()'s browsable branch now treats it.
        std::string entryName = pointed;
        if (mstr::compareFilename(entryName, wanted, wildcard))
            return true;
        pointed = image->seekNextEntry();
    }
    return false;
}

// Reads a selected entry to completion. MStream::read() returns at most one
// block, so a caller wanting the whole file loops.
static uint32_t readAll(std::shared_ptr<TestCSMStream>& image, uint8_t* buf, uint32_t cap)
{
    uint32_t total = 0;
    while (total < cap)
    {
        uint32_t n = image->read(buf + total, cap - total);
        if (n == 0) break;
        total += n;
    }
    return total;
}

void setUp(void)
{
    static int seq = 0;
    CSM_PATH = "build_test_csm_" + std::to_string(seq++) + ".csm";
}

void tearDown(void)
{
    remove(CSM_PATH.c_str());
}


/********************************************************
 * The walk
 ********************************************************/

// Entry n's offset is the sum of every preceding header block and data block,
// so getting this wrong misaligns everything after entry 0.
void test_walk_finds_every_entry(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)image->entries.size());

    TEST_ASSERT_EQUAL_STRING("loader", image->entries[0].name.c_str());
    TEST_ASSERT_EQUAL_UINT16(0x1001, image->entries[0].start_address);
    TEST_ASSERT_EQUAL_UINT32(HEADER_BLOCK, image->entries[0].data_offset);
    TEST_ASSERT_EQUAL_UINT32(256, image->entries[0].data_length);

    // 192 + 256 + 192
    TEST_ASSERT_EQUAL_UINT32(640, image->entries[1].data_offset);
    TEST_ASSERT_EQUAL_UINT32(1024, image->entries[1].data_length);
}

// A type-$05 block ends the tape. Its address fields are not a data length -
// the jsvic20 reference decoder reads them as one and runs off the end of four
// of the corpus samples.
void test_end_of_tape_block_terminates_and_is_not_listed(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());

    // Two entries, not three: the $05 block is a terminator, not a file.
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)image->entries.size());
}

// A tape whose last entry runs to EOF with no $05 block is still fully read -
// several corpus samples end this way.
void test_tape_without_end_block_is_fully_walked(void)
{
    std::vector<uint8_t> img;
    appendHeader(img, 1, 0x1001, 0x1101, "ONLY");
    appendData(img, 0x1001, 0x1101, 0xAA);
    writeImage(img);

    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)image->entries.size());
    TEST_ASSERT_EQUAL_STRING("only", image->entries[0].name.c_str());
}

// Names are a fixed-width padded field. The padding is stripped, so a LOAD of
// the trimmed name matches; a field that is all padding trims to empty and the
// entry is listed under the media file's name instead.
void test_names_are_padding_trimmed(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_STRING("loader", image->entries[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("", image->entries[1].name.c_str());
}

// $A0 padding occurs in the wild (Motor Mouse.csm) and must be stripped too.
void test_a0_padded_name_is_trimmed(void)
{
    std::vector<uint8_t> img;
    appendHeader(img, 1, 0x1001, 0x1101, "MOTOR MOUSE");
    img[5 + 11] = 0xA0;
    img[5 + 12] = 0xA0;
    img[5 + 13] = 0xA0;
    appendData(img, 0x1001, 0x1101, 0xAA);
    writeImage(img);

    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_STRING("motor mouse", image->entries[0].name.c_str());
}

// Types 1 and 3 are both programs; 2 and 4 are sequential.
void test_type_decoding(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_EQUAL_STRING(" PRG", image->decodeType(1).c_str());
    TEST_ASSERT_EQUAL_STRING(" PRG", image->decodeType(3).c_str());
    TEST_ASSERT_EQUAL_STRING(" SEQ", image->decodeType(2).c_str());
    TEST_ASSERT_EQUAL_STRING(" SEQ", image->decodeType(4).c_str());
}

// The walk must not repeat on every listing: rewindDirectory() calls into it
// each time, and over the network each step is a range request.
void test_readHeader_is_idempotent(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)image->entries.size());
}


/********************************************************
 * Datasette behaviour
 ********************************************************/

// One entry at a time, in tape order, then the end of the tape.
void test_listing_is_sequential_then_ends(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->nextTapeEntry());
    TEST_ASSERT_EQUAL_STRING("loader", image->current.name.c_str());

    TEST_ASSERT_TRUE(image->nextTapeEntry());
    TEST_ASSERT_EQUAL_STRING("", image->current.name.c_str());

    TEST_ASSERT_FALSE(image->nextTapeEntry());
    TEST_ASSERT_TRUE(image->tapeEnded());
}

// At the end of the tape it rewinds, so listing again starts over rather than
// staying stuck at the end.
void test_tape_wraps_after_end(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    while (image->nextTapeEntry()) { }
    TEST_ASSERT_TRUE(image->tapeEnded());

    image->resetTape();
    TEST_ASSERT_FALSE(image->tapeEnded());

    TEST_ASSERT_TRUE(image->nextTapeEntry());
    TEST_ASSERT_EQUAL_STRING("loader", image->current.name.c_str());
}

// An entry the tape leaves unnamed is listed under the media file's name,
// rather than as an unloadable blank.
void test_unnamed_entry_uses_media_name(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->nextTapeEntry());
    TEST_ASSERT_TRUE(image->nextTapeEntry());   // the blank-named one

    TEST_ASSERT_EQUAL_STRING("TAPEIMAGE", image->entryDisplayName(image->current).c_str());
}

// The whole point of the sequential model: a tape carrying the same name twice
// resolves POSITIONALLY. Abductor.csm is exactly this - a BASIC loader and its
// payload, both called ABDUCTOR. Loading it twice must give the loader, then
// the payload, not the loader twice.
void test_duplicate_names_resolve_positionally(void)
{
    std::vector<uint8_t> img;
    appendHeader(img, 1, 0x1001, 0x1101, "ABDUCTOR");   // 256 bytes, $AA
    appendData(img, 0x1001, 0x1101, 0xAA);
    appendHeader(img, 1, 0x1001, 0x1201, "ABDUCTOR");   // 512 bytes, $BB
    appendData(img, 0x1001, 0x1201, 0xBB);
    appendHeader(img, 5, 0, 0, "");
    writeImage(img);

    // Two separate opens, as two LOADs really are - fresh streams sharing one
    // tape position through the registry.
    auto first = openImage();
    TEST_ASSERT_NOT_NULL(first.get());
    TEST_ASSERT_TRUE(loadByName(first, "ABDUCTOR"));
    TEST_ASSERT_EQUAL_UINT32(258, first->size());
    uint8_t a[8];
    TEST_ASSERT_TRUE(readAll(first, a, sizeof(a)) > 2);
    TEST_ASSERT_EQUAL_HEX8(0xAA, a[2]);

    auto second = openImage();
    TEST_ASSERT_NOT_NULL(second.get());
    TEST_ASSERT_TRUE(loadByName(second, "ABDUCTOR"));
    TEST_ASSERT_EQUAL_UINT32(514, second->size());
    uint8_t b[8];
    TEST_ASSERT_TRUE(readAll(second, b, sizeof(b)) > 2);
    TEST_ASSERT_EQUAL_HEX8(0xBB, b[2]);
}

// A load searches forward from the current position, so a name behind the head
// is only reached by wrapping.
void test_load_searches_forward_and_wraps(void)
{
    std::vector<uint8_t> img;
    appendHeader(img, 1, 0x1001, 0x1101, "FIRST");
    appendData(img, 0x1001, 0x1101, 0xAA);
    appendHeader(img, 1, 0x2000, 0x2100, "SECOND");
    appendData(img, 0x2000, 0x2100, 0xBB);
    appendHeader(img, 5, 0, 0, "");
    writeImage(img);

    // Move the head past FIRST.
    auto ahead = openImage();
    TEST_ASSERT_NOT_NULL(ahead.get());
    TEST_ASSERT_TRUE(loadByName(ahead, "SECOND"));

    // FIRST is now behind the head; reaching it requires the wrap.
    auto back = openImage();
    TEST_ASSERT_NOT_NULL(back.get());
    TEST_ASSERT_TRUE(loadByName(back, "FIRST"));
    TEST_ASSERT_EQUAL_UINT32(258, back->size());

    uint8_t buf[8];
    TEST_ASSERT_TRUE(readAll(back, buf, sizeof(buf)) > 2);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x10, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[2]);
}

// The end of the tape is not the end of a scan: the head runs back to the
// start. "" comes only after a full lap, which is what stops a miss looping.
void test_scan_wraps_past_end_of_tape(void)
{
    writeStandardImage();

    // A listing leaves the head on the LAST entry.
    auto listing = openImage();
    TEST_ASSERT_NOT_NULL(listing.get());
    TEST_ASSERT_TRUE(listing->nextTapeEntry());
    TEST_ASSERT_TRUE(listing->nextTapeEntry());

    auto load = openImage();
    TEST_ASSERT_NOT_NULL(load.get());

    // The entry left ready - the last one, which is unnamed.
    TEST_ASSERT_EQUAL_STRING("TAPEIMAGE", load->seekNextEntry().c_str());
    // Past the end: wraps to the start rather than reporting the end.
    TEST_ASSERT_EQUAL_STRING("loader", load->seekNextEntry().c_str());
    // A full lap done - now the scan terminates.
    TEST_ASSERT_EQUAL_STRING("", load->seekNextEntry().c_str());
}

// A name that is not on the tape must terminate, not circle it forever.
void test_missing_name_terminates(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_FALSE(loadByName(image, "NOPE"));
}

// LOAD"*" takes whatever is at the current position - the entry the last
// directory request left ready.
void test_wildcard_takes_the_current_entry(void)
{
    writeStandardImage();

    // As a listing would: advance to the second entry.
    auto listing = openImage();
    TEST_ASSERT_NOT_NULL(listing.get());
    TEST_ASSERT_TRUE(listing->nextTapeEntry());
    TEST_ASSERT_TRUE(listing->nextTapeEntry());

    auto load = openImage();
    TEST_ASSERT_NOT_NULL(load.get());
    TEST_ASSERT_TRUE(loadByName(load, "*"));
    TEST_ASSERT_EQUAL_UINT32(1026, load->size());   // the 1024-byte entry
}

// A tape is a datasette, not a directory: getSourceStream() must take the
// browsable branch and drive seekNextEntry(), never seekPath().
void test_stream_is_browsable_not_random_access(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->isBrowsable());
    TEST_ASSERT_FALSE(image->isRandomAccess());
}

// File opens create a FRESH stream while listings use the ImageBroker
// instance, so the datasette position has to live in state shared per
// container URL - otherwise a LOAD"*" after a listing would rewind to entry 0.
void test_streams_share_tape_position(void)
{
    writeStandardImage();

    auto listing = openImage();
    TEST_ASSERT_NOT_NULL(listing.get());

    // The "listing" advances to the second entry.
    TEST_ASSERT_TRUE(listing->nextTapeEntry());
    TEST_ASSERT_TRUE(listing->nextTapeEntry());

    // A separate stream on the same container sees that position.
    auto load = openImage();
    TEST_ASSERT_NOT_NULL(load.get());
    TEST_ASSERT_TRUE(load->have_current);
    TEST_ASSERT_EQUAL_UINT32(1024, load->current.data_length);

    // ...and its walk cost nothing, the entries being shared too.
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)load->entries.size());
}


/********************************************************
 * Reading
 ********************************************************/

// The two-byte load address is synthesized from the header - a CSM data block
// holds raw program bytes and does not carry one.
void test_read_prepends_synthesized_load_address(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(loadByName(image, "LOADER"));
    TEST_ASSERT_EQUAL_UINT32(258, image->size());   // 256 data + 2 synthesized

    uint8_t buf[258];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(258, readAll(image, buf, sizeof(buf)));

    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);   // $1001 low
    TEST_ASSERT_EQUAL_HEX8(0x10, buf[1]);   // $1001 high - shifted, not masked
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[257]);
}

// Reading a LATER entry proves the walk's offsets are right: a misaligned
// data_offset serves the wrong entry's bytes while still reporting a plausible
// size.
void test_later_entry_reads_its_own_bytes(void)
{
    std::vector<uint8_t> img;
    appendHeader(img, 1, 0x1001, 0x1101, "FIRST");
    appendData(img, 0x1001, 0x1101, 0xAA);
    appendHeader(img, 3, 0x2000, 0x2100, "SECOND");
    appendData(img, 0x2000, 0x2100, 0xBB);
    appendHeader(img, 5, 0, 0, "");
    writeImage(img);

    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(loadByName(image, "SECOND"));
    TEST_ASSERT_EQUAL_UINT32(258, image->size());

    uint8_t buf[258];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(258, readAll(image, buf, sizeof(buf)));

    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);   // $2000 low
    TEST_ASSERT_EQUAL_HEX8(0x20, buf[1]);   // $2000 high
    TEST_ASSERT_EQUAL_HEX8(0xBB, buf[2]);   // its own fill, not FIRST's
    TEST_ASSERT_EQUAL_HEX8(0xBB, buf[257]);
}

// CSM is read-only. MMediaStream::write() only routes through writeFile() once
// a file has been selected - without the load it writes container bytes
// verbatim, which is the base class's behaviour and not the question here.
void test_write_to_selected_file_is_refused(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(loadByName(image, "LOADER"));

    uint8_t buf[4] = { 1, 2, 3, 4 };
    TEST_ASSERT_EQUAL_UINT32(0, image->write(buf, sizeof(buf)));
}


/********************************************************
 * Corrupt input
 ********************************************************/

// A header whose end address precedes its start describes no readable data;
// the walk stops there rather than computing a huge length from the underflow.
void test_reversed_addresses_stop_the_walk(void)
{
    std::vector<uint8_t> img;
    appendHeader(img, 1, 0x1001, 0x1101, "GOOD");
    appendData(img, 0x1001, 0x1101, 0xAA);
    appendHeader(img, 1, 0x1400, 0x1000, "BAD");    // end < start
    appendData(img, 0x1001, 0x1101, 0xCC);
    writeImage(img);

    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)image->entries.size());
    TEST_ASSERT_EQUAL_STRING("good", image->entries[0].name.c_str());
}

// A tape cut off mid-file serves the bytes that are there instead of reading
// past the end of the container.
void test_truncated_data_block_is_clamped(void)
{
    std::vector<uint8_t> img;
    appendHeader(img, 1, 0x1001, 0x1101, "CUTOFF");  // declares 256 bytes
    img.insert(img.end(), 100, 0xAA);                // only 100 are present
    writeImage(img);

    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)image->entries.size());
    TEST_ASSERT_EQUAL_UINT32(100, image->entries[0].data_length);

    TEST_ASSERT_TRUE(loadByName(image, "CUTOFF"));
    TEST_ASSERT_EQUAL_UINT32(102, image->size());
}

// A header block that does not fit in what remains is the end of the tape.
void test_partial_header_block_stops_the_walk(void)
{
    std::vector<uint8_t> img;
    appendHeader(img, 1, 0x1001, 0x1101, "ONE");
    appendData(img, 0x1001, 0x1101, 0xAA);
    img.insert(img.end(), 50, 0x20);    // 50 bytes of a 192-byte header
    writeImage(img);

    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)image->entries.size());
}

// A file too short to hold even one header block yields an empty tape, not a
// fabricated entry.
void test_runt_file_yields_no_entries(void)
{
    std::vector<uint8_t> img(10, 0x00);
    writeImage(img);

    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_FALSE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)image->entries.size());
    TEST_ASSERT_FALSE(image->nextTapeEntry());
    TEST_ASSERT_TRUE(image->tapeEnded());
}

// An image whose first block is an end-of-tape marker holds nothing.
void test_immediate_end_block_yields_no_entries(void)
{
    std::vector<uint8_t> img;
    appendHeader(img, 5, 0x1000, 0x1400, "");
    writeImage(img);

    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_FALSE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)image->entries.size());
}

// An empty tape must not make a load spin looking for a name to wrap around to.
void test_load_on_empty_tape_terminates(void)
{
    std::vector<uint8_t> img;
    appendHeader(img, 5, 0, 0, "");
    writeImage(img);

    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_FALSE(loadByName(image, "ANYTHING"));
    TEST_ASSERT_FALSE(loadByName(image, "*"));
}


/********************************************************
 * Corpus
 ********************************************************/

// The synthesized images above pin the logic; this pins the FORMAT MODEL
// against real tapes. Every corpus sample must walk to exactly its own length -
// entries then either an end-of-tape block or EOF, with no slack and no
// overrun. A single wrong field width or a missed terminator shows up here as
// leftover bytes. The corpus lives in .data/media/, which is gitignored, so this
// skips cleanly when it is not present - the same arrangement test_hdd_read
// uses.
static const char* CORPUS_DIR = ".data/media/csm";

static const char* CORPUS[] = {
    "3D Silicon Fish.csm", "Abductor.csm", "Alien Attack.csm", "Alien Soccer.csm",
    "Alpha Blaster.csm", "Another Vic in the Wall.csm", "Catcha Troopa.csm",
    "Crazey Cavey.csm", "Motor Mouse.csm", "Scram20.csm", "Vic Panic.csm",
    "Wacky Waiters.csm",
};

void test_corpus_samples_walk_to_exactly_eof(void)
{
    int checked = 0;

    for (size_t i = 0; i < sizeof(CORPUS) / sizeof(CORPUS[0]); i++)
    {
        std::string path = std::string(CORPUS_DIR) + "/" + CORPUS[i];

        auto src = std::make_shared<FileContainerStream>(path);
        if (!src->isOpen())
            continue;   // corpus not present

        auto image = std::make_shared<TestCSMStream>(src);
        image->mode = std::ios_base::in;

        TEST_ASSERT_TRUE_MESSAGE(image->readHeader(), CORPUS[i]);
        TEST_ASSERT_TRUE_MESSAGE(image->entries.size() > 0, CORPUS[i]);

        // Where the last entry's data ends. The file is either exactly that
        // long, or that plus one 192-byte end-of-tape block.
        const auto& last = image->entries.back();
        uint32_t consumed = last.data_offset + last.data_length;
        uint32_t total = src->size();

        bool exact = (consumed == total) || (consumed + HEADER_BLOCK == total);
        TEST_ASSERT_TRUE_MESSAGE(exact, CORPUS[i]);

        checked++;
    }

    if (checked == 0)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/csm");
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();

    // The walk
    RUN_TEST(test_walk_finds_every_entry);
    RUN_TEST(test_end_of_tape_block_terminates_and_is_not_listed);
    RUN_TEST(test_tape_without_end_block_is_fully_walked);
    RUN_TEST(test_names_are_padding_trimmed);
    RUN_TEST(test_a0_padded_name_is_trimmed);
    RUN_TEST(test_type_decoding);
    RUN_TEST(test_readHeader_is_idempotent);

    // Datasette behaviour
    RUN_TEST(test_listing_is_sequential_then_ends);
    RUN_TEST(test_tape_wraps_after_end);
    RUN_TEST(test_unnamed_entry_uses_media_name);
    RUN_TEST(test_duplicate_names_resolve_positionally);
    RUN_TEST(test_load_searches_forward_and_wraps);
    RUN_TEST(test_scan_wraps_past_end_of_tape);
    RUN_TEST(test_stream_is_browsable_not_random_access);
    RUN_TEST(test_missing_name_terminates);
    RUN_TEST(test_wildcard_takes_the_current_entry);
    RUN_TEST(test_streams_share_tape_position);

    // Reading
    RUN_TEST(test_read_prepends_synthesized_load_address);
    RUN_TEST(test_later_entry_reads_its_own_bytes);
    RUN_TEST(test_write_to_selected_file_is_refused);

    // Corrupt input
    RUN_TEST(test_reversed_addresses_stop_the_walk);
    RUN_TEST(test_truncated_data_block_is_clamped);
    RUN_TEST(test_partial_header_block_stops_the_walk);
    RUN_TEST(test_runt_file_yields_no_entries);
    RUN_TEST(test_immediate_end_block_yields_no_entries);
    RUN_TEST(test_load_on_empty_tape_terminates);

    // Corpus
    RUN_TEST(test_corpus_samples_walk_to_exactly_eof);

    return UNITY_END();
}
