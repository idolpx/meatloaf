// Read-path tests for the T64 tape format.
//
// Written to pin the load address a T64 entry loads to. T64 data records hold
// raw program bytes, so the two bytes a PRG begins with are synthesized from
// the directory entry's start address - and the high byte of that was being
// MASKED (`start_address & 0xFF00`) into a uint8_t rather than SHIFTED, which
// stores 0 for every possible address. Every T64 entry therefore loaded with a
// high byte of $00: a PRG at $0801 went to $0001.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../test_disk_write/file_container_stream.h"
#include "media/tape/t64.h"

static const char* T64_PATH = "build_test_t64.t64";

// T64 layout: 32-byte description, then the header at $20, then 32-byte
// directory entries from $40. Data records follow; these images place them
// at $100, past a two-entry directory.
static const uint32_t DIR_OFFSET  = 0x40;
static const uint32_t ENTRY_SIZE  = 32;
static const uint32_t DATA_OFFSET = 0x100;

// seekPath()/seekEntry() are protected; the tests drive them the way a LOAD does.
class TestT64Stream : public T64MStream
{
public:
    using T64MStream::T64MStream;
    using T64MStream::entry;
    using T64MStream::header;
    using T64MStream::readHeader;
    using T64MStream::seekEntry;
    using T64MStream::seekPath;
};

static void put16(std::vector<uint8_t>& img, uint32_t at, uint16_t v)
{
    img[at]     = v & 0xFF;
    img[at + 1] = (v >> 8) & 0xFF;
}

static void put32(std::vector<uint8_t>& img, uint32_t at, uint32_t v)
{
    img[at]     = v & 0xFF;
    img[at + 1] = (v >> 8) & 0xFF;
    img[at + 2] = (v >> 16) & 0xFF;
    img[at + 3] = (v >> 24) & 0xFF;
}

static void putEntry(std::vector<uint8_t>& img, uint16_t index,
                     uint16_t start, uint16_t end, uint32_t data_offset,
                     const std::string& name, uint8_t pad)
{
    uint32_t at = DIR_OFFSET + (uint32_t)index * ENTRY_SIZE;
    img[at + 0] = 1;            // entry_type: normal tape file
    img[at + 1] = 0x82;         // file_type: PRG
    put16(img, at + 2, start);
    put16(img, at + 4, end);
    put16(img, at + 6, 0);      // free_1
    put32(img, at + 8, data_offset);
    put32(img, at + 12, 0);     // free_2
    for (size_t i = 0; i < 16; i++)
        img[at + 16 + i] = (i < name.size()) ? (uint8_t)name[i] : pad;
}

// Two entries whose start addresses have DIFFERENT high bytes, so a masked
// high byte cannot coincidentally look right for both:
//   "BASIC"  $0801  (256 bytes, fill $AA)
//   "HIMEM"  $C000  (256 bytes, fill $BB)
//
// `pad` is the filename field's padding byte. The T64 spec says $20, but most
// files in the wild are $00-padded, and T64MStream only resolves a name in the
// $00 case - see test_space_padded_name_lookup below. The load-address tests
// use $00 so they measure the load address and nothing else.
static void writeImage(uint8_t pad = 0x00)
{
    std::vector<uint8_t> img(DATA_OFFSET + 512, 0x00);

    const char* sig = "C64 tape image file";
    memcpy(img.data(), sig, strlen(sig));

    put16(img, 0x20, 0x0101);   // version
    put16(img, 0x22, 2);        // entry_max
    put16(img, 0x24, 2);        // entry_count
    put16(img, 0x26, 0);        // unused
    for (size_t i = 0; i < 24; i++)
        img[0x28 + i] = 0x20;   // name, space padded

    putEntry(img, 0, 0x0801, 0x0901, DATA_OFFSET, "BASIC", pad);
    putEntry(img, 1, 0xC000, 0xC100, DATA_OFFSET + 256, "HIMEM", pad);

    memset(img.data() + DATA_OFFSET, 0xAA, 256);
    memset(img.data() + DATA_OFFSET + 256, 0xBB, 256);

    FILE* fp = fopen(T64_PATH, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    fwrite(img.data(), 1, img.size(), fp);
    fclose(fp);
}

// seekEntry() compares against mstr::toUTF8(entry.filename), because what
// arrives from the drive is PETSCII that has been through the same conversion.
static std::string q(const char* name)
{
    return mstr::toUTF8(name);
}

static std::shared_ptr<TestT64Stream> openImage()
{
    auto src = std::make_shared<FileContainerStream>(T64_PATH);
    if (!src->isOpen())
        return nullptr;

    auto image = std::make_shared<TestT64Stream>(src);
    // A directly constructed media stream has an uninitialised `mode` - only
    // MFile::getSourceStream() sets it, and garbage carrying the `out` bit
    // makes seekPath() take a write path.
    image->mode = std::ios_base::in;
    return image;
}

void setUp(void)
{
    writeImage();
}

void tearDown(void)
{
    remove(T64_PATH);
}

// The bug. `start_address & 0xFF00` assigned into a uint8_t is always 0,
// because that mask clears exactly the eight bits the assignment keeps.
void test_load_address_high_byte_is_shifted_not_masked(void)
{
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekPath(q("BASIC")));

    uint8_t buf[2] = { 0, 0 };
    TEST_ASSERT_EQUAL_UINT32(2, image->read(buf, 2));

    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x08, buf[1]);   // was 0x00
}

// A second address with a different high byte, so the fix cannot be a constant
// that happens to suit $0801.
void test_high_load_address_survives(void)
{
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekPath(q("HIMEM")));

    uint8_t buf[2] = { 0, 0 };
    TEST_ASSERT_EQUAL_UINT32(2, image->read(buf, 2));

    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xC0, buf[1]);   // was 0x00
}

// The whole record: synthesized load address followed by the entry's own data.
void test_full_read_is_load_address_then_data(void)
{
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekPath(q("HIMEM")));
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
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xC0, buf[1]);
    // Its own fill, not the first entry's.
    TEST_ASSERT_EQUAL_HEX8(0xBB, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, buf[257]);
}

// seekEntry() indexes the directory as `0x40 + (index * sizeof(entry))`, which
// is only correct while the struct has no padding. If a compiler ever inserts
// some, every entry after the first is read from the wrong offset - so pin it.
void test_entry_struct_is_exactly_32_bytes(void)
{
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_EQUAL_UINT32(ENTRY_SIZE, (uint32_t)sizeof(image->entry));
}

// The header carries the entry count, and a stream that went straight to a
// LOAD has never read it.
void test_header_and_directory_are_read(void)
{
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->readHeader());
    TEST_ASSERT_EQUAL_UINT16(2, image->header.entry_count);

    TEST_ASSERT_TRUE(image->seekEntry((uint16_t)2));
    TEST_ASSERT_EQUAL_UINT16(0xC000, image->entry.start_address);

    TEST_ASSERT_FALSE(image->seekEntry((uint16_t)3));
}

// KNOWN ISSUE, separate from the load-address fix and deliberately not fixed
// here. T64MStream::seekEntry(std::string) compares the query against the raw
// 16-byte filename field, which it never trims - the rtrimA0() call is
// commented out in t64.cpp. For a $00-padded file the std::string constructor
// stops at the first NUL and the name comes out clean, which is why this has
// gone unnoticed; for a $20-padded file (what the T64 spec actually specifies)
// the entry compares as "himem           " and no exact-name LOAD can match it.
// Only LOAD"*" and wildcards work on those images.
//
// It is left open because the fix is a behaviour change with a real trade-off:
// trailing spaces can be part of a CBM name, which is precisely why
// mstr::rtrimA0() strips only $A0 and mstr::rtrimPad() exists as the separate
// opt-in for fixed-width fields. Lift the guard and re-run to verify a fix.
void test_space_padded_name_lookup(void)
{
    TEST_IGNORE_MESSAGE("open finding: T64 does not trim the 16-byte filename field, so exact-name lookup fails on $20-padded images");

    writeImage(0x20);
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekPath(q("HIMEM")));
}

// The wildcard path works regardless of padding, which is the workaround for
// the finding above and worth pinning so a future fix does not break it.
void test_wildcard_lookup_works_on_space_padded_names(void)
{
    writeImage(0x20);
    auto image = openImage();
    TEST_ASSERT_NOT_NULL(image.get());

    TEST_ASSERT_TRUE(image->seekPath(q("HIMEM*")));

    uint8_t buf[2] = { 0, 0 };
    TEST_ASSERT_EQUAL_UINT32(2, image->read(buf, 2));
    TEST_ASSERT_EQUAL_HEX8(0xC0, buf[1]);
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_load_address_high_byte_is_shifted_not_masked);
    RUN_TEST(test_high_load_address_survives);
    RUN_TEST(test_full_read_is_load_address_then_data);
    RUN_TEST(test_entry_struct_is_exactly_32_bytes);
    RUN_TEST(test_header_and_directory_are_read);
    RUN_TEST(test_space_padded_name_lookup);
    RUN_TEST(test_wildcard_lookup_works_on_space_padded_names);
    return UNITY_END();
}
