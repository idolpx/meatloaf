// Entry-lookup tests for the container formats whose directory state is
// parsed once and cached on the stream: ARK, LBR, LNX and T64.
//
// A LOAD never lists first: MFile::getSourceStream() builds a FRESH decoded
// stream and goes straight to seekPath()/seekEntry(). Each of these formats
// used to parse its directory only in <Format>MFile::rewindDirectory(), so a
// listing worked and a load did not. These tests drive the lookup the way a
// LOAD does - with no listing first - and assert it agrees with the same
// lookup on a stream whose directory was parsed explicitly.
//
// The fixtures are written byte by byte here rather than checked in, so the
// tests are self-contained and the exact layout each parser expects is
// visible next to the assertions.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../test_disk_write/file_container_stream.h"
#include "media/archive/ark.h"
#include "media/archive/lbr.h"
#include "media/archive/lnx.h"
#include "media/tape/t64.h"

// Artifact names follow the disk-write suite's "build_*" convention; every
// one is removed by tearDown().
static const char* ARK_PATH = "build_test_entries.ark";
static const char* LBR_PATH = "build_test_entries.lbr";
static const char* LNX_PATH = "build_test_entries.lnx";
static const char* T64_PATH = "build_test_entries.t64";

// Both fixture entries are named so the second one distinguishes a correct
// walk from one that always lands on the first entry.
static const char* NAME_1 = "GAME";
static const char* NAME_2 = "LOADER";

/********************************************************
 * Fixture builders
 ********************************************************/

// Samples under .data/media/ are gitignored, so tests that use them skip when
// they aren't present.
static bool haveFile(const char* path)
{
    FILE* fp = fopen(path, "rb");
    if (fp == nullptr)
        return false;
    fclose(fp);
    return true;
}

static void writeFile(const char* path, const std::vector<uint8_t>& bytes)
{
    FILE* fp = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    if (!bytes.empty())
        fwrite(bytes.data(), 1, bytes.size(), fp);
    fclose(fp);
}

static void appendText(std::vector<uint8_t>& out, const std::string& s)
{
    out.insert(out.end(), s.begin(), s.end());
}

// name padded to `width` with `pad`
static void appendPadded(std::vector<uint8_t>& out, const std::string& name,
                         size_t width, uint8_t pad)
{
    for (size_t i = 0; i < width; i++)
        out.push_back(i < name.size() ? (uint8_t)name[i] : pad);
}

// ARK: byte 0 is the entry count, then 29-byte directory entries, then file
// data on the next 254-byte boundary.
static void buildArk()
{
    std::vector<uint8_t> b;
    b.push_back(2);                     // entry count

    for (const char* name : { NAME_1, NAME_2 })
    {
        b.push_back(0x82);              // file_type: closed PRG
        b.push_back(0x10);              // lsu_byte
        appendPadded(b, name, 16, 0xA0);
        for (int i = 0; i < 9; i++)     // rel/geos/date fields
            b.push_back(0);
        b.push_back(2); b.push_back(0); // blocks (LE)
    }

    b.resize(254 * 3, 0);               // room for the data area
    writeFile(ARK_PATH, b);
}

// LBR: "DWB <count> \r", then "<name>\r<type>\r <size> \r" per entry, then
// the file data. readUntil() consumes its delimiter, which is what the lone
// spaces before the size fields are for.
static void buildLbr()
{
    std::vector<uint8_t> b;
    appendText(b, "DWB 2 \r");
    appendText(b, std::string(NAME_1) + "\rP\r 100 \r");
    appendText(b, std::string(NAME_2) + "\rP\r 50 \r");
    b.resize(b.size() + 150, 0);        // file data
    writeFile(LBR_PATH, b);
}

// LNX: a BASIC loader, then a signature line containing "LYNX", the
// directory block count, the entry count, and then per entry a 16-byte
// padded name, CR, block count, type and last-sector-used.
static void buildLnx()
{
    std::vector<uint8_t> b;
    appendText(b, "\x01\x08 BASIC LOADER STUB \x00\x00");
    appendText(b, "LYNX ARCHIVE\r");
    appendText(b, " 1 \r");             // directory blocks
    appendText(b, " 2 \r");             // entry count

    for (const char* name : { NAME_1, NAME_2 })
    {
        appendPadded(b, name, 16, 0xA0);
        b.push_back(0x0D);
        appendText(b, " 2 \r");         // block count
        appendText(b, " P \r");         // type
        appendText(b, " 254 \r");       // last sector used
    }

    // A Lynx block is 254 bytes of data, so this archive is 1 directory block
    // plus 2 entries x 2 blocks = 5 blocks. Sizing it in 256-byte units left
    // the second entry running past EOF.
    b.resize(254 * 5, 0);
    writeFile(LNX_PATH, b);
}

// T64: 32-byte signature, header at 0x20 (version, entry_max, entry_count,
// unused, 24-byte tape name), 32-byte entries from 0x40.
static void buildT64()
{
    std::vector<uint8_t> b;
    appendPadded(b, "C64S tape image file", 32, 0x20);

    b.push_back(0x00); b.push_back(0x01);   // version
    b.push_back(0x02); b.push_back(0x00);   // entry_max
    b.push_back(0x02); b.push_back(0x00);   // entry_count
    b.push_back(0x00); b.push_back(0x00);   // unused
    appendPadded(b, "TEST TAPE", 24, 0x20);

    uint32_t data_offset = 0x80;
    for (const char* name : { NAME_1, NAME_2 })
    {
        b.push_back(0x01);                  // entry_type: normal tape file
        b.push_back(0x82);                  // file_type: PRG
        b.push_back(0x01); b.push_back(0x08); // start address $0801
        b.push_back(0x01); b.push_back(0x09); // end address $0901
        b.push_back(0x00); b.push_back(0x00); // free_1
        b.push_back((uint8_t)(data_offset & 0xFF));
        b.push_back((uint8_t)((data_offset >> 8) & 0xFF));
        b.push_back(0x00); b.push_back(0x00);
        b.push_back(0x00); b.push_back(0x00); b.push_back(0x00); b.push_back(0x00); // free_2
        appendPadded(b, name, 16, 0x20);
        data_offset += 0x100;
    }

    b.resize(0x80 + 0x200, 0);              // file data
    writeFile(T64_PATH, b);
}

/********************************************************
 * Test-only subclasses: seekEntry()/readHeader() are protected, and the
 * tests drive them exactly as seekPath() does.
 ********************************************************/

#define EXPOSE_STREAM(Test, Base, ...)                    \
    class Test : public Base                              \
    {                                                     \
    public:                                               \
        using Base::Base;                                 \
        using Base::entry;                                \
        using Base::entry_count;                          \
        using Base::containerStream;                      \
        using Base::readHeader;                           \
        using Base::seekEntry;                            \
        using Base::seekPath;                             \
        __VA_ARGS__                                       \
    }

// The isolation tests below deliberately use loadEntries()/readHeader() - the
// API that existed before the fix - so this file compiles unchanged against
// both revisions and a before/after run compares like with like.
EXPOSE_STREAM(TestARKStream, ARKMStream);
EXPOSE_STREAM(TestLBRStream, LBRMStream, using LBRMStream::loadEntries;);
EXPOSE_STREAM(TestLNXStream, LNXMStream, using LNXMStream::loadEntries;);
EXPOSE_STREAM(TestT64Stream, T64MStream);

template <typename T>
static std::shared_ptr<T> openImage(const char* path)
{
    auto src = std::make_shared<FileContainerStream>(path);
    if (!src->isOpen())
        return nullptr;

    auto image = std::make_shared<T>(src);
    // A directly constructed media stream has an uninitialised `mode` - only
    // MFile::getSourceStream() sets it.
    image->mode = std::ios_base::in;
    return image;
}

void setUp(void)
{
    buildArk();
    buildLbr();
    buildLnx();
    buildT64();
}

void tearDown(void)
{
    remove(ARK_PATH);
    remove(LBR_PATH);
    remove(LNX_PATH);
    remove(T64_PATH);
}

/********************************************************
 * ARK
 ********************************************************/

// Without the entry count, ARK's `index > entry_count` bound is (size_t)-1:
// the walk runs off the end of the directory into file data, and seekPath()
// derives its data offset from that same number.
void test_ark_lookup_without_listing(void)
{
    auto image = openImage<TestARKStream>(ARK_PATH);
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)2));
    TEST_ASSERT_EQUAL_STRING_LEN(NAME_2, image->entry.filename, strlen(NAME_2));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)image->entry_count);

    // Past the last entry must now fail rather than reading file data.
    TEST_ASSERT_FALSE(image->seekEntry((uint16_t)3));
}

void test_ark_wildcard_without_listing(void)
{
    auto image = openImage<TestARKStream>(ARK_PATH);
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekEntry(std::string("*")));
    TEST_ASSERT_EQUAL_STRING_LEN(NAME_1, image->entry.filename, strlen(NAME_1));
}

/********************************************************
 * LBR
 ********************************************************/

void test_lbr_lookup_without_listing(void)
{
    auto image = openImage<TestLBRStream>(LBR_PATH);
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)2));
    TEST_ASSERT_EQUAL_STRING(NAME_2, image->entry.filename.c_str());
}

// LBR's by-name walk called readEntry(), which it never overrode, so the base
// always returned false and the loop never ran - a second defect that had to
// be fixed for any by-name load to work at all.
void test_lbr_by_name_walk_runs(void)
{
    auto image = openImage<TestLBRStream>(LBR_PATH);
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekEntry(mstr::toUTF8(NAME_2)));
    TEST_ASSERT_EQUAL_STRING(NAME_2, image->entry.filename.c_str());
}

void test_lbr_missing_name_is_not_found(void)
{
    auto image = openImage<TestLBRStream>(LBR_PATH);
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_FALSE(image->seekEntry(mstr::toUTF8("NOPE")));
}

/********************************************************
 * LNX
 ********************************************************/

void test_lnx_lookup_without_listing(void)
{
    auto image = openImage<TestLNXStream>(LNX_PATH);
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)2));
    TEST_ASSERT_EQUAL_STRING_LEN(NAME_2, image->entry.filename.c_str(), strlen(NAME_2));
}

// The lazy parse must happen once per stream, not once per lookup: each
// re-parse would append another copy of every entry to `entries`.
void test_lnx_repeated_lookups_do_not_grow_the_directory(void)
{
    auto image = openImage<TestLNXStream>(LNX_PATH);
    TEST_ASSERT_NOT_NULL(image.get());

    for (int i = 0; i < 3; i++)
    {
        TEST_ASSERT_TRUE(image->seekEntry((uint16_t)1));
        TEST_ASSERT_TRUE(image->seekEntry((uint16_t)2));
    }

    TEST_ASSERT_FALSE(image->seekEntry((uint16_t)3));
}

// $A0 is the pad; a trailing SPACE belongs to the name. This archive is the
// case that proves it - two entries whose names differ ONLY in six trailing
// spaces. mstr::rtrimA0() strips whitespace as well as $A0, which collapsed
// them into one name and made the second file unreachable.
// A Lynx block carries 254 bytes - it is a CBM disk block minus its two link
// bytes - and LSU is the INDEX of the last used byte in the 256-byte sector,
// where data begins at index 2. So a block contributes 254, the last one
// contributes lsu - 1, and the first entry begins at directory_blocks * 254.
//
// The inherited MStream::block_size is 256 and was being used for all three,
// which put every entry two bytes late: the first byte a caller saw was the
// THIRD byte of the file, so a PRG lost its load address and came back
// starting at its BASIC link pointer instead.
void test_lnx_entry_offsets_and_sizes_use_254_byte_blocks(void)
{
    auto image = openImage<TestLNXStream>(LNX_PATH);
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)1));
    TEST_ASSERT_EQUAL_UINT32(254, image->entry.offset);          // 1 dir block
    TEST_ASSERT_EQUAL_UINT32(254 + 253, image->entry.size);      // 2 blocks, lsu 254

    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)2));
    TEST_ASSERT_EQUAL_UINT32(254 + 2 * 254, image->entry.offset);
    TEST_ASSERT_EQUAL_UINT32(254 + 253, image->entry.size);
}

// The invariant that settles the arithmetic against media nobody here wrote:
// entries tile the archive with no gaps, so the last one must end at EOF (or
// within its final partly-used block of it). With 256-byte blocks the walk
// overshoots by two bytes per block and the last entry runs past the end.
void test_lnx_real_archive_entries_tile_the_file(void)
{
    static const char* PATH = ".data/media/archive/lnx/Acid_Rain.lnx";
    if (!haveFile(PATH))
        TEST_IGNORE_MESSAGE("sample .data/media/archive/lnx/Acid_Rain.lnx not present");

    auto image = openImage<TestLNXStream>(PATH);
    TEST_ASSERT_NOT_NULL(image.get());

    uint32_t file_size = image->containerStream->size();
    TEST_ASSERT_GREATER_THAN_UINT32(0, file_size);

    uint16_t last = 0;
    for (uint16_t i = 1; image->seekEntry(i); i++)
    {
        TEST_ASSERT_TRUE(image->entry.offset + image->entry.size <= file_size);
        last = i;
    }
    TEST_ASSERT_GREATER_THAN_UINT16(0, last);

    // The final entry ends inside its last block, so at most 253 bytes of
    // padding separate it from EOF.
    TEST_ASSERT_TRUE(image->seekEntry(last));
    uint32_t end = image->entry.offset + image->entry.size;
    TEST_ASSERT_TRUE(end <= file_size);
    TEST_ASSERT_TRUE(file_size - end < 254);
}

void test_lnx_real_archive_keeps_trailing_spaces(void)
{
    static const char* PATH = ".data/media/archive/lnx/Cloud King.lnx";
    if (!haveFile(PATH))
        TEST_IGNORE_MESSAGE("sample .data/media/archive/lnx/Cloud King.lnx not present");

    auto image = openImage<TestLNXStream>(PATH);
    TEST_ASSERT_NOT_NULL(image.get());

    // "CLOUD KING" + six $A0 pad bytes
    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)1));
    TEST_ASSERT_EQUAL_STRING("CLOUD KING", image->entry.filename.c_str());

    // "CLOUD KING" + six real spaces
    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)2));
    TEST_ASSERT_EQUAL_STRING("CLOUD KING      ", image->entry.filename.c_str());

    // ...and the two must resolve to different files.
    TEST_ASSERT_TRUE(image->seekEntry(mstr::toUTF8("CLOUD KING")));
    uint32_t first = image->entry.size;
    TEST_ASSERT_TRUE(image->seekEntry(mstr::toUTF8("CLOUD KING      ")));
    TEST_ASSERT_NOT_EQUAL(first, image->entry.size);
}

// The two trims differ by one letter and confusing them is exactly the bug
// above, so pin the distinction: rtrimA0() takes only the PETSCII pad,
// rtrimPad() also takes spaces (for space-padded fields like CBM tape
// headers - see TapeDecoder::harvestEntries and csip.cpp).
void test_rtrim_variants_differ_on_trailing_space(void)
{
    std::string a0 = "CLOUD KING      ";
    mstr::rtrimA0(a0);
    TEST_ASSERT_EQUAL_STRING("CLOUD KING      ", a0.c_str());

    std::string pad = "CLOUD KING      ";
    mstr::rtrimPad(pad);
    TEST_ASSERT_EQUAL_STRING("CLOUD KING", pad.c_str());

    // Both remove $A0, and rtrimPad() handles a mix in either order.
    std::string mixed_a0 = std::string("NAME") + "\xA0\xA0";
    mstr::rtrimA0(mixed_a0);
    TEST_ASSERT_EQUAL_STRING("NAME", mixed_a0.c_str());

    std::string mixed = std::string("NAME") + " \xA0 \xA0";
    mstr::rtrimPad(mixed);
    TEST_ASSERT_EQUAL_STRING("NAME", mixed.c_str());
}

/********************************************************
 * T64
 ********************************************************/

// T64's bound is header.entry_count, and `header` is an uninitialised POD
// until readHeader() runs - so this failed or passed depending on what the
// allocation happened to contain.
void test_t64_lookup_without_listing(void)
{
    auto image = openImage<TestT64Stream>(T64_PATH);
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)2));
    TEST_ASSERT_EQUAL_STRING_LEN(NAME_2, image->entry.filename, strlen(NAME_2));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)image->entry_count);

    TEST_ASSERT_FALSE(image->seekEntry((uint16_t)3));
}

void test_t64_wildcard_without_listing(void)
{
    auto image = openImage<TestT64Stream>(T64_PATH);
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekEntry(std::string("*")));
    TEST_ASSERT_EQUAL_STRING_LEN(NAME_1, image->entry.filename, strlen(NAME_1));
}

/********************************************************
 * Entry sizes, against the real archives.
 *
 * <Format>MFile::getNextFileInDir() is what puts a size in the listing, and
 * it cannot be reached natively (it needs MFSOwner/ImageBroker). What these
 * pin down is the number it now reads: the directory field for LBR, and the
 * derived expression for ARK. Both were checked against the whole-file
 * arithmetic - see the comments on each.
 *
 * .data/media/ is gitignored, so these skip when the samples aren't present.
 ********************************************************/

static const char* REAL_LBR = ".data/media/archive/lbr/zbbs-files!.lbr";
static const char* REAL_ARK = ".data/media/archive/ark/Turbo_Assembler5t.ark";

// LBR stores byte counts directly. Directory ends at 344 and the sizes sum
// to 4774; 344 + 4774 = 5118 of the archive's 5120 bytes.
void test_lbr_real_archive_sizes(void)
{
    if (!haveFile(REAL_LBR))
        TEST_IGNORE_MESSAGE("sample .data/media/archive/lbr/zbbs-files!.lbr not present");

    auto image = openImage<TestLBRStream>(REAL_LBR);
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)1));
    TEST_ASSERT_EQUAL_STRING(".SET-UP.BBS", image->entry.filename.c_str());
    TEST_ASSERT_EQUAL_UINT32(77, image->entry.size);

    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)2));
    TEST_ASSERT_EQUAL_UINT32(151, image->entry.size);

    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)3));
    TEST_ASSERT_EQUAL_UINT32(4, image->entry.size);

    TEST_ASSERT_EQUAL_UINT32(18, (uint32_t)image->entry_count);
}

// ARK stores a block count plus the bytes used in the last block. Data
// starts on the first 254-byte boundary after the directory and each file
// occupies blocks*254, so 254 + 762 + 16256 + 207 = 17479 - the archive's
// exact length, with the last file truncated to its real size.
void test_ark_real_archive_sizes(void)
{
    if (!haveFile(REAL_ARK))
        TEST_IGNORE_MESSAGE("sample .data/media/archive/ark/Turbo_Assembler5t.ark not present");

    auto image = openImage<TestARKStream>(REAL_ARK);
    TEST_ASSERT_NOT_NULL(image.get());

    struct { uint16_t blocks; uint8_t lsu; uint32_t expect; } want[] = {
        { 3,  155, 662   },
        { 64, 129, 16130 },
        { 1,  208, 207   },
    };

    for (uint16_t i = 0; i < 3; i++)
    {
        TEST_ASSERT_TRUE(image->seekEntry((uint16_t)(i + 1)));
        TEST_ASSERT_EQUAL_UINT32(want[i].blocks, image->entry.blocks);
        TEST_ASSERT_EQUAL_UINT32(want[i].lsu, image->entry.lsu_byte);

        uint32_t size = image->entry.blocks
                      ? ((image->entry.blocks - 1) * 254) + image->entry.lsu_byte - 1
                      : 0;
        TEST_ASSERT_EQUAL_UINT32(want[i].expect, size);
    }

    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)image->entry_count);
}

// seekPath() has to skip the space every preceding file occupies to reach a
// file's data. It walked that with readEntry(), which ARKMStream never
// overrides, so `entry` was never updated and the walk added the TARGET
// file's block count once per preceding entry - every file after the first
// resolved into the middle of an earlier one.
//
// Each of the three files in the sample starts with distinct bytes, so
// reading through seekPath() and comparing against the raw image at the
// independently computed offset catches a wrong landing spot.
void test_ark_real_archive_data_offsets(void)
{
    if (!haveFile(REAL_ARK))
        TEST_IGNORE_MESSAGE("sample .data/media/archive/ark/Turbo_Assembler5t.ark not present");

    // Directory is 3*29+1 bytes, so data starts on the 254 boundary at 254;
    // then each file occupies blocks*254 (3 and 64 blocks).
    struct { const char* name; uint32_t offset; uint32_t size; } want[] = {
        { "FAST/ESM",      254,               662   },
        { "TASS V5.3/ESM", 254 + 762,         16130 },
        { "FAST.HLP",      254 + 762 + 16256, 207   },
    };

    // Raw image, to compare the decoded bytes against.
    FILE* fp = fopen(REAL_ARK, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    fseek(fp, 0, SEEK_END);
    long raw_len = ftell(fp);
    std::vector<uint8_t> raw((size_t)raw_len);
    fseek(fp, 0, SEEK_SET);
    TEST_ASSERT_EQUAL_UINT32(raw.size(), fread(raw.data(), 1, raw.size(), fp));
    fclose(fp);

    for (const auto& w : want)
    {
        auto image = openImage<TestARKStream>(REAL_ARK);
        TEST_ASSERT_NOT_NULL(image.get());

        TEST_ASSERT_TRUE_MESSAGE(image->seekPath(mstr::toUTF8(w.name)), w.name);
        TEST_ASSERT_EQUAL_UINT32(w.size, image->size());

        // The landing spot itself, not just the bytes: several files in these
        // archives open with the same BASIC loader stub, so a short content
        // comparison can match at the wrong offset.
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(w.offset, image->containerStream->position(), w.name);

        // The entry left selected must be the one that was asked for, not
        // whichever one the offset walk visited last.
        TEST_ASSERT_EQUAL_STRING_LEN(w.name, image->entry.filename, strlen(w.name));

        uint8_t got[64] = {0};
        TEST_ASSERT_EQUAL_UINT32(sizeof(got), image->read(got, sizeof(got)));
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(&raw[w.offset], got, sizeof(got), w.name);
    }
}

// The listing escapes a '/' in a CBM name as '\' (T64, LNX, M2I and the disk
// formats all do), and seekEntry() converts it back. This walks that round
// trip on the name as it appears in `ls`, on an archive where the entry is
// four deep so the offset walk has real work to do.
//
// It also stands in for what cannot be tested here: typing that name at the
// console does NOT work, because ESP-IDF's esp_console_split_argv() treats
// the backslash as its own escape character and drops "\T" entirely.
void test_ark_backslash_name_round_trip(void)
{
    static const char* PATH = ".data/media/archive/ark/Tpztools.ark";
    if (!haveFile(PATH))
        TEST_IGNORE_MESSAGE("sample .data/media/archive/ark/Tpztools.ark not present");

    auto image = openImage<TestARKStream>(PATH);
    TEST_ASSERT_NOT_NULL(image.get());

    // 5 entries of 54/23/12/28/43 blocks; data starts at 254.
    TEST_ASSERT_TRUE(image->seekPath(mstr::toUTF8("ASS.C.NOTE\\TOPAZ")));
    TEST_ASSERT_EQUAL_UINT32(7031, image->size());
    TEST_ASSERT_EQUAL_UINT32(22860, image->containerStream->position());

    // The '/' form - what a C64 sends over IEC, where no shell escaping is
    // involved - must resolve identically.
    auto other = openImage<TestARKStream>(PATH);
    TEST_ASSERT_NOT_NULL(other.get());
    TEST_ASSERT_TRUE(other->seekPath(mstr::toUTF8("ASS.C.NOTE/TOPAZ")));
    TEST_ASSERT_EQUAL_UINT32(7031, other->size());
    TEST_ASSERT_EQUAL_UINT32(22860, other->containerStream->position());
}

// seekEntry() matches against mstr::toUTF8(entry name), and the PETSCII map
// sends 'A'-'Z' to 'a'-'z' - so for these archives the UTF-8 form is NOT the
// raw bytes, and a listing that prints the raw name shows something that
// cannot be typed back. That is what ARK and LNX did by naming their entries
// through sourceFile->url (the archive's PARENT directory), which resolves to
// a plain file with isPETSCII false so `ls` skips the conversion.
//
// getNextFileInDir() needs MFSOwner and cannot run here; what this pins is
// the invariant that made the mismatch visible.
void test_ark_lookup_domain_is_utf8_not_raw(void)
{
    static const char* PATH = ".data/media/archive/ark/Tpztools.ark";
    if (!haveFile(PATH))
        TEST_IGNORE_MESSAGE("sample .data/media/archive/ark/Tpztools.ark not present");

    // The conversion is not a no-op for these names.
    TEST_ASSERT_EQUAL_STRING("w.bazaar  /topaz", mstr::toUTF8("W.BAZAAR  /TOPAZ").c_str());

    {   // The UTF-8 form - what a listing must show - resolves.
        auto image = openImage<TestARKStream>(PATH);
        TEST_ASSERT_NOT_NULL(image.get());
        TEST_ASSERT_TRUE(image->seekPath("w.bazaar  \\topaz"));
        TEST_ASSERT_EQUAL_UINT32(10772, image->size());
        TEST_ASSERT_EQUAL_UINT32(29972, image->containerStream->position());
    }
    {   // The raw PETSCII bytes do not - this is exactly what the console
        // sent when the listing printed the unconverted name.
        auto image = openImage<TestARKStream>(PATH);
        TEST_ASSERT_NOT_NULL(image.get());
        TEST_ASSERT_FALSE(image->seekPath("W.BAZAAR  \\TOPAZ"));
    }
}

/********************************************************
 * Isolation: the same lookups on a stream whose directory was parsed
 * explicitly. These pass before and after the fix, so a failure here means
 * the fixture is wrong rather than the lazy parse.
 ********************************************************/

void test_explicit_parse_finds_the_same_entries(void)
{
    {
        auto image = openImage<TestARKStream>(ARK_PATH);
        TEST_ASSERT_NOT_NULL(image.get());
        TEST_ASSERT_TRUE(image->readHeader());
        TEST_ASSERT_TRUE(image->seekEntry((uint16_t)2));
        TEST_ASSERT_EQUAL_STRING_LEN(NAME_2, image->entry.filename, strlen(NAME_2));
    }
    {
        auto image = openImage<TestLBRStream>(LBR_PATH);
        TEST_ASSERT_NOT_NULL(image.get());
        TEST_ASSERT_EQUAL_INT(2, image->loadEntries());
        TEST_ASSERT_TRUE(image->seekEntry((uint16_t)2));
        TEST_ASSERT_EQUAL_STRING(NAME_2, image->entry.filename.c_str());
    }
    {
        auto image = openImage<TestLNXStream>(LNX_PATH);
        TEST_ASSERT_NOT_NULL(image.get());
        TEST_ASSERT_TRUE(image->readHeader());
        TEST_ASSERT_EQUAL_INT(2, image->loadEntries());
        TEST_ASSERT_TRUE(image->seekEntry((uint16_t)2));
        TEST_ASSERT_EQUAL_STRING_LEN(NAME_2, image->entry.filename.c_str(), strlen(NAME_2));
    }
    {
        auto image = openImage<TestT64Stream>(T64_PATH);
        TEST_ASSERT_NOT_NULL(image.get());
        TEST_ASSERT_TRUE(image->readHeader());
        TEST_ASSERT_TRUE(image->seekEntry((uint16_t)2));
        TEST_ASSERT_EQUAL_STRING_LEN(NAME_2, image->entry.filename, strlen(NAME_2));
    }
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_ark_lookup_without_listing);
    RUN_TEST(test_ark_wildcard_without_listing);

    RUN_TEST(test_lbr_lookup_without_listing);
    RUN_TEST(test_lbr_by_name_walk_runs);
    RUN_TEST(test_lbr_missing_name_is_not_found);

    RUN_TEST(test_lnx_lookup_without_listing);
    RUN_TEST(test_lnx_repeated_lookups_do_not_grow_the_directory);
    RUN_TEST(test_lnx_entry_offsets_and_sizes_use_254_byte_blocks);
    RUN_TEST(test_lnx_real_archive_entries_tile_the_file);
    RUN_TEST(test_lnx_real_archive_keeps_trailing_spaces);
    RUN_TEST(test_rtrim_variants_differ_on_trailing_space);

    RUN_TEST(test_t64_lookup_without_listing);
    RUN_TEST(test_t64_wildcard_without_listing);

    RUN_TEST(test_lbr_real_archive_sizes);
    RUN_TEST(test_ark_real_archive_sizes);
    RUN_TEST(test_ark_real_archive_data_offsets);
    RUN_TEST(test_ark_backslash_name_round_trip);
    RUN_TEST(test_ark_lookup_domain_is_utf8_not_raw);

    RUN_TEST(test_explicit_parse_finds_the_same_entries);

    return UNITY_END();
}
