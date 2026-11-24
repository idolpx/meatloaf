// Reading existing REL files out of D64-family images. Fixtures are made by
// VICE's c1541 so the side-sector layout is not our own reading of it.
#include <unity.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "media/disk/d64.h"
#include "media/disk/d81.h"
#include "media/file/rel.h"
#include "../test_disk_write/file_container_stream.h"
#include "../test_disk_write/c1541_oracle.h"

static const char* kHost = "build_rel_host.bin";
static const char* kD64 = "build_rel.d64";
static const char* kD81 = "build_rel.d81";
static const char* kOut = "build_rel_out.bin";

void setUp(void) {}
void tearDown(void)
{
    remove(kHost); remove(kD64); remove(kD81); remove(kOut);
}

static std::vector<uint8_t> pattern(uint32_t records, uint32_t len)
{
    std::vector<uint8_t> v(records * len);
    for (uint32_t r = 0; r < records; r++)
        for (uint32_t i = 0; i < len; i++)
            v[r * len + i] = (uint8_t)(r * 7 + i * 3 + 1);   // never a long zero run
    return v;
}

static void make_image(const char* type, const char* path, const std::vector<uint8_t>& data, uint32_t len)
{
    FILE* fp = fopen(kHost, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_UINT32(data.size(), (uint32_t)fwrite(data.data(), 1, data.size(), fp));
    fclose(fp);
    int rc = 0;
    std::string out = c1541_run("-format " + c1541_quote("t,01") + " " + type + " " + c1541_quote(path) +
                                " -write " + c1541_quote(kHost) + " " + c1541_quote("data,l," + std::to_string(len)), &rc);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, out.c_str());
    TEST_ASSERT_TRUE_MESSAGE(out.find("REL file") != std::string::npos, out.c_str());
}

// Opens DATA from the image, checks the record length, and compares every byte
// the storage serves against both the source pattern and what c1541 itself
// reads back out of the image.
static void check(D64MStream& image, const char* path, const std::vector<uint8_t>& data, uint32_t len)
{
    image.mode = std::ios_base::in;
    TEST_ASSERT_TRUE(image.seekPath("data"));
    TEST_ASSERT_EQUAL_UINT8(len, image.relRecordLength());

    auto st = image.relStorage();
    TEST_ASSERT_NOT_NULL(st.get());

    // c1541 pads a REL file out to whole side-sector-allocated blocks, so the
    // truth for the size is what c1541 itself reads back, not data.size().
    size_t n = 0;
    TEST_ASSERT_TRUE(c1541_read(path, "data,l", kOut, &n));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, st->size());
    TEST_ASSERT_TRUE(st->size() >= data.size());

    std::vector<uint8_t> got(st->size());
    TEST_ASSERT_EQUAL_UINT32(got.size(), st->readAt(0, got.data(), (uint32_t)got.size()));
    TEST_ASSERT_TRUE(std::equal(data.begin(), data.end(), got.begin()));

    // The whole file matches c1541's own read-back, not just the part we wrote.
    std::string ref;
    TEST_ASSERT_TRUE(c1541_read_raw_file(kOut, ref));
    TEST_ASSERT_EQUAL_MEMORY(ref.data(), got.data(), got.size());

    // Offsets that straddle block boundaries read the same bytes.
    for (uint32_t off : { 253u, 254u, 507u })
        if (off + 10 <= got.size())
        {
            uint8_t b[10];
            TEST_ASSERT_EQUAL_UINT32(10, st->readAt(off, b, 10));
            TEST_ASSERT_EQUAL_MEMORY(&got[off], b, 10);
        }
    uint8_t one;
    TEST_ASSERT_EQUAL_UINT32(0, st->readAt(st->size(), &one, 1));
}

void test_rel_d64_len10_three_records(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    auto data = pattern(3, 10);
    make_image("d64", kD64, data, 10);
    D64MStream image(std::make_shared<FileContainerStream>(kD64));
    check(image, kD64, data, 10);
}

void test_rel_d64_len254_crosses_side_sector_group(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    auto data = pattern(130, 254);   // 130 data blocks > 120 per side sector
    make_image("d64", kD64, data, 254);
    D64MStream image(std::make_shared<FileContainerStream>(kD64));
    check(image, kD64, data, 254);
}

void test_rel_d81_len100_needs_second_super_side_group(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    auto data = pattern(1900, 100);  // 190000 bytes = 748 blocks > 720 (6 side sectors)
    make_image("d81", kD81, data, 100);
    D81MStream image(std::make_shared<FileContainerStream>(kD81));
    check(image, kD81, data, 100);
}

void test_rel_record_length_zero_for_non_rel(void)
{
    if (!c1541_available()) TEST_IGNORE_MESSAGE("c1541 not found; set C1541 env var");
    std::vector<uint8_t> data(300, 0x41);
    FILE* fp = fopen(kHost, "wb"); fwrite(data.data(), 1, data.size(), fp); fclose(fp);
    c1541_run("-format " + c1541_quote("t,01") + " d64 " + c1541_quote(kD64) + " -write " + c1541_quote(kHost) + " data");
    D64MStream image(std::make_shared<FileContainerStream>(kD64));
    image.mode = std::ios_base::in;
    TEST_ASSERT_TRUE(image.seekPath("data"));
    TEST_ASSERT_EQUAL_UINT8(0, image.relRecordLength());
    TEST_ASSERT_NULL(image.relStorage().get());
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_rel_d64_len10_three_records);
    RUN_TEST(test_rel_d64_len254_crosses_side_sector_group);
    RUN_TEST(test_rel_d81_len100_needs_second_super_side_group);
    RUN_TEST(test_rel_record_length_zero_for_non_rel);
    return UNITY_END();
}
