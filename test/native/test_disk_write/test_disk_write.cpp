#include <unity.h>
#include <cstdio>
#include <memory>
#include "media/disk/d64.h"
#include "media/disk/d71.h"
#include "media/disk/d80.h"
#include "media/disk/d81.h"
#include "media/disk/d82.h"
#include "file_container_stream.h"
#include "c1541_oracle.h"

void setUp(void) {}
void tearDown(void) {}

void test_file_container_stream_roundtrip(void)
{
    const char* path = "build_test_fcs.bin";
    remove(path);
    {
        FileContainerStream s(path, 1024);
        TEST_ASSERT_TRUE(s.isOpen());
        TEST_ASSERT_EQUAL_UINT32(1024, s.size());

        const uint8_t out[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        TEST_ASSERT_TRUE(s.seek(256));
        TEST_ASSERT_EQUAL_UINT32(4, s.write(out, 4));

        uint8_t in[4] = { 0, 0, 0, 0 };
        TEST_ASSERT_TRUE(s.seek(256));
        TEST_ASSERT_EQUAL_UINT32(4, s.read(in, 4));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(out, in, 4);
    }
    // Reopening must see the persisted bytes and the original size.
    {
        FileContainerStream s(path);
        TEST_ASSERT_EQUAL_UINT32(1024, s.size());
        uint8_t in[4] = { 0, 0, 0, 0 };
        TEST_ASSERT_TRUE(s.seek(256));
        TEST_ASSERT_EQUAL_UINT32(4, s.read(in, 4));
        TEST_ASSERT_EQUAL_UINT8(0xDE, in[0]);
        TEST_ASSERT_EQUAL_UINT8(0xEF, in[3]);
    }
    remove(path);
}

// Proves the write engine compiles, links, and constructs natively, by reading
// values that only exist if its geometry tables were built.
//
// speedZone(track) = (track<18) + (track<25) + (track<31), indexing
// sectorsPerTrack = {17, 18, 19, 21}. That maps: tracks 1-17 -> 21 sectors,
// tracks 18-24 -> 19, tracks 25-30 -> 18, tracks 31-35 -> 17 (verified against
// d64.h; the brief's placeholder values for tracks 25 and 31 didn't match the
// code and are corrected here per the brief's own instruction to do so).
void test_engine_constructs_with_expected_geometry(void)
{
    const char* path = "build_test_geom.d64";
    remove(path);
    auto src = std::make_shared<FileContainerStream>(path, 174848);
    D64MStream image(src);

    TEST_ASSERT_EQUAL_UINT16(21, image.getSectorCount(1));   // zone 1 (tracks 1-17)
    TEST_ASSERT_EQUAL_UINT16(18, image.getSectorCount(25));  // zone 3 (tracks 25-30)
    TEST_ASSERT_EQUAL_UINT16(17, image.getSectorCount(31));  // zone 4 (tracks 31-35)
    TEST_ASSERT_EQUAL_UINT16(17, image.getSectorCount(35));  // zone 4 (tracks 31-35)
    TEST_ASSERT_EQUAL_UINT8(18, image.partitions[image.partition].directory_track);

    // Close the underlying file before removing it - on Windows an open
    // handle (still held via src's shared_ptr) blocks remove().
    src->close();
    remove(path);
}

void test_default_image_sizes(void)
{
    const char* path = "build_test_sizes.bin";
    remove(path);
    auto src = std::make_shared<FileContainerStream>(path, 256);

    TEST_ASSERT_EQUAL_UINT32(174848,  D64MStream(src).defaultImageSize());
    TEST_ASSERT_EQUAL_UINT32(349696,  D71MStream(src).defaultImageSize());
    TEST_ASSERT_EQUAL_UINT32(533248,  D80MStream(src).defaultImageSize());
    TEST_ASSERT_EQUAL_UINT32(819200,  D81MStream(src).defaultImageSize());
    TEST_ASSERT_EQUAL_UINT32(1066496, D82MStream(src).defaultImageSize());

    remove(path);
}

void test_format_image_creates_sized_image(void)
{
    const char* path = "build_test_fmt.d64";
    remove(path);
    {
        auto src = std::make_shared<FileContainerStream>(path, 174848);
        D64MStream image(src);
        TEST_ASSERT_TRUE(image.formatImage("testdisk", "01"));
    }
    FILE* fp = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    fseek(fp, 0, SEEK_END);
    TEST_ASSERT_EQUAL_UINT32(174848, (uint32_t)ftell(fp));
    fclose(fp);
    remove(path);
}

void test_c1541_validates_our_formatted_image(void)
{
    if (!c1541_available())
        TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");

    const char* path = "build_test_oracle.d64";
    remove(path);
    {
        auto src = std::make_shared<FileContainerStream>(path, 174848);
        D64MStream image(src);
        TEST_ASSERT_TRUE(image.formatImage("testdisk", "01"));
    }
    TEST_ASSERT_TRUE_MESSAGE(c1541_validate(path),
                             "c1541 validate rejected our formatted image");
    remove(path);
}

void process()
{
    UNITY_BEGIN();
    RUN_TEST(test_file_container_stream_roundtrip);
    RUN_TEST(test_engine_constructs_with_expected_geometry);
    RUN_TEST(test_default_image_sizes);
    RUN_TEST(test_format_image_creates_sized_image);
    RUN_TEST(test_c1541_validates_our_formatted_image);
    UNITY_END();
}

int main(int argc, char **argv)
{
    process();
    return 0;
}
