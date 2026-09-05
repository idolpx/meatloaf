// Read-path tests for the Wraptor (.wra) and Wraptor 3 (.wr3) archive format.
//
// The container half is trivial - a signature scan - and the compression half
// is an LZSS variant with a variable-width dictionary code. The awkward part
// for testing is that the format's 16-bit CRC has no published algorithm, so
// unlike ARC or LHA there is no stored checksum to decode against.
//
// Two independent oracles are used instead:
//
//  1. TERMINATION. The compressed stream carries its own end marker, and the
//     data of one entry runs to exactly two bytes before the next entry's
//     signature, so `extractEntry()` returning true means the stream ended
//     inside the span the container framed for it.
//
//     This is necessary but NOT sufficient, and it is worth knowing which:
//     reversing the bit order to LSB-first still terminates cleanly on every
//     entry in the corpus - it just produces 176 bytes where 511 belong. Only
//     the second oracle catches that. Measured, not assumed.
//
//  2. THE GEOS HEADER. Every entry in the corpus is a GEOS file, and Wraptor
//     prefixes those with nine bytes of its own that the format document does
//     not describe (established by inspection - see the layout below). Two of
//     those fields are DUPLICATED inside the GEOS info sector that follows,
//     254 bytes further on, and one is the file's block count on disk, which
//     reconciles with the decoded length. So the decoder's output cross-checks
//     against itself at two widely separated offsets and against its own total
//     size. Getting those to agree by accident is not plausible.
//
// The nine-byte Wraptor GEOS header:
//     0    GEOS file structure   0 sequential, 1 VLIR
//     1    GEOS file type
//     2-6  year, month, day, hour, minute
//     7-8  block count on disk, low/high
// followed at offset 9 by the GEOS info sector, whose first three bytes are
// always 03 15 BF (the record id and the 3x21 icon descriptor). Info-sector
// byte $NN therefore sits at payload offset $NN + 7.
//
// Nothing here reconstructs a GEOS file - the payload is served verbatim, as
// D64 already does for VLIR - so this header is documentation, not something
// the read path acts on.
//
// The corpus is real: six .WR3 and one .WRA in .data/media/archive/wr3, none of
// them written by this project. .data/media is gitignored, so these skip cleanly
// without it.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../test_disk_write/file_container_stream.h"
#include "media/archive/wra.h"
#include "string_utils.h"

static const char* CORPUS_DIR = ".data/media/archive/wr3";

static const char* CORPUS[] = {
    "G12841.WR3", "G12871.WR3", "G12881.WR3",
    "G6441.WR3", "G6471.WR3", "G6481.WR3",
    "PAINTVIEW_CODE.WRA",
};
static const size_t CORPUS_COUNT = sizeof(CORPUS) / sizeof(CORPUS[0]);

// Offsets within a decoded GEOS payload; see the layout note above.
static constexpr size_t GEOS_HEADER_SIZE = 9;
static constexpr size_t GEOS_INFO_SIZE   = 254;
static constexpr size_t GEOS_C64_TYPE    = 75;   // info sector $44
static constexpr size_t GEOS_FILE_TYPE   = 76;   // info sector $45
static constexpr size_t GEOS_STRUCTURE   = 77;   // info sector $46

class TestWRAStream : public WRAMStream
{
public:
    using WRAMStream::WRAMStream;
    using WRAMStream::data;
    using WRAMStream::decodeType;
    using WRAMStream::entries;
    using WRAMStream::entry;
    using WRAMStream::extractEntry;
    using WRAMStream::readHeader;
    using WRAMStream::seekEntry;
    using WRAMStream::seekPath;
};

static std::shared_ptr<FileContainerStream> g_src;

static std::shared_ptr<TestWRAStream> openImage(const std::string& path,
                                                std::shared_ptr<FileContainerStream>& src)
{
    src = std::make_shared<FileContainerStream>(path);
    if (!src->isOpen())
        return nullptr;

    auto image = std::make_shared<TestWRAStream>(src);
    // mode is uninitialised on a directly constructed stream - only
    // MFile::getSourceStream() sets it, and garbage carrying the out bit sends
    // seekPath() down the write branch.
    image->mode = std::ios_base::in;
    return image;
}

static bool looksLikeGeos(const std::vector<uint8_t>& payload)
{
    return payload.size() >= GEOS_HEADER_SIZE + GEOS_INFO_SIZE
        && payload[9] == 0x03 && payload[10] == 0x15 && payload[11] == 0xBF;
}

void setUp(void) {}

void tearDown(void)
{
    // Artifacts are removed here, never inline: on Windows remove() fails
    // while the stream still holds the file open, and a failed assertion skips
    // any cleanup below it.
    if (g_src != nullptr)
    {
        g_src->close();
        g_src = nullptr;
    }
    remove("build_wra_not_a_wra.bin");
}


// The signature scan, and the fields each hit is validated on.
void test_directory_lists_entries(void)
{
    for (size_t c = 0; c < CORPUS_COUNT; c++)
    {
        auto image = openImage(std::string(CORPUS_DIR) + "/" + CORPUS[c], g_src);
        if (image == nullptr)
            TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/wr3");

        char message[128];
        snprintf(message, sizeof(message), "%s", CORPUS[c]);

        TEST_ASSERT_TRUE_MESSAGE(image->readHeader(), message);

        // Every archive in the corpus holds exactly four entries.
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, (uint32_t)image->entries.size(), message);

        uint32_t previous_end = 0;
        for (const auto& e : image->entries)
        {
            TEST_ASSERT_TRUE(e.file_type >= 1 && e.file_type <= 4);
            TEST_ASSERT_TRUE(e.filename.size() > 0 && e.filename.size() <= 32);
            TEST_ASSERT_TRUE(e.data_end > e.data_offset);
            TEST_ASSERT_TRUE(e.data_offset > e.sig_offset);
            // Spans are ordered and do not overlap.
            TEST_ASSERT_TRUE(e.sig_offset >= previous_end);
            previous_end = e.data_end + 2;
        }

        // The last entry's data ends two bytes before the end of the file.
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(g_src->size(),
                                         image->entries.back().data_end + 2, message);

        g_src->close();
        g_src = nullptr;
    }
}

// Wraptor writes the name exactly as the CBM directory held it and says
// nothing about its encoding, so the decision is per name. Both branches are
// pinned here, because getting either wrong makes the name a listing shows
// differ from the name a lookup matches.
void test_name_decoding_covers_both_encodings(void)
{
    // A GEOS name is the ASCII GEOS itself uses. It must survive untouched -
    // mstr::toUTF8() would map its lowercase bytes to graphics characters.
    TEST_ASSERT_EQUAL_STRING("ReadPaint", WRAMStream::decodeName("ReadPaint").c_str());
    TEST_ASSERT_EQUAL_STRING("M.Randall", WRAMStream::decodeName("M.Randall").c_str());

    // An all-uppercase name is read as PETSCII, which lowercases it - the
    // repository-wide internal convention, so that it round-trips back through
    // toPETSCII2() to the bytes the container holds.
    TEST_ASSERT_EQUAL_STRING("geos", WRAMStream::decodeName("GEOS").c_str());
    TEST_ASSERT_EQUAL_STRING("desk top", WRAMStream::decodeName("DESK TOP").c_str());
}

// And the two sides agree: a name in the form a listing emits is the name a
// lookup finds, for an entry of each encoding.
void test_entries_are_found_by_their_listed_name(void)
{
    auto image = openImage(std::string(CORPUS_DIR) + "/PAINTVIEW_CODE.WRA", g_src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/wr3");

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_STRING("ReadPaint", image->entries[0].filename.c_str());

    TEST_ASSERT_TRUE(image->seekEntry(WRAMStream::decodeName(image->entries[0].filename)));
    TEST_ASSERT_EQUAL_STRING("ReadPaint", image->entry.filename.c_str());
    TEST_ASSERT_FALSE(image->seekEntry(std::string("no such file here")));

    g_src->close();
    g_src = nullptr;

    image = openImage(std::string(CORPUS_DIR) + "/G6441.WR3", g_src);
    TEST_ASSERT_NOT_NULL(image);
    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_STRING("GEOS", image->entries[0].filename.c_str());

    TEST_ASSERT_TRUE(image->seekEntry(std::string("geos")));
    TEST_ASSERT_EQUAL_STRING("GEOS", image->entry.filename.c_str());
}

// A wildcard resolves to the first match, as it does everywhere else.
void test_wildcard_resolves_to_the_first_entry(void)
{
    auto image = openImage(std::string(CORPUS_DIR) + "/G6441.WR3", g_src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/wr3");

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_TRUE(image->seekEntry(std::string("*")));
    TEST_ASSERT_EQUAL_STRING(image->entries[0].filename.c_str(), image->entry.filename.c_str());
}

// The load-bearing one. Every entry of every archive decodes to its own end
// marker, and its output cross-checks against itself: the two fields Wraptor
// duplicates from the GEOS info sector 254 bytes further into the payload, and
// the block count reconciled against the decoded length.
void test_every_entry_decodes_and_reconciles(void)
{
    size_t checked = 0;

    for (size_t c = 0; c < CORPUS_COUNT; c++)
    {
        auto image = openImage(std::string(CORPUS_DIR) + "/" + CORPUS[c], g_src);
        if (image == nullptr)
            TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/wr3");

        TEST_ASSERT_TRUE(image->readHeader());

        for (size_t i = 0; i < image->entries.size(); i++)
        {
            const std::string name = WRAMStream::decodeName(image->entries[i].filename);

            char message[192];
            snprintf(message, sizeof(message), "%s entry %u [%s]",
                     CORPUS[c], (unsigned)(i + 1), name.c_str());

            // Decoding at all means the stream reached its own end marker
            // inside the span the container framed for it.
            TEST_ASSERT_TRUE_MESSAGE(image->seekPath(name), message);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)image->data.size(),
                                             image->size(), message);

            const std::vector<uint8_t>& payload = image->data;
            TEST_ASSERT_TRUE_MESSAGE(looksLikeGeos(payload), message);

            const uint8_t structure = payload[0];
            const uint8_t geos_type = payload[1];
            const uint16_t blocks = (uint16_t)(payload[7] | (payload[8] << 8));

            // Duplicated inside the info sector, 254 bytes further on. These
            // are the two offsets that make the decode self-checking.
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(geos_type, payload[GEOS_FILE_TYPE], message);
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(structure, payload[GEOS_STRUCTURE], message);

            // The C64 file type the info sector reports, which the container's
            // own type byte tracks: type 4 entries are PRG, type 3 are USR.
            const uint8_t c64_type = payload[GEOS_C64_TYPE];
            TEST_ASSERT_TRUE_MESSAGE(c64_type >= 0x81 && c64_type <= 0x83, message);
            if (image->entry.file_type == 3)
                TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x82, c64_type, message);
            if (image->entry.file_type == 4)
                TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, c64_type, message);

            // The block count against the decoded length. A sequential GEOS
            // file is the info sector plus its data chain, so this is exact. A
            // VLIR file also has an index sector and rounds each record up to a
            // whole block, so its stored count can only be bounded below.
            const size_t body = payload.size() - GEOS_HEADER_SIZE - GEOS_INFO_SIZE;
            const uint32_t data_blocks = (uint32_t)((body + 253) / 254);

            if (structure == 0)
            {
                TEST_ASSERT_EQUAL_UINT32_MESSAGE(1 + data_blocks, blocks, message);
            }
            else
            {
                TEST_ASSERT_TRUE_MESSAGE(body >= 254, message);
                const uint32_t record_blocks = (uint32_t)((body - 254 + 253) / 254);
                TEST_ASSERT_TRUE_MESSAGE(blocks >= 2 + record_blocks, message);
            }

            checked++;
        }

        g_src->close();
        g_src = nullptr;
    }

    // Seven archives, four entries each. Asserting the total stops a future
    // corpus reorganisation from silently reducing this to a walk over nothing.
    TEST_ASSERT_EQUAL_UINT32(28, (uint32_t)checked);
}

// The three G128 archives and the three G64 archives were each built
// independently - their per-entry CRCs differ - yet they carry the same
// programs. Decoding the same entry out of different containers must produce
// byte-identical output, which no amount of self-consistency inside one
// archive can demonstrate.
void test_the_same_entry_decodes_identically_across_archives(void)
{
    struct Group { const char* files[3]; const char* entry; uint32_t size; };
    static const Group groups[] = {
        { { "G12841.WR3", "G12871.WR3", "G12881.WR3" }, "128 configure", 19617 },
        { { "G6441.WR3",  "G6471.WR3",  "G6481.WR3"  }, "desk top",      29972 },
    };

    for (const auto& group : groups)
    {
        std::vector<uint8_t> reference;

        for (int i = 0; i < 3; i++)
        {
            auto image = openImage(std::string(CORPUS_DIR) + "/" + group.files[i], g_src);
            if (image == nullptr)
                TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/wr3");

            char message[128];
            snprintf(message, sizeof(message), "%s [%s]", group.files[i], group.entry);

            TEST_ASSERT_TRUE_MESSAGE(image->readHeader(), message);
            TEST_ASSERT_TRUE_MESSAGE(image->seekPath(std::string(group.entry)), message);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(group.size, (uint32_t)image->data.size(), message);

            if (i == 0)
                reference = image->data;
            else
                TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(reference.data(), image->data.data(),
                                                      group.size, message);

            g_src->close();
            g_src = nullptr;
        }
    }
}

// The stream serves exactly the bytes it decoded, through the real read path.
void test_reading_an_entry_serves_exactly_its_bytes(void)
{
    auto image = openImage(std::string(CORPUS_DIR) + "/G6441.WR3", g_src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/wr3");

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_TRUE(image->seekPath(std::string("geos")));

    const std::vector<uint8_t> expected = image->data;
    TEST_ASSERT_EQUAL_UINT32(511, (uint32_t)expected.size());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)expected.size(), image->size());

    std::vector<uint8_t> got;
    uint8_t buffer[256];
    for (int guard = 0; guard < 65536; guard++)
    {
        uint32_t n = image->read(buffer, sizeof(buffer));
        if (n == 0)
            break;
        got.insert(got.end(), buffer, buffer + n);
    }

    TEST_ASSERT_EQUAL_UINT32((uint32_t)expected.size(), (uint32_t)got.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), got.data(), expected.size());
}

// A listing must never decode anything - the format document calls that "very
// slow" - so the size it reports is the COMPRESSED span. This pins the choice:
// the two numbers genuinely differ, and for these entries the compressed span
// is the smaller of the two.
void test_listing_size_is_the_compressed_span(void)
{
    auto image = openImage(std::string(CORPUS_DIR) + "/G6441.WR3", g_src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/wr3");

    TEST_ASSERT_TRUE(image->readHeader());

    const auto& e = image->entries[1];      // GEOBOOT, 21616 bytes decoded
    const uint32_t span = e.data_end - e.data_offset;

    TEST_ASSERT_TRUE(image->seekPath(WRAMStream::decodeName(e.filename)));
    TEST_ASSERT_EQUAL_UINT32(21616, (uint32_t)image->data.size());
    TEST_ASSERT_TRUE(span < image->data.size());
}

// The corpus is 100% GEOS, so nothing in it exercises a PLAIN entry - and
// "a plain file carries no nine-byte header" is the claim the decision to
// serve payloads verbatim rests on. The format document's own worked example
// is the only non-GEOS sample that exists, so it is used as a fixture.
//
// It is a complete entry: 80 bytes that terminate exactly at the CRC, and
// decode to a real C64 BASIC program - load address $0801, a link pointer to
// $0823, line 10, `LOAD "POOYAN.LOADER",8,1`. Byte 0 is the load address, NOT
// a Wraptor header, which is the point.
//
// It also re-establishes the bit order independently of the GEOS corpus: a
// program whose BASIC link pointer walks correctly cannot come out of a
// decoder reading its bits the other way round.
static const uint8_t POOYAN_SAMPLE[] = {
    0xFF,0x42,0x4C,0xFF,0x50,0x4F,0x4F,0x59,0x41,0x4E,0x00,0x02,0x00,0x82,0x04,0x60,
    0x80,0x50,0x01,0x16,0x41,0x59,0x0C,0x14,0xF0,0x81,0x0C,0x47,0x49,0x31,0x11,0x40,
    0x9E,0x4F,0x2C,0x90,0x49,0xC2,0xE2,0x61,0x3C,0x82,0x44,0x22,0x94,0x84,0x42,0xC1,
    0xC0,0xB0,0x62,0x00,0x21,0x82,0x02,0x90,0x62,0x0C,0x61,0x63,0x19,0x43,0xD4,0x4D,
    0x20,0x92,0x49,0xD1,0xF3,0x13,0x41,0x01,0xE0,0x02,0x78,0x68,0x37,0x1C,0x0D,0x40,
    0x14,0xE1,0x40,0x00,0xDD,0x0B,0xFF,0x42,0x4C,0xFF,0x50,0x4F,0x4F,0x59,0x41,0x4E,
    0x2E,0x4D,0x41,0x49,0x4E,0x00,0x02,0x7E,0x0C,0x0A,0xB0,0x31,0x31,0x00,0x08,0x00,
    0x00,0x09,0x40,0x00,0x05,0x2A,0x00,0x02,0xA5,0x54,0x3C,0x81,0x10,0x88,0x00,0x08,
};

void test_a_plain_prg_entry_has_no_geos_header(void)
{
    const std::string path = "build_wra_not_a_wra.bin";

    FILE* fp = fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fwrite(POOYAN_SAMPLE, 1, sizeof(POOYAN_SAMPLE), fp);
    fclose(fp);

    g_src = std::make_shared<FileContainerStream>(path);
    TEST_ASSERT_TRUE(g_src->isOpen());
    auto image = std::make_shared<TestWRAStream>(g_src);
    image->mode = std::ios_base::in;

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)image->entries.size());
    TEST_ASSERT_EQUAL_STRING("POOYAN", image->entries[0].filename.c_str());
    TEST_ASSERT_EQUAL_UINT8(2, image->entries[0].file_type);          // PRG
    TEST_ASSERT_EQUAL_STRING("POOYAN.MAIN", image->entries[1].filename.c_str());

    // An all-uppercase name is read as PETSCII, so it is held lowercased.
    TEST_ASSERT_TRUE(image->seekPath(std::string("pooyan")));
    TEST_ASSERT_EQUAL_UINT32(80, (uint32_t)image->data.size());

    // Byte 0 is the PRG load address, not a Wraptor header. A GEOS payload
    // would have 03 15 BF at offset 9; this one must not.
    const uint8_t expected[] = {
        0x01,0x08,             // load address $0801
        0x23,0x08,             // link to $0823
        0x0A,0x00,             // line 10
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, image->data.data(), sizeof(expected));
    TEST_ASSERT_FALSE(looksLikeGeos(image->data));

    // The BASIC text of that line. Uppercase ASCII and unshifted PETSCII have
    // the same byte values, so it compares directly.
    const std::string text((const char*)image->data.data() + 16, 19);
    TEST_ASSERT_EQUAL_STRING("\"POOYAN.LOADER\",8,1", text.c_str());

    // The second entry is truncated - the document's dump stops mid-stream -
    // so it must fail rather than serve a short file.
    TEST_ASSERT_FALSE(image->seekPath(std::string("pooyan.main")));
}

// The signature scan reads the container in chunks and carries three bytes
// across each boundary, so that a signature straddling one is still found.
// No corpus archive happens to place one there, so it is built.
void test_a_signature_straddling_a_scan_boundary_is_found(void)
{
    const std::string path = "build_wra_not_a_wra.bin";

    // Second signature at 1022, so two of its four bytes fall in the first
    // 1024-byte chunk and two in the second.
    std::vector<uint8_t> image_bytes;
    const uint8_t sig[] = { 0xFF, 0x42, 0x4C, 0xFF };

    image_bytes.insert(image_bytes.end(), sig, sig + 4);
    image_bytes.push_back('A');
    image_bytes.push_back(0x00);
    image_bytes.push_back(0x02);
    while (image_bytes.size() < 1022)
        image_bytes.push_back(0x55);        // filler, never a signature byte

    TEST_ASSERT_EQUAL_UINT32(1022, (uint32_t)image_bytes.size());
    image_bytes.insert(image_bytes.end(), sig, sig + 4);
    image_bytes.push_back('B');
    image_bytes.push_back(0x00);
    image_bytes.push_back(0x02);
    for (int i = 0; i < 8; i++)
        image_bytes.push_back(0x55);

    FILE* fp = fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fwrite(image_bytes.data(), 1, image_bytes.size(), fp);
    fclose(fp);

    g_src = std::make_shared<FileContainerStream>(path);
    TEST_ASSERT_TRUE(g_src->isOpen());
    auto image = std::make_shared<TestWRAStream>(g_src);
    image->mode = std::ios_base::in;

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)image->entries.size());
    TEST_ASSERT_EQUAL_STRING("A", image->entries[0].filename.c_str());
    TEST_ASSERT_EQUAL_STRING("B", image->entries[1].filename.c_str());
    TEST_ASSERT_EQUAL_UINT32(1022, image->entries[1].sig_offset);
}

// The container's type byte is what a listing shows.
void test_file_types_decode(void)
{
    auto image = openImage(std::string(CORPUS_DIR) + "/G6441.WR3", g_src);
    if (image == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/wr3");

    TEST_ASSERT_EQUAL_STRING("seq", image->decodeType(1).c_str());
    TEST_ASSERT_EQUAL_STRING("prg", image->decodeType(2).c_str());
    TEST_ASSERT_EQUAL_STRING("usr", image->decodeType(3).c_str());
    TEST_ASSERT_EQUAL_STRING("prg", image->decodeType(4).c_str());
}

// Something with no signature in it has to be refused rather than walked.
void test_non_archive_is_refused(void)
{
    const std::string path = "build_wra_not_a_wra.bin";

    FILE* fp = fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(fp);
    std::vector<uint8_t> junk(4096);
    for (size_t i = 0; i < junk.size(); i++)
        junk[i] = (uint8_t)(0x40 + (i % 7));
    fwrite(junk.data(), 1, junk.size(), fp);
    fclose(fp);

    g_src = std::make_shared<FileContainerStream>(path);
    TEST_ASSERT_TRUE(g_src->isOpen());
    auto image = std::make_shared<TestWRAStream>(g_src);
    image->mode = std::ios_base::in;

    TEST_ASSERT_FALSE(image->readHeader());
}

// A signature followed by a type byte the format does not define is a false
// positive inside compressed data - the format document warns it can happen -
// and must be dropped rather than turned into an entry.
void test_a_bogus_signature_is_not_an_entry(void)
{
    const std::string path = "build_wra_not_a_wra.bin";

    FILE* fp = fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(fp);
    const uint8_t bogus[] = {
        0xFF, 0x42, 0x4C, 0xFF,             // signature
        'J', 'U', 'N', 'K', 0x00,           // name
        0x09,                               // type 9 - not 1..4
        0x00, 0x11, 0x22, 0x33,             // "data"
    };
    fwrite(bogus, 1, sizeof(bogus), fp);
    fclose(fp);

    g_src = std::make_shared<FileContainerStream>(path);
    TEST_ASSERT_TRUE(g_src->isOpen());
    auto image = std::make_shared<TestWRAStream>(g_src);
    image->mode = std::ios_base::in;

    TEST_ASSERT_FALSE(image->readHeader());
}

// A truncated entry stops at the end of its span without the stream's own end
// marker, and that has to fail rather than serve a short file that looks whole.
void test_a_truncated_entry_fails_rather_than_truncating(void)
{
    const std::string path = "build_wra_not_a_wra.bin";

    auto source = openImage(std::string(CORPUS_DIR) + "/G6441.WR3", g_src);
    if (source == nullptr)
        TEST_IGNORE_MESSAGE("corpus not present in .data/media/archive/wr3");

    TEST_ASSERT_TRUE(source->readHeader());
    const uint32_t keep = source->entries[0].data_end - 32;

    std::vector<uint8_t> head(keep + 2, 0);
    TEST_ASSERT_TRUE(g_src->seek(0));
    uint32_t got = 0;
    while (got < keep)
    {
        uint32_t n = g_src->read(head.data() + got, keep - got);
        TEST_ASSERT_TRUE(n > 0);
        got += n;
    }
    g_src->close();
    g_src = nullptr;

    FILE* fp = fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fwrite(head.data(), 1, head.size(), fp);
    fclose(fp);

    g_src = std::make_shared<FileContainerStream>(path);
    TEST_ASSERT_TRUE(g_src->isOpen());
    auto image = std::make_shared<TestWRAStream>(g_src);
    image->mode = std::ios_base::in;

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)image->entries.size());
    TEST_ASSERT_FALSE(image->seekPath(WRAMStream::decodeName(image->entries[0].filename)));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)image->data.size());
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_directory_lists_entries);
    RUN_TEST(test_name_decoding_covers_both_encodings);
    RUN_TEST(test_entries_are_found_by_their_listed_name);
    RUN_TEST(test_wildcard_resolves_to_the_first_entry);
    RUN_TEST(test_reading_an_entry_serves_exactly_its_bytes);
    RUN_TEST(test_listing_size_is_the_compressed_span);
    RUN_TEST(test_a_plain_prg_entry_has_no_geos_header);
    RUN_TEST(test_a_signature_straddling_a_scan_boundary_is_found);
    RUN_TEST(test_file_types_decode);
    RUN_TEST(test_non_archive_is_refused);
    RUN_TEST(test_a_bogus_signature_is_not_an_entry);
    RUN_TEST(test_a_truncated_entry_fails_rather_than_truncating);
    RUN_TEST(test_the_same_entry_decodes_identically_across_archives);
    RUN_TEST(test_every_entry_decodes_and_reconciles);

    return UNITY_END();
}
