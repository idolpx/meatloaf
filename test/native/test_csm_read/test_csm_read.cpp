// Read-path tests for the CSM cassette format.
//
// CSM has no magic number, no version and no directory - it is a flat run of
// decoded CBM tape blocks, and every entry's offset depends on the size of the
// one before it. readHeader() walking that chain correctly IS the format, so
// these tests build images byte by byte and drive the walk directly.
//
// The images are synthesized rather than taken from .archive/csm because the
// interesting cases (a truncated tape, a header whose end address precedes its
// start, a missing end-of-tape block) do not occur in the corpus. The layout
// they encode was verified against all 12 of those samples first.

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
static const char* CSM_PATH = "build_test_csm.csm";

static const uint32_t HEADER_BLOCK = 192;

// seekPath()/seekEntry()/readHeader() are protected; the tests drive them the
// way the drive and the directory listing do.
class TestCSMStream : public CSMMStream
{
public:
    using CSMMStream::CSMMStream;
    using CSMMStream::entries;
    using CSMMStream::entry;
    using CSMMStream::readHeader;
    using CSMMStream::seekEntry;
    using CSMMStream::seekPath;
    using CSMMStream::decodeType;
    using CSMMStream::getNextImageEntry;
    using CSMMStream::resetEntryCounter;
};

// Appends one 192-byte header block. `name` is placed in the 16-byte field and
// padded with spaces, exactly as a real tape header is.
static void appendHeader(std::vector<uint8_t>& img, uint8_t type,
                         uint16_t start, uint16_t end, const std::string& name)
{
    std::vector<uint8_t> block(HEADER_BLOCK, 0x20);
    block[0] = type;
    block[1] = start & 0xFF;
    block[2] = (start >> 8) & 0xFF;
    block[3] = end & 0xFF;
    block[4] = (end >> 8) & 0xFF;
    for (size_t i = 0; i < 16 && i < name.size(); i++)
        block[5 + i] = (uint8_t)name[i];
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
    FILE* fp = fopen(CSM_PATH, "wb");
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

// seekEntry() compares the query against mstr::toUTF8(entry.filename), because
// what arrives from the drive is PETSCII that has been through the same
// conversion. A test that passes a bare ASCII literal is asking a question the
// drive never asks.
static std::string q(const char* name)
{
    return mstr::toUTF8(name);
}

static std::shared_ptr<TestCSMStream> openImage()
{
    auto src = std::make_shared<FileContainerStream>(CSM_PATH);
    if (!src->isOpen())
        return nullptr;

    auto image = std::make_shared<TestCSMStream>(src);
    // A directly constructed media stream has an uninitialised `mode` - only
    // MFile::getSourceStream() sets it, and garbage carrying the `out` bit
    // makes seekPath() CREATE the entry it was asked to find.
    image->mode = std::ios_base::in;
    return image;
}

void setUp(void)
{
}

void tearDown(void)
{
    remove(CSM_PATH);
}

// The walk: entry n's offset is the sum of every preceding header block and
// data block, so getting this wrong misaligns everything after entry 0.
void test_walk_finds_every_entry(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)image->entries.size());

    TEST_ASSERT_EQUAL_STRING("LOADER", image->entries[0].filename);
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
    TEST_ASSERT_FALSE(image->seekEntry((uint16_t)3));
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
    TEST_ASSERT_EQUAL_STRING("ONLY", image->entries[0].filename);
}

// The two-byte load address is synthesized from the header - a CSM data block
// holds raw program bytes and does not carry one.
void test_read_prepends_synthesized_load_address(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekPath(q("LOADER")));

    // 256 data bytes plus the two synthesized ones.
    TEST_ASSERT_EQUAL_UINT32(258, image->size());

    uint8_t buf[258];
    memset(buf, 0, sizeof(buf));
    uint32_t total = 0;
    while (total < sizeof(buf))
    {
        uint32_t n = image->read(buf + total, sizeof(buf) - total);
        if (n == 0) break;
        total += n;
    }

    TEST_ASSERT_EQUAL_UINT32(258, total);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);   // $1001 low
    TEST_ASSERT_EQUAL_HEX8(0x10, buf[1]);   // $1001 high - not `& 0xFF00`
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[257]);
}

// The load address high byte must be shifted, not masked. $1001 >> 8 is $10;
// masking with 0xFF00 yields $1000 truncated to a byte, which is 0.
void test_load_address_high_byte_is_shifted(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekPath(q("LOADER")));

    uint8_t buf[2] = { 0, 0 };
    TEST_ASSERT_EQUAL_UINT32(2, image->read(buf, 2));
    TEST_ASSERT_EQUAL_HEX8(0x10, buf[1]);
}

// Reading the SECOND entry proves the walk's offsets are right: a misaligned
// data_offset serves the wrong file's bytes while still reporting a plausible
// size.
void test_second_entry_reads_its_own_bytes(void)
{
    // Both entries named, so the second can be reached by name and read
    // through the stream rather than probed in the file.
    std::vector<uint8_t> img;
    appendHeader(img, 1, 0x1001, 0x1101, "FIRST");
    appendData(img, 0x1001, 0x1101, 0xAA);
    appendHeader(img, 3, 0x2000, 0x2100, "SECOND");
    appendData(img, 0x2000, 0x2100, 0xBB);
    appendHeader(img, 5, 0, 0, "");
    writeImage(img);

    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekPath(q("SECOND")));
    TEST_ASSERT_EQUAL_UINT32(258, image->size());

    uint8_t buf[258];
    memset(buf, 0, sizeof(buf));
    uint32_t total = 0;
    while (total < sizeof(buf))
    {
        uint32_t n = image->read(buf + total, sizeof(buf) - total);
        if (n == 0) break;
        total += n;
    }

    TEST_ASSERT_EQUAL_UINT32(258, total);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);   // $2000 low
    TEST_ASSERT_EQUAL_HEX8(0x20, buf[1]);   // $2000 high
    // Its own fill, not the first entry's - a misaligned data_offset would
    // serve $AA here while still reporting a plausible size.
    TEST_ASSERT_EQUAL_HEX8(0xBB, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, buf[257]);
}

// Names are a fixed-width space-padded field. The padding is stripped, so a
// LOAD of the trimmed name matches; a field that is all padding trims to empty
// and is listed blank rather than as sixteen spaces.
void test_names_are_padding_trimmed(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_STRING("LOADER", image->entries[0].filename);
    TEST_ASSERT_EQUAL_STRING("", image->entries[1].filename);
}

// $A0 padding occurs in the wild (Motor Mouse.csm) and must be stripped too.
void test_a0_padded_name_is_trimmed(void)
{
    std::vector<uint8_t> img;
    appendHeader(img, 1, 0x1001, 0x1101, "MOTOR MOUSE");
    // Overwrite the three bytes after the name with $A0, as that tape does.
    img[5 + 11] = 0xA0;
    img[5 + 12] = 0xA0;
    img[5 + 13] = 0xA0;
    appendData(img, 0x1001, 0x1101, 0xAA);
    writeImage(img);

    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_STRING("MOTOR MOUSE", image->entries[0].filename);
}

// A tape can carry the same name twice - Abductor.csm has two ABDUCTOR entries,
// a BASIC loader and its payload. The earlier one is what a LOAD wants.
void test_duplicate_names_resolve_to_first(void)
{
    std::vector<uint8_t> img;
    appendHeader(img, 1, 0x1001, 0x1101, "ABDUCTOR");
    appendData(img, 0x1001, 0x1101, 0xAA);
    appendHeader(img, 1, 0x1001, 0x1201, "ABDUCTOR");
    appendData(img, 0x1001, 0x1201, 0xBB);
    appendHeader(img, 5, 0, 0, "");
    writeImage(img);

    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekPath(q("ABDUCTOR")));
    TEST_ASSERT_EQUAL_UINT32(HEADER_BLOCK, image->entry.data_offset);
    TEST_ASSERT_EQUAL_UINT32(256, image->entry.data_length);
}

// LOAD"*" must reach the first entry even though its name is not a literal.
void test_wildcard_matches_first_entry(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekEntry(std::string("*")));
    TEST_ASSERT_EQUAL_STRING("LOADER", image->entry.filename);
}

// A LOAD that has never been through a directory listing has never walked the
// container, so the walk has to happen lazily on lookup.
void test_seekEntry_walks_lazily(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    // No readHeader() call first.
    TEST_ASSERT_TRUE(image->seekEntry(q("LOADER")));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)image->entries.size());
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

// A missing name reports not-found rather than looping or serving entry 0.
void test_missing_name_is_not_found(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_FALSE(image->seekEntry(q("NOPE")));
}

// Sequential listing walks each entry once and then stops - it must not wrap
// around and list forever.
void test_listing_terminates(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    image->resetEntryCounter();

    TEST_ASSERT_TRUE(image->getNextImageEntry());
    TEST_ASSERT_TRUE(image->getNextImageEntry());
    TEST_ASSERT_FALSE(image->getNextImageEntry());
}

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
    TEST_ASSERT_EQUAL_STRING("GOOD", image->entries[0].filename);
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

    TEST_ASSERT_TRUE(image->seekPath(q("CUTOFF")));
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

// A file too short to hold even one header block yields an empty listing, not
// a fabricated entry.
void test_runt_file_yields_no_entries(void)
{
    std::vector<uint8_t> img(10, 0x00);
    writeImage(img);

    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_FALSE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)image->entries.size());
    TEST_ASSERT_FALSE(image->seekEntry((uint16_t)1));
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

// The walk must not repeat on every listing: rewindDirectory() calls
// readHeader() each time, and over the network each step is a range request.
void test_readHeader_is_idempotent(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)image->entries.size());
}

// CSM is read-only, as T64 is. MMediaStream::write() only routes through
// writeFile() once a file has been selected - without the seekPath() it writes
// container bytes verbatim, which is the base class's behaviour and not the
// question being asked here.
void test_write_to_selected_file_is_refused(void)
{
    writeStandardImage();
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekPath(q("LOADER")));

    uint8_t buf[4] = { 1, 2, 3, 4 };
    TEST_ASSERT_EQUAL_UINT32(0, image->write(buf, sizeof(buf)));
}

// The synthesized images above pin the logic; this pins the FORMAT MODEL
// against real tapes. Every corpus sample must walk to exactly its own length -
// entries then either an end-of-tape block or EOF, with no slack and no
// overrun. A single wrong field width or a missed terminator shows up here as
// leftover bytes. The corpus lives in .archive/, which is gitignored, so this
// skips cleanly when it is not present - the same arrangement test_hdd_read
// uses.
static const char* CORPUS_DIR = ".archive/csm";

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
        TEST_IGNORE_MESSAGE("corpus not present in .archive/csm");
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_walk_finds_every_entry);
    RUN_TEST(test_end_of_tape_block_terminates_and_is_not_listed);
    RUN_TEST(test_tape_without_end_block_is_fully_walked);
    RUN_TEST(test_read_prepends_synthesized_load_address);
    RUN_TEST(test_load_address_high_byte_is_shifted);
    RUN_TEST(test_second_entry_reads_its_own_bytes);
    RUN_TEST(test_names_are_padding_trimmed);
    RUN_TEST(test_a0_padded_name_is_trimmed);
    RUN_TEST(test_duplicate_names_resolve_to_first);
    RUN_TEST(test_wildcard_matches_first_entry);
    RUN_TEST(test_seekEntry_walks_lazily);
    RUN_TEST(test_type_decoding);
    RUN_TEST(test_missing_name_is_not_found);
    RUN_TEST(test_listing_terminates);
    RUN_TEST(test_reversed_addresses_stop_the_walk);
    RUN_TEST(test_truncated_data_block_is_clamped);
    RUN_TEST(test_partial_header_block_stops_the_walk);
    RUN_TEST(test_runt_file_yields_no_entries);
    RUN_TEST(test_immediate_end_block_yields_no_entries);
    RUN_TEST(test_readHeader_is_idempotent);
    RUN_TEST(test_write_to_selected_file_is_refused);
    RUN_TEST(test_corpus_samples_walk_to_exactly_eof);
    return UNITY_END();
}
