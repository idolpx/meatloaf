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

    b.resize(256 * 3, 0);               // directory_blocks * block_size data
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
        using Base::readHeader;                           \
        using Base::seekEntry;                            \
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

    RUN_TEST(test_t64_lookup_without_listing);
    RUN_TEST(test_t64_wildcard_without_listing);

    RUN_TEST(test_explicit_parse_finds_the_same_entries);

    return UNITY_END();
}
